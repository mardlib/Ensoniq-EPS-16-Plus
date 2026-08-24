#ifndef EPS16_HFE_DISK_H
#define EPS16_HFE_DISK_H

#include <stddef.h>
#include <stdint.h>

enum {
    EPS16_LOGICAL_SECTOR_COUNT = 80 * 2 * 10,
    EPS16_LOGICAL_DISK_SIZE = EPS16_LOGICAL_SECTOR_COUNT * 512,
    EPS16_SECTOR_READABLE = 0x00,
    EPS16_SECTOR_CRC_ERROR = 0x08,
    EPS16_SECTOR_NOT_FOUND = 0x10
};

typedef enum {
    EPS16_DISK_IMG,
    EPS16_DISK_HFE,
    EPS16_DISK_EFE
} Eps16DiskFormat;

int eps16_disk_create_blank(uint8_t *logical, size_t logical_size);
int eps16_disk_load(const char *path, uint8_t *logical, size_t logical_size,
                    Eps16DiskFormat *format, char *error, size_t error_size);
int eps16_disk_load_physical(const char *path, uint8_t *logical,
                             size_t logical_size, uint8_t *sector_status,
                             size_t sector_status_size,
                             Eps16DiskFormat *format, char *error,
                             size_t error_size);
int eps16_disk_save(const char *path, const uint8_t *logical,
                    size_t logical_size, Eps16DiskFormat format,
                    char *error, size_t error_size);

#endif
