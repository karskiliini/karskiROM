#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

/*
 * FAT12 filesystem for 1.44MB floppy disk.
 * Nano version: FAT cache stored in external SRAM.
 *
 * Disk layout:
 *   Sector 0:      Boot sector (BPB)
 *   Sectors 1-9:   FAT #1
 *   Sectors 10-18: FAT #2 (copy)
 *   Sectors 19-32: Root directory (14 sectors, 224 entries)
 *   Sectors 33+:   Data area
 */

/* 1.44MB disk parameters */
#define FAT12_BYTES_PER_SECTOR   512
#define FAT12_SECTORS_PER_CLUSTER  1
#define FAT12_RESERVED_SECTORS     1
#define FAT12_NUM_FATS             2
#define FAT12_ROOT_ENTRIES       224
#define FAT12_TOTAL_SECTORS     2880
#define FAT12_SECTORS_PER_FAT      9
#define FAT12_MEDIA_BYTE        0xF0

/* Derived constants */
#define FAT12_FAT1_START         1
#define FAT12_FAT2_START        (FAT12_FAT1_START + FAT12_SECTORS_PER_FAT)
#define FAT12_ROOT_DIR_START    (FAT12_FAT2_START + FAT12_SECTORS_PER_FAT)
#define FAT12_ROOT_DIR_SECTORS  14
#define FAT12_DATA_START        (FAT12_ROOT_DIR_START + FAT12_ROOT_DIR_SECTORS)
#define FAT12_TOTAL_CLUSTERS    ((FAT12_TOTAL_SECTORS - FAT12_DATA_START) / FAT12_SECTORS_PER_CLUSTER)

/* FAT entry values */
#define FAT12_FREE       0x000
#define FAT12_EOC_MIN    0xFF8
#define FAT12_BAD        0xFF7
#define FAT12_EOC        0xFFF

/* Directory entry attributes */
#define FAT12_ATTR_READ_ONLY  0x01
#define FAT12_ATTR_HIDDEN     0x02
#define FAT12_ATTR_SYSTEM     0x04
#define FAT12_ATTR_VOLUME_ID  0x08
#define FAT12_ATTR_DIRECTORY  0x10
#define FAT12_ATTR_ARCHIVE    0x20

/* Directory entry (32 bytes, packed) */
typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  ctime_tenths;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t file_size;
} fat12_dirent_t;

/* Boot sector BPB */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_serial;
    char     volume_label[11];
    char     fs_type[8];
} fat12_bpb_t;

/* File handle */
typedef struct {
    bool     active;
    uint16_t first_cluster;
    uint16_t current_cluster;
    uint32_t file_size;
    uint32_t position;
    uint16_t dir_entry_sector;
    uint16_t dir_entry_offset;
    bool     write_mode;
    bool     modified;
} fat12_file_t;

/* Error codes */
#define FAT12_OK              0
#define FAT12_ERR_IO         -1
#define FAT12_ERR_NOT_MOUNT  -2
#define FAT12_ERR_NOT_FOUND  -3
#define FAT12_ERR_EXISTS     -4
#define FAT12_ERR_DIR_FULL   -5
#define FAT12_ERR_DISK_FULL  -6
#define FAT12_ERR_INVALID    -7
#define FAT12_ERR_NOT_FAT12  -8

int fat12_mount(void);
void fat12_unmount(void);
uint16_t fat12_read_fat_entry(uint16_t cluster);
void fat12_write_fat_entry(uint16_t cluster, uint16_t value);
int fat12_flush_fat(void);
int fat12_find_file(const char *name, const char *ext, fat12_dirent_t *entry);
int fat12_open_read(const char *name, const char *ext, fat12_file_t *file);
int fat12_read(fat12_file_t *file, uint8_t *buf, uint16_t count);
int fat12_create(const char *name, const char *ext, fat12_file_t *file);
int fat12_write(fat12_file_t *file, const uint8_t *buf, uint16_t count);
int fat12_close(fat12_file_t *file);
int fat12_delete(const char *name, const char *ext);
int fat12_rename(const char *old_name, const char *old_ext,
                  const char *new_name, const char *new_ext);
int fat12_readdir(uint16_t *index, fat12_dirent_t *entry);
uint32_t fat12_free_space(void);
int fat12_format(const char *label);
void fat12_parse_filename(const char *cbm_name, char *name8, char *ext3);

#endif /* FAT12_H */
