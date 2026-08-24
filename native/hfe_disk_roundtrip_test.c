#include "hfe_disk.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    HFE_HEADER_SIZE = 1024,
    HFE_TRACK_LENGTH = 49 * 512,
    HFE_SIDE_LENGTH = 49 * 256
};

static unsigned int le16(const uint8_t *value) {
    return value[0] | ((unsigned int)value[1] << 8);
}

static unsigned int be24(const uint8_t *value) {
    return ((unsigned int)value[0] << 16) |
           ((unsigned int)value[1] << 8) | value[2];
}

static unsigned int be32(const uint8_t *value) {
    return ((unsigned int)value[0] << 24) |
           ((unsigned int)value[1] << 16) |
           ((unsigned int)value[2] << 8) | value[3];
}

static unsigned int fat_value(const uint8_t *disk, unsigned int block) {
    const uint8_t *entry = disk + (5U + block / 170U) * 512U +
                           (block % 170U) * 3U;
    return be24(entry);
}

static uint8_t reverse_bits(uint8_t value) {
    value = (uint8_t)(((value & 0x55) << 1) | ((value >> 1) & 0x55));
    value = (uint8_t)(((value & 0x33) << 2) | ((value >> 2) & 0x33));
    return (uint8_t)((value << 4) | (value >> 4));
}

static uint8_t decode_word(const uint8_t *encoded) {
    const unsigned int word = ((unsigned int)encoded[0] << 8) | encoded[1];
    uint8_t value = 0;
    for (int bit = 14; bit >= 0; bit -= 2)
        value = (uint8_t)((value << 1) | ((word >> bit) & 1));
    return value;
}

