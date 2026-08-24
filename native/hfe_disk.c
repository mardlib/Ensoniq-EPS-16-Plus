#include "hfe_disk.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EPS_TRACKS = 80,
    EPS_SIDES = 2,
    EPS_SECTORS = 10,
    EPS_SECTOR_SIZE = 512
};

static void fail(char *error, size_t size, const char *format, ...) {
    if (!error || !size) return;
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t crc16(const uint8_t *data, size_t size, uint16_t crc) {
    while (size--) {
        crc ^= (uint16_t)*data++ << 8;
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = crc & 0x8000 ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint8_t reverse_bits(uint8_t value) {
    value = (uint8_t)(((value & 0x55) << 1) | ((value >> 1) & 0x55));
    value = (uint8_t)(((value & 0x33) << 2) | ((value >> 2) & 0x33));
    return (uint8_t)((value << 4) | (value >> 4));
}

static uint8_t decode_word(const uint8_t *encoded) {
    uint16_t word = be16(encoded);
    uint8_t value = 0;
    for (int bit = 14; bit >= 0; bit -= 2)
        value = (uint8_t)((value << 1) | ((word >> bit) & 1));
    return value;
}

static void put_le16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void put_be24(uint8_t *target, unsigned int value) {
    target[0] = (uint8_t)(value >> 16);
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)value;
}

static void put_be32(uint8_t *target, unsigned int value) {
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

int eps16_disk_create_blank(uint8_t *logical, size_t logical_size) {
    if (!logical || logical_size != EPS16_LOGICAL_DISK_SIZE) return 0;
    memset(logical, 0, logical_size);

    /* EPS-formatted data disk: 80 cylinders, two heads, ten 512-byte
       sectors. Block zero uses the formatter's standard repeating fill. */
    for (size_t index = 0; index < EPS_SECTOR_SIZE; ++index)
        logical[index] = (index & 1U) ? 0xb6 : 0x6d;

    uint8_t *device = logical + EPS_SECTOR_SIZE;
    const uint8_t descriptor[] = {
        0x00, 0x80, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x02,
        0x00, 0x50, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x06, 0x40, 0x1e, 0x02
    };
    memcpy(device, descriptor, sizeof(descriptor));
    device[30] = 0xff;
    memcpy(device + 31, "NEWDISK", 7);
    memcpy(device + 38, "ID", 2);

    uint8_t *system = logical + 2 * EPS_SECTOR_SIZE;
    const unsigned int free_blocks = 1600 - 15;
    system[0] = (uint8_t)(free_blocks >> 24);
    system[1] = (uint8_t)(free_blocks >> 16);
    system[2] = (uint8_t)(free_blocks >> 8);
    system[3] = (uint8_t)free_blocks;
    memcpy(system + 28, "OS", 2);

    uint8_t *directory_tail = logical + 5 * EPS_SECTOR_SIZE - 2;
    memcpy(directory_tail, "DR", 2);

    /* Blocks 0..14 are reserved. Each FAT block stores 170 big-endian
       24-bit entries followed by its FB signature. */
    uint8_t *fat = logical + 5 * EPS_SECTOR_SIZE;
    for (unsigned int entry = 0; entry < 15; ++entry)
        fat[entry * 3 + 2] = 1;
    for (unsigned int block = 5; block < 15; ++block)
        memcpy(logical + (block + 1) * EPS_SECTOR_SIZE - 2, "FB", 2);
    return 1;
}

static uint8_t *fat_entry(uint8_t *logical, unsigned int block) {
    const unsigned int fat_block = block / 170U;
    const unsigned int index = block % 170U;
    return logical + (5U + fat_block) * EPS_SECTOR_SIZE + index * 3U;
}

static int create_from_efe(const uint8_t *efe, size_t efe_size,
                           uint8_t *logical, size_t logical_size,
                           char *error, size_t error_size) {
    static const uint8_t signature[] = {
        '\r', '\n', 'E', 'p', 's', ' ', 'F', 'i', 'l', 'e', ':'
    };
    enum {
        EFE_HEADER_SIZE = 512,
        EFE_NAME_OFFSET = 0x12,
        EFE_NAME_SIZE = 12,
        EFE_TYPE_OFFSET = 0x32,
        EFE_BLOCKS_OFFSET = 0x34,
        EFE_COPY_BLOCKS_OFFSET = 0x36,
        EPS_RESERVED_BLOCKS = 15,
        EPS_DIRECTORY_ENTRY_SIZE = 25,
        EPS_DIRECTORY_FIRST_FILE = 1,
        EPS_TOTAL_BLOCKS = EPS16_LOGICAL_DISK_SIZE / EPS_SECTOR_SIZE
    };
    if (!efe || efe_size < EFE_HEADER_SIZE ||
        efe_size % EPS_SECTOR_SIZE ||
        memcmp(efe, signature, sizeof(signature)) || efe[0x31] != 0x1a) {
        fail(error, error_size, "invalid or truncated Ensoniq EFE header");
        return 0;
    }
    const size_t data_blocks = efe_size / EPS_SECTOR_SIZE - 1U;
    const unsigned int declared_blocks = be16(efe + EFE_BLOCKS_OFFSET);
    const unsigned int copied_blocks = be16(efe + EFE_COPY_BLOCKS_OFFSET);
    if (!data_blocks || data_blocks != declared_blocks ||
        data_blocks != copied_blocks) {
        fail(error, error_size,
             "EFE size/header mismatch: file has %zu data blocks, header says %u/%u",
             data_blocks, declared_blocks, copied_blocks);
        return 0;
    }
    if (data_blocks > EPS_TOTAL_BLOCKS - EPS_RESERVED_BLOCKS) {
        fail(error, error_size,
             "EFE needs %zu blocks; an EPS DD disk has %u free blocks",
             data_blocks, EPS_TOTAL_BLOCKS - EPS_RESERVED_BLOCKS);
        return 0;
    }
    if (!efe[EFE_TYPE_OFFSET]) {
        fail(error, error_size, "EFE has no Ensoniq file type");
        return 0;
    }
    int has_name = 0;
    for (size_t index = 0; index < EFE_NAME_SIZE; ++index)
        has_name |= efe[EFE_NAME_OFFSET + index] != ' ' &&
                    efe[EFE_NAME_OFFSET + index] != 0;
    if (!has_name) {
        fail(error, error_size, "EFE has an empty Ensoniq file name");
        return 0;
    }
    if (!eps16_disk_create_blank(logical, logical_size)) {
        fail(error, error_size, "cannot create temporary EPS disk for EFE");
        return 0;
    }

    memcpy(logical + EPS_SECTOR_SIZE + 31, "EFELOAD", 7);
    put_be32(logical + 2U * EPS_SECTOR_SIZE,
             (unsigned int)(EPS_TOTAL_BLOCKS - EPS_RESERVED_BLOCKS -
                            data_blocks));

    uint8_t *entry = logical + 3U * EPS_SECTOR_SIZE +
                     EPS_DIRECTORY_FIRST_FILE * EPS_DIRECTORY_ENTRY_SIZE;
    entry[2] = efe[EFE_TYPE_OFFSET];
    memcpy(entry + 3, efe + EFE_NAME_OFFSET, EFE_NAME_SIZE);
    memcpy(entry + 15, efe + EFE_BLOCKS_OFFSET, 2);
    memcpy(entry + 17, efe + EFE_COPY_BLOCKS_OFFSET, 2);
    put_be32(entry + 19, EPS_RESERVED_BLOCKS);

    memcpy(logical + EPS_RESERVED_BLOCKS * EPS_SECTOR_SIZE,
           efe + EFE_HEADER_SIZE, data_blocks * EPS_SECTOR_SIZE);
    for (size_t index = 0; index < data_blocks; ++index) {
        const unsigned int block = EPS_RESERVED_BLOCKS + (unsigned int)index;
        put_be24(fat_entry(logical, block),
                 index + 1U < data_blocks ? block + 1U : 1U);
    }
    return 1;
}

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    unsigned int previous_data_bit;
} MfmStream;

static int mfm_word(MfmStream *stream, uint16_t word) {
    if (stream->size + 2 > stream->capacity) return 0;
    stream->data[stream->size++] = (uint8_t)(word >> 8);
    stream->data[stream->size++] = (uint8_t)word;
    return 1;
}

static int mfm_byte(MfmStream *stream, uint8_t value) {
    uint16_t word = 0;
    for (int bit = 7; bit >= 0; --bit) {
        const unsigned int data_bit = (value >> bit) & 1U;
        const unsigned int clock_bit =
            !(stream->previous_data_bit || data_bit);
        word = (uint16_t)((word << 1) | clock_bit);
        word = (uint16_t)((word << 1) | data_bit);
        stream->previous_data_bit = data_bit;
    }
    return mfm_word(stream, word);
}

static int mfm_repeat(MfmStream *stream, uint8_t value, size_t count) {
    while (count--)
        if (!mfm_byte(stream, value)) return 0;
    return 1;
}

static int mfm_sync_a1(MfmStream *stream) {
    stream->previous_data_bit = 1;
    return mfm_word(stream, 0x4489);
}

static int encode_track_side(const uint8_t *logical, unsigned int track,
                             unsigned int side, uint8_t *encoded,
                             size_t encoded_size) {
    MfmStream stream = {encoded, 0, encoded_size, 0};
    if (!mfm_repeat(&stream, 0x4e, 80)) return 0;
    /* Match the physical sector skew used by EPS-formatted media.  Each
       cylinder advances six sector positions and side one begins two
       positions before side zero. */
    const unsigned int first_sector =
        (track * 6U + side * 8U) % EPS_SECTORS;
    for (unsigned int position = 0; position < EPS_SECTORS; ++position) {
        const unsigned int sector = (first_sector + position) % EPS_SECTORS;
        uint8_t id[5] = {0xfe, (uint8_t)track, (uint8_t)side,
                         (uint8_t)sector, 2};
        uint16_t id_crc = crc16((const uint8_t *)"\xa1\xa1\xa1", 3, 0xffff);
        id_crc = crc16(id, sizeof(id), id_crc);
        if (!mfm_repeat(&stream, 0x00, 12) ||
            !mfm_sync_a1(&stream) || !mfm_sync_a1(&stream) ||
            !mfm_sync_a1(&stream)) return 0;
        for (size_t index = 0; index < sizeof(id); ++index)
            if (!mfm_byte(&stream, id[index])) return 0;
        if (!mfm_byte(&stream, (uint8_t)(id_crc >> 8)) ||
            !mfm_byte(&stream, (uint8_t)id_crc) ||
            !mfm_repeat(&stream, 0x4e, 22) ||
            !mfm_repeat(&stream, 0x00, 12) ||
            !mfm_sync_a1(&stream) || !mfm_sync_a1(&stream) ||
            !mfm_sync_a1(&stream) || !mfm_byte(&stream, 0xfb))
            return 0;

        const size_t block = ((track * EPS_SIDES + side) * EPS_SECTORS) + sector;
        const uint8_t *sector_data = logical + block * EPS_SECTOR_SIZE;
        uint16_t data_crc = crc16((const uint8_t *)"\xa1\xa1\xa1", 3, 0xffff);
        const uint8_t mark = 0xfb;
        data_crc = crc16(&mark, 1, data_crc);
        data_crc = crc16(sector_data, EPS_SECTOR_SIZE, data_crc);
        for (size_t index = 0; index < EPS_SECTOR_SIZE; ++index)
            if (!mfm_byte(&stream, sector_data[index])) return 0;
        if (!mfm_byte(&stream, (uint8_t)(data_crc >> 8)) ||
            !mfm_byte(&stream, (uint8_t)data_crc) ||
            !mfm_repeat(&stream, 0x4e, 40)) return 0;
    }
    if (stream.size > encoded_size || (encoded_size - stream.size) % 2)
        return 0;
    return mfm_repeat(&stream, 0x4e, (encoded_size - stream.size) / 2) &&
           stream.size == encoded_size;
}

static uint8_t *encode_hfe(const uint8_t *logical, size_t logical_size,
                           size_t *image_size, char *error,
                           size_t error_size) {
    enum {
        HFE_HEADER_BLOCKS = 2,
        HFE_TRACK_BLOCKS = 49,
        HFE_TRACK_LENGTH = HFE_TRACK_BLOCKS * 512,
        HFE_SIDE_LENGTH = HFE_TRACK_BLOCKS * 256
    };
    if (!logical || logical_size != EPS16_LOGICAL_DISK_SIZE) {
        fail(error, error_size, "logical buffer must be %u bytes",
             EPS16_LOGICAL_DISK_SIZE);
        return NULL;
    }
    const size_t size = (HFE_HEADER_BLOCKS + EPS_TRACKS * HFE_TRACK_BLOCKS) * 512U;
    uint8_t *image = malloc(size);
    uint8_t *side_stream[EPS_SIDES] = {
        malloc(HFE_SIDE_LENGTH), malloc(HFE_SIDE_LENGTH)
    };
    if (!image || !side_stream[0] || !side_stream[1]) {
        free(side_stream[0]);
        free(side_stream[1]);
        free(image);
        fail(error, error_size, "out of memory encoding HFE disk");
        return NULL;
    }
    memset(image, 0xff, size);
    memcpy(image, "HXCPICFE", 8);
    image[8] = 0;
    image[9] = EPS_TRACKS;
    image[10] = EPS_SIDES;
    image[11] = 0;
    put_le16(image + 12, 250);
    put_le16(image + 14, 0);
    image[16] = 7;
    image[17] = 1;
    put_le16(image + 18, 1);

    for (unsigned int track = 0; track < EPS_TRACKS; ++track) {
        uint8_t *entry = image + 512 + track * 4;
        const unsigned int start_block = HFE_HEADER_BLOCKS + track * HFE_TRACK_BLOCKS;
        put_le16(entry, (uint16_t)start_block);
        put_le16(entry + 2, HFE_TRACK_LENGTH);
        if (!encode_track_side(logical, track, 0, side_stream[0],
                               HFE_SIDE_LENGTH) ||
            !encode_track_side(logical, track, 1, side_stream[1],
                               HFE_SIDE_LENGTH)) {
            free(side_stream[0]);
            free(side_stream[1]);
            free(image);
            fail(error, error_size, "cannot encode HFE track %u", track);
            return NULL;
        }
        uint8_t *track_data = image + (size_t)start_block * 512;
        for (size_t block = 0; block < HFE_TRACK_BLOCKS; ++block) {
            for (unsigned int side = 0; side < EPS_SIDES; ++side) {
                const size_t source = block * 256;
                const size_t count = source < HFE_SIDE_LENGTH
                    ? (HFE_SIDE_LENGTH - source > 256
                           ? 256 : HFE_SIDE_LENGTH - source)
                    : 0;
                for (size_t index = 0; index < count; ++index)
                    track_data[block * 512 + side * 256 + index] =
                        reverse_bits(side_stream[side][source + index]);
            }
        }
    }
    free(side_stream[0]);
    free(side_stream[1]);
    *image_size = size;
    return image;
}

static int save_atomic(const char *path, const uint8_t *data, size_t size,
                       char *error, size_t error_size) {
    const size_t path_length = strlen(path);
    char *temporary_path = malloc(path_length + 11);
    if (!temporary_path) {
        fail(error, error_size, "cannot allocate disk output path");
        return 0;
    }
    memcpy(temporary_path, path, path_length);
    memcpy(temporary_path + path_length, ".eps16.tmp", 11);
    FILE *output = fopen(temporary_path, "wb");
    int ok = output != NULL;
    if (ok && fwrite(data, 1, size, output) != size) ok = 0;
    if (output && fclose(output)) ok = 0;
    if (!ok || rename(temporary_path, path)) {
        remove(temporary_path);
        free(temporary_path);
        fail(error, error_size, "cannot save disk image: %s", path);
        return 0;
    }
    free(temporary_path);
    return 1;
}

static int decode_bytes(const uint8_t *stream, size_t stream_size, size_t offset,
                        uint8_t *output, size_t count) {
    if (offset > stream_size || count > (stream_size - offset) / 2) return 0;
    for (size_t index = 0; index < count; ++index)
        output[index] = decode_word(stream + offset + index * 2);
    return 1;
}

static int decode_hfe(const uint8_t *image, size_t image_size, uint8_t *logical,
                      size_t logical_size, uint8_t *sector_status,
                      int tolerate_physical_errors, char *error,
                      size_t error_size) {
    if (logical_size != EPS16_LOGICAL_DISK_SIZE) {
        fail(error, error_size, "logical buffer must be %u bytes", EPS16_LOGICAL_DISK_SIZE);
        return 0;
    }
    if (image_size < 1024 || memcmp(image, "HXCPICFE", 8)) {
        fail(error, error_size, "invalid HFE v1 signature or truncated header");
        return 0;
    }
    unsigned int tracks = image[9], sides = image[10], encoding = image[11];
    unsigned int bitrate = le16(image + 12), rpm = le16(image + 14);
    size_t table = (size_t)le16(image + 18) * 512;
    if (image[8] != 0 || tracks != EPS_TRACKS || sides != EPS_SIDES ||
        encoding != 0 || bitrate != 250 || (rpm != 0 && rpm != 300)) {
        fail(error, error_size,
             "unsupported EPS HFE geometry: rev=%u tracks=%u sides=%u encoding=%u bitrate=%u rpm=%u",
             image[8], tracks, sides, encoding, bitrate, rpm);
        return 0;
    }
    if (table < 512 || table > image_size || tracks * 4 > image_size - table) {
        fail(error, error_size, "invalid HFE track lookup table");
        return 0;
    }

    memset(logical, 0, logical_size);
    if (sector_status)
        memset(sector_status, EPS16_SECTOR_NOT_FOUND,
               EPS16_LOGICAL_SECTOR_COUNT);
    unsigned int total = 0;
    for (unsigned int track_index = 0; track_index < tracks; ++track_index) {
        const uint8_t *entry = image + table + track_index * 4;
        size_t track_start = (size_t)le16(entry) * 512;
        size_t track_size = le16(entry + 2);
        if (!track_size || track_start > image_size || track_size > image_size - track_start) {
            fail(error, error_size, "track %u is empty or outside the HFE file", track_index);
            return 0;
        }
        size_t side_capacity = ((track_size + 511) / 512) * 256;
        uint8_t *stream = malloc(side_capacity);
        if (!stream) {
            fail(error, error_size, "out of memory decoding HFE track %u", track_index);
            return 0;
        }
        for (unsigned int side = 0; side < sides; ++side) {
            size_t stream_size = 0;
            for (size_t block = 0; block < track_size; block += 512) {
                size_t start = block + side * 256;
                if (start >= track_size) continue;
                size_t count = track_size - start;
                if (count > 256) count = 256;
                for (size_t index = 0; index < count; ++index)
                    stream[stream_size++] = reverse_bits(image[track_start + start + index]);
            }

            unsigned int seen = 0;
            int pending_sector = -1;
            for (size_t position = 0; position + 6 <= stream_size; ++position) {
                static const uint8_t sync[6] = {0x44, 0x89, 0x44, 0x89, 0x44, 0x89};
                if (memcmp(stream + position, sync, sizeof(sync))) continue;
                size_t field = position + sizeof(sync);
                uint8_t mark;
                if (!decode_bytes(stream, stream_size, field, &mark, 1)) break;
                if (mark == 0xfe) {
                    uint8_t id[7];
                    if (!decode_bytes(stream, stream_size, field, id, sizeof(id))) {
                        free(stream);
                        fail(error, error_size, "truncated ID field at track %u side %u", track_index, side);
                        return 0;
                    }
                    uint16_t actual = crc16((const uint8_t *)"\xa1\xa1\xa1", 3, 0xffff);
                    actual = crc16(id, 5, actual);
                    if (id[1] != track_index || id[2] != side ||
                        id[3] >= EPS_SECTORS || id[4] != 2) {
                        if (tolerate_physical_errors) {
                            pending_sector = -1;
                            continue;
                        }
                        free(stream);
                        fail(error, error_size,
                             "invalid ID/CRC at track %u side %u: C=%u H=%u R=%u N=%u",
                             track_index, side, id[1], id[2], id[3], id[4]);
                        return 0;
                    }
                    if (actual != be16(id + 5)) {
                        if (tolerate_physical_errors) {
                            const size_t block =
                                ((track_index * EPS_SIDES + side) * EPS_SECTORS) + id[3];
                            if (sector_status[block] != EPS16_SECTOR_READABLE)
                                sector_status[block] = EPS16_SECTOR_CRC_ERROR;
                            pending_sector = -1;
                            continue;
                        }
                        free(stream);
                        fail(error, error_size,
                             "invalid ID/CRC at track %u side %u: C=%u H=%u R=%u N=%u",
                             track_index, side, id[1], id[2], id[3], id[4]);
                        return 0;
                    }
                    pending_sector = id[3];
                } else if ((mark == 0xf8 || mark == 0xfb) && pending_sector >= 0) {
                    uint8_t data[EPS_SECTOR_SIZE + 3];
                    if (!decode_bytes(stream, stream_size, field, data, sizeof(data))) {
                        free(stream);
                        fail(error, error_size, "truncated data field at track %u side %u sector %d",
                             track_index, side, pending_sector);
                        return 0;
                    }
                    uint16_t actual = crc16((const uint8_t *)"\xa1\xa1\xa1", 3, 0xffff);
                    actual = crc16(data, EPS_SECTOR_SIZE + 1, actual);
                    if (actual != be16(data + EPS_SECTOR_SIZE + 1)) {
                        if (tolerate_physical_errors) {
                            const size_t block =
                                ((track_index * EPS_SIDES + side) * EPS_SECTORS) +
                                (unsigned int)pending_sector;
                            if (sector_status[block] != EPS16_SECTOR_READABLE)
                                sector_status[block] = EPS16_SECTOR_CRC_ERROR;
                            pending_sector = -1;
                            continue;
                        }
                        free(stream);
                        fail(error, error_size, "bad data CRC at track %u side %u sector %d",
                             track_index, side, pending_sector);
                        return 0;
                    }
                    size_t block = ((track_index * EPS_SIDES + side) * EPS_SECTORS) +
                                   (unsigned int)pending_sector;
                    if (!(seen & (1u << pending_sector))) {
                        memcpy(logical + block * EPS_SECTOR_SIZE, data + 1,
                               EPS_SECTOR_SIZE);
                        seen |= 1u << pending_sector;
                        if (sector_status)
                            sector_status[block] = EPS16_SECTOR_READABLE;
                        ++total;
                    }
                    pending_sector = -1;
                }
            }
            if (!tolerate_physical_errors && seen != 0x03ff) {
                free(stream);
                fail(error, error_size, "track %u side %u has %u of 10 sectors",
                     track_index, side, total);
                return 0;
            }
        }
        free(stream);
    }
    if ((!tolerate_physical_errors &&
         total != EPS_TRACKS * EPS_SIDES * EPS_SECTORS) || !total) {
        fail(error, error_size, "decoded %u sectors, expected 1600", total);
        return 0;
    }
    return 1;
}

static int disk_load(const char *path, uint8_t *logical, size_t logical_size,
                     uint8_t *sector_status, int tolerate_physical_errors,
                     Eps16DiskFormat *format, char *error, size_t error_size) {
    FILE *input = fopen(path, "rb");
    if (!input) {
        fail(error, error_size, "cannot open disk image: %s", path);
        return 0;
    }
    if (fseek(input, 0, SEEK_END) || ftell(input) < 0) {
        fclose(input);
        fail(error, error_size, "cannot determine disk image size: %s", path);
        return 0;
    }
    long length = ftell(input);
    rewind(input);
    uint8_t *image = malloc(length ? (size_t)length : 1);
    if (!image || fread(image, 1, (size_t)length, input) != (size_t)length) {
        free(image);
        fclose(input);
        fail(error, error_size, "cannot read complete disk image: %s", path);
        return 0;
    }
    fclose(input);

    int ok;
    if (length >= 8 && !memcmp(image, "HXCPICFE", 8)) {
        uint8_t *decoded = malloc(logical_size);
        if (!decoded) {
            fail(error, error_size, "out of memory allocating validated HFE disk");
            ok = 0;
        } else {
            ok = decode_hfe(image, (size_t)length, decoded, logical_size,
                            sector_status, tolerate_physical_errors,
                            error, error_size);
            if (ok) {
                memcpy(logical, decoded, logical_size);
                if (format) *format = EPS16_DISK_HFE;
            }
            free(decoded);
        }
    } else if (length >= 11 && !memcmp(image, "\r\nEps File:", 11)) {
        ok = create_from_efe(image, (size_t)length, logical, logical_size,
                             error, error_size);
        if (ok) {
            if (sector_status)
                memset(sector_status, EPS16_SECTOR_READABLE,
                       EPS16_LOGICAL_SECTOR_COUNT);
            if (format) *format = EPS16_DISK_EFE;
        }
    } else if ((size_t)length == logical_size) {
        memcpy(logical, image, logical_size);
        if (sector_status)
            memset(sector_status, EPS16_SECTOR_READABLE,
                   EPS16_LOGICAL_SECTOR_COUNT);
        ok = 1;
        if (format) *format = EPS16_DISK_IMG;
    } else {
        fail(error, error_size,
             "expected an EFE, HFE v1, or exactly %zu IMG bytes; got %ld",
             logical_size, length);
        ok = 0;
    }
    free(image);
    return ok;
}

int eps16_disk_load(const char *path, uint8_t *logical, size_t logical_size,
                    Eps16DiskFormat *format, char *error, size_t error_size) {
    return disk_load(path, logical, logical_size, NULL, 0, format, error,
                     error_size);
}

int eps16_disk_load_physical(const char *path, uint8_t *logical,
                             size_t logical_size, uint8_t *sector_status,
                             size_t sector_status_size,
                             Eps16DiskFormat *format, char *error,
                             size_t error_size) {
    if (!sector_status ||
        sector_status_size != EPS16_LOGICAL_SECTOR_COUNT) {
        fail(error, error_size, "sector status buffer must be %u bytes",
             EPS16_LOGICAL_SECTOR_COUNT);
        return 0;
    }
    return disk_load(path, logical, logical_size, sector_status, 1, format,
                     error, error_size);
}

int eps16_disk_save(const char *path, const uint8_t *logical,
                    size_t logical_size, Eps16DiskFormat format,
                    char *error, size_t error_size) {
    if (!path || !*path || !logical ||
        logical_size != EPS16_LOGICAL_DISK_SIZE) {
        fail(error, error_size, "invalid disk output request");
        return 0;
    }
    if (format == EPS16_DISK_IMG)
        return save_atomic(path, logical, logical_size, error, error_size);
    if (format != EPS16_DISK_HFE) {
        fail(error, error_size, "disk output format must be IMG or HFE");
        return 0;
    }
    size_t encoded_size = 0;
    uint8_t *encoded = encode_hfe(logical, logical_size, &encoded_size,
                                  error, error_size);
    if (!encoded) return 0;
    const int ok = save_atomic(path, encoded, encoded_size, error, error_size);
    free(encoded);
    return ok;
}