static int verify_hardware_track(FILE *input, unsigned int track) {
    uint8_t interleaved[HFE_TRACK_LENGTH];
    uint8_t side_stream[HFE_SIDE_LENGTH];
    if (fseek(input, HFE_HEADER_SIZE + (long)track * HFE_TRACK_LENGTH,
              SEEK_SET) ||
        fread(interleaved, 1, sizeof(interleaved), input) !=
            sizeof(interleaved))
        return 0;

    for (unsigned int side = 0; side < 2; ++side) {
        size_t output = 0;
        for (size_t block = 0; block < 49; ++block)
            for (size_t index = 0; index < 256; ++index)
                side_stream[output++] = reverse_bits(
                    interleaved[block * 512 + side * 256 + index]);

        unsigned int sectors[10];
        unsigned int count = 0;
        for (size_t position = 0;
             position + 16 <= sizeof(side_stream) && count < 10;
             ++position) {
            if (side_stream[position] != 0x44 ||
                side_stream[position + 1] != 0x89 ||
                side_stream[position + 2] != 0x44 ||
                side_stream[position + 3] != 0x89 ||
                side_stream[position + 4] != 0x44 ||
                side_stream[position + 5] != 0x89 ||
                decode_word(side_stream + position + 6) != 0xfe)
                continue;
            sectors[count++] = decode_word(side_stream + position + 12);
            position += 15;
        }
        if (count != 10) return 0;
        const unsigned int first = (track * 6U + side * 8U) % 10U;
        for (unsigned int index = 0; index < 10; ++index)
            if (sectors[index] != (first + index) % 10U) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    uint8_t source[EPS16_LOGICAL_DISK_SIZE];
    uint8_t decoded[EPS16_LOGICAL_DISK_SIZE];
    if (!eps16_disk_create_blank(decoded, sizeof(decoded)) ||
        memcmp(decoded + 512 + 38, "ID", 2) ||
        memcmp(decoded + 2 * 512 + 28, "OS", 2) ||
        memcmp(decoded + 5 * 512 - 2, "DR", 2) ||
        memcmp(decoded + 6 * 512 - 2, "FB", 2) ||
        decoded[2 * 512 + 2] != 0x06 || decoded[2 * 512 + 3] != 0x31) {
        fputs("blank EPS disk structure mismatch\n", stderr);
        return 1;
    }
    for (size_t index = 0; index < sizeof(source); ++index)
        source[index] = (uint8_t)((index * 37U + index / 512U) & 0xffU);

    char path[128];
    snprintf(path, sizeof(path), "/tmp/eps16-hfe-roundtrip-%ld.hfe",
             (long)getpid());
    char error[256] = {0};
    if (!eps16_disk_save(path, source, sizeof(source), EPS16_DISK_HFE,
                         error, sizeof(error))) {
        fprintf(stderr, "HFE save failed: %s\n", error);
        return 1;
    }
    FILE *encoded = fopen(path, "rb");
    uint8_t header[HFE_HEADER_SIZE];
    if (!encoded || fread(header, 1, sizeof(header), encoded) != sizeof(header) ||
        le16(header + 512 + 2) != HFE_TRACK_LENGTH ||
        le16(header + 512 + 4) != 51 ||
        le16(header + 512 + 6) != HFE_TRACK_LENGTH ||
        !verify_hardware_track(encoded, 0) ||
        !verify_hardware_track(encoded, 1)) {
        if (encoded) fclose(encoded);
        remove(path);
        fputs("generated HFE hardware track layout mismatch\n", stderr);
        return 1;
    }
    fclose(encoded);
    Eps16DiskFormat format;
    const int loaded = eps16_disk_load(path, decoded, sizeof(decoded), &format,
                                       error, sizeof(error));
    if (!loaded || format != EPS16_DISK_HFE) {
        remove(path);
        fprintf(stderr, "generated HFE load failed: %s\n", error);
        return 1;
    }
    if (memcmp(source, decoded, sizeof(source))) {
        fputs("IMG -> HFE -> IMG roundtrip mismatch\n", stderr);
        remove(path);
        return 1;
    }

    /* Real HxC/Gotek captures can contain unused bad or unformatted sectors.
       A physical drive does not reject the whole disk up front: the WD1772
       reports the fault only if the OS requests that sector. Corrupt one data
       bit in the first ID CRC and verify both APIs: archival extraction stays
       strict, while the emulator import retains a per-sector CRC error. */
    FILE *damaged = fopen(path, "r+b");
    const long first_id_crc_low = HFE_HEADER_SIZE + 203;
    uint8_t encoded_crc_byte = 0;
    if (!damaged || fseek(damaged, first_id_crc_low, SEEK_SET) ||
        fread(&encoded_crc_byte, 1, 1, damaged) != 1 ||
        fseek(damaged, first_id_crc_low, SEEK_SET)) {
        if (damaged) fclose(damaged);
        remove(path);
        fputs("cannot prepare damaged HFE test image\n", stderr);
        return 1;
    }
    encoded_crc_byte ^= 0x80; /* bit-reversed HFE storage: decoded bit zero */
    const int damaged_write_ok =
        fwrite(&encoded_crc_byte, 1, 1, damaged) == 1;
    const int damaged_close_ok = fclose(damaged) == 0;
    if (!damaged_write_ok || !damaged_close_ok) {
        remove(path);
        fputs("cannot write damaged HFE test image\n", stderr);
        return 1;
    }
    if (eps16_disk_load(path, decoded, sizeof(decoded), &format,
                        error, sizeof(error))) {
        remove(path);
        fputs("strict HFE extraction accepted a bad ID CRC\n", stderr);
        return 1;
    }
    uint8_t sector_status[EPS16_LOGICAL_SECTOR_COUNT];
    if (!eps16_disk_load_physical(path, decoded, sizeof(decoded),
                                  sector_status, sizeof(sector_status),
                                  &format, error, sizeof(error)) ||
        sector_status[0] != EPS16_SECTOR_CRC_ERROR) {
        remove(path);
        fprintf(stderr, "physical HFE import failed: %s\n", error);
        return 1;
    }
    for (size_t block = 1; block < EPS16_LOGICAL_SECTOR_COUNT; ++block) {
        if (sector_status[block] != EPS16_SECTOR_READABLE) {
            remove(path);
            fputs("physical HFE import marked an intact sector bad\n", stderr);
            return 1;
        }
    }
    remove(path);

    /* EFE is a 512-byte exchange header followed by the native file blocks.
       Import must create an ordinary EPS directory/FAT image so the original
       OS remains solely responsible for loading the instrument. */
    uint8_t efe[3 * 512] = {0};
    memcpy(efe, "\r\nEps File:", 11);
    memcpy(efe + 0x12, "TEST EFE    ", 12);
    memcpy(efe + 0x22, "Instrument   ", 13);
    efe[0x2f] = '\r';
    efe[0x30] = '\n';
    efe[0x31] = 0x1a;
    efe[0x32] = 0x03;
    efe[0x33] = 0x03;
    efe[0x35] = 2;
    efe[0x37] = 2;
    for (size_t index = 512; index < sizeof(efe); ++index)
        efe[index] = (uint8_t)(index * 29U + 7U);
    snprintf(path, sizeof(path), "/tmp/eps16-efe-import-%ld.efe",
             (long)getpid());
    FILE *efe_file = fopen(path, "wb");
    const int efe_write_ok = efe_file &&
        fwrite(efe, 1, sizeof(efe), efe_file) == sizeof(efe);
    const int efe_close_ok = efe_file && fclose(efe_file) == 0;
    if (!efe_write_ok || !efe_close_ok) {
        remove(path);
        fputs("cannot create synthetic EFE fixture\n", stderr);
        return 1;
    }
    if (!eps16_disk_load(path, decoded, sizeof(decoded), &format,
                         error, sizeof(error))) {
        remove(path);
        fprintf(stderr, "synthetic EFE import failed: %s\n", error);
        return 1;
    }
    remove(path);
    const uint8_t *directory_entry = decoded + 3 * 512 + 25;
    if (format != EPS16_DISK_EFE ||
        memcmp(decoded + 512 + 31, "EFELOAD", 7) ||
        be32(decoded + 2 * 512) != 1583 ||
        directory_entry[2] != 3 ||
        memcmp(directory_entry + 3, "TEST EFE    ", 12) ||
        be32(directory_entry + 19) != 15 ||
        memcmp(decoded + 15 * 512, efe + 512, 2 * 512) ||
        fat_value(decoded, 15) != 16 || fat_value(decoded, 16) != 1) {
        fputs("synthetic EFE directory/FAT import mismatch\n", stderr);
        return 1;
    }

    if (argc == 2) {
        if (!eps16_disk_load(argv[1], decoded, sizeof(decoded), &format,
                             error, sizeof(error)) ||
            format != EPS16_DISK_EFE) {
            fprintf(stderr, "supplied EFE import failed: %s\n", error);
            return 1;
        }
        printf("supplied EFE imported: %.12s, %u blocks, %u free\n",
               decoded + 3 * 512 + 28,
               be32(decoded + 3 * 512 + 25 + 19) == 15
                   ? (unsigned int)(1585 - be32(decoded + 2 * 512)) : 0,
               be32(decoded + 2 * 512));
    }
    puts("IMG -> HFE -> IMG roundtrip: 1600 sectors, 819200 bytes");
    puts("damaged HFE import: bad sector deferred to WD1772 access");
    puts("EFE import: native directory, FAT chain and payload verified");
    return 0;
}
