#include "fat12.h"
#include "config.h"
#include <string.h>

#ifdef __AVR__
#include <ctype.h>
#else
#include <ctype.h>
#endif

/*
 * FAT12 filesystem implementation for Arduino Nano.
 *
 * FAT cache stored in external SRAM at SRAM_FAT_CACHE.
 * Disk I/O via external disk_read_sector / disk_write_sector.
 * SRAM access via external sram_read / sram_write.
 */

/* External dependencies (provided by platform or test mock) */
extern int disk_read_sector(uint16_t lba, uint8_t *buf);
extern int disk_write_sector(uint16_t lba, const uint8_t *buf);
extern void sram_read(uint32_t addr, uint8_t *buf, uint16_t len);
extern void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len);

/* Internal state (fits in Nano's 2KB RAM) */
static struct {
    bool     mounted;
    bool     fat_dirty;
    uint32_t volume_serial;
    char     volume_label[12];
} state;

/* Shared sector buffer (512 bytes in internal RAM) */
static uint8_t sector_buf[512];

/* ---- FAT entry access (reads/writes SRAM) ---- */

uint16_t fat12_read_fat_entry(uint16_t cluster) {
    if (!state.mounted) return 0;

    uint32_t offset = cluster + (cluster / 2);  /* 1.5 bytes per entry */
    uint8_t buf[2];
    sram_read(SRAM_FAT_CACHE + offset, buf, 2);

    uint16_t raw = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return (cluster & 1) ? (raw >> 4) : (raw & 0x0FFF);
}

void fat12_write_fat_entry(uint16_t cluster, uint16_t value) {
    if (!state.mounted) return;

    uint32_t offset = cluster + (cluster / 2);
    uint8_t buf[2];
    sram_read(SRAM_FAT_CACHE + offset, buf, 2);

    uint16_t raw = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

    if (cluster & 1) {
        raw = (raw & 0x000F) | ((value & 0x0FFF) << 4);
    } else {
        raw = (raw & 0xF000) | (value & 0x0FFF);
    }

    buf[0] = raw & 0xFF;
    buf[1] = (raw >> 8) & 0xFF;
    sram_write(SRAM_FAT_CACHE + offset, buf, 2);

    state.fat_dirty = true;
}

/* Find a free cluster in the FAT */
static int16_t fat12_find_free_cluster(void) {
    for (uint16_t c = 2; c < FAT12_TOTAL_CLUSTERS + 2; c++) {
        if (fat12_read_fat_entry(c) == FAT12_FREE) {
            return (int16_t)c;
        }
    }
    return -1;
}

/* ---- Disk I/O helpers ---- */

int fat12_flush_fat(void) {
    if (!state.mounted || !state.fat_dirty) return FAT12_OK;

    /* Write FAT from SRAM to disk, both copies */
    for (uint8_t i = 0; i < FAT12_SECTORS_PER_FAT; i++) {
        sram_read(SRAM_FAT_CACHE + (uint32_t)i * 512, sector_buf, 512);
        if (disk_write_sector(FAT12_FAT1_START + i, sector_buf) != 0)
            return FAT12_ERR_IO;
        if (disk_write_sector(FAT12_FAT2_START + i, sector_buf) != 0)
            return FAT12_ERR_IO;
    }

    state.fat_dirty = false;
    return FAT12_OK;
}

/* ---- Mount / Unmount ---- */

int fat12_mount(void) {
    if (state.mounted) return FAT12_OK;

    /* Read boot sector */
    if (disk_read_sector(0, sector_buf) != 0) return FAT12_ERR_IO;

    /* Validate BPB */
    fat12_bpb_t *bpb = (fat12_bpb_t *)sector_buf;
    if (bpb->bytes_per_sector != 512) return FAT12_ERR_NOT_FAT12;
    if (bpb->num_fats != 2) return FAT12_ERR_NOT_FAT12;
    if (bpb->sectors_per_cluster == 0) return FAT12_ERR_NOT_FAT12;

    /* Store volume info */
    if (bpb->boot_sig == 0x29) {
        state.volume_serial = bpb->volume_serial;
        memcpy(state.volume_label, bpb->volume_label, 11);
        state.volume_label[11] = '\0';
    } else {
        state.volume_serial = 0;
        memcpy(state.volume_label, "NO NAME    ", 11);
        state.volume_label[11] = '\0';
    }

    /* Read FAT #1 into SRAM */
    for (uint8_t i = 0; i < FAT12_SECTORS_PER_FAT; i++) {
        if (disk_read_sector(FAT12_FAT1_START + i, sector_buf) != 0)
            return FAT12_ERR_IO;
        sram_write(SRAM_FAT_CACHE + (uint32_t)i * 512, sector_buf, 512);
    }

    state.mounted = true;
    state.fat_dirty = false;
    return FAT12_OK;
}

void fat12_unmount(void) {
    if (state.mounted) {
        fat12_flush_fat();
        state.mounted = false;
    }
}

/* ---- Directory operations ---- */

int fat12_find_file(const char *name, const char *ext, fat12_dirent_t *entry) {
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    for (uint8_t s = 0; s < FAT12_ROOT_DIR_SECTORS; s++) {
        if (disk_read_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
            return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)sector_buf;
        for (uint8_t i = 0; i < 16; i++) {
            if ((uint8_t)entries[i].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr & FAT12_ATTR_VOLUME_ID) continue;

            if (memcmp(entries[i].name, name, 8) == 0 &&
                memcmp(entries[i].ext, ext, 3) == 0) {
                if (entry) *entry = entries[i];
                return FAT12_OK;
            }
        }
    }
    return FAT12_ERR_NOT_FOUND;
}

int fat12_readdir(uint16_t *index, fat12_dirent_t *entry) {
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    while (*index < FAT12_ROOT_ENTRIES) {
        uint16_t sector = FAT12_ROOT_DIR_START + (*index / 16);
        uint16_t offset = (*index % 16) * 32;

        if (disk_read_sector(sector, sector_buf) != 0) return FAT12_ERR_IO;

        fat12_dirent_t *e = (fat12_dirent_t *)(sector_buf + offset);

        (*index)++;

        if ((uint8_t)e->name[0] == 0x00) return FAT12_ERR_NOT_FOUND;
        if ((uint8_t)e->name[0] == 0xE5) continue;
        if (e->attr == 0x0F) continue;  /* LFN */

        *entry = *e;
        return FAT12_OK;
    }
    return FAT12_ERR_NOT_FOUND;
}

/* ---- File read operations ---- */

int fat12_open_read(const char *name, const char *ext, fat12_file_t *file) {
    fat12_dirent_t entry;
    int rc = fat12_find_file(name, ext, &entry);
    if (rc != FAT12_OK) return rc;

    memset(file, 0, sizeof(*file));
    file->active = true;
    file->first_cluster = entry.cluster_lo;
    file->current_cluster = entry.cluster_lo;
    file->file_size = entry.file_size;
    file->position = 0;
    file->write_mode = false;
    return FAT12_OK;
}

int fat12_read(fat12_file_t *file, uint8_t *buf, uint16_t count) {
    if (!file->active || file->write_mode) return FAT12_ERR_INVALID;
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    uint16_t bytes_read = 0;

    while (bytes_read < count && file->position < file->file_size) {
        /* Calculate sector for current position */
        uint16_t offset_in_cluster = file->position % FAT12_BYTES_PER_SECTOR;
        uint16_t data_sector = FAT12_DATA_START + file->current_cluster - 2;

        if (disk_read_sector(data_sector, sector_buf) != 0)
            return bytes_read > 0 ? (int)bytes_read : FAT12_ERR_IO;

        /* Copy data from sector buffer */
        uint16_t remaining_in_sector = FAT12_BYTES_PER_SECTOR - offset_in_cluster;
        uint16_t remaining_in_file = (uint16_t)(file->file_size - file->position);
        uint16_t remaining_to_read = count - bytes_read;
        uint16_t to_copy = remaining_in_sector;
        if (to_copy > remaining_in_file) to_copy = remaining_in_file;
        if (to_copy > remaining_to_read) to_copy = remaining_to_read;

        memcpy(buf + bytes_read, sector_buf + offset_in_cluster, to_copy);
        bytes_read += to_copy;
        file->position += to_copy;

        /* Move to next cluster if we've finished this one */
        if (file->position % FAT12_BYTES_PER_SECTOR == 0 &&
            file->position < file->file_size) {
            uint16_t next = fat12_read_fat_entry(file->current_cluster);
            if (next >= FAT12_EOC_MIN) break;
            file->current_cluster = next;
        }
    }

    return (int)bytes_read;
}

/* ---- File write operations ---- */

int fat12_create(const char *name, const char *ext, fat12_file_t *file) {
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    /* Check if file already exists, delete it first */
    fat12_dirent_t existing;
    if (fat12_find_file(name, ext, &existing) == FAT12_OK) {
        fat12_delete(name, ext);
    }

    /* Find free directory entry */
    for (uint8_t s = 0; s < FAT12_ROOT_DIR_SECTORS; s++) {
        if (disk_read_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
            return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)sector_buf;
        for (uint8_t i = 0; i < 16; i++) {
            if ((uint8_t)entries[i].name[0] == 0x00 ||
                (uint8_t)entries[i].name[0] == 0xE5) {

                /* Allocate first cluster */
                int16_t cluster = fat12_find_free_cluster();
                if (cluster < 0) return FAT12_ERR_DISK_FULL;

                fat12_write_fat_entry(cluster, FAT12_EOC);

                /* Write directory entry */
                memset(&entries[i], 0, sizeof(fat12_dirent_t));
                memcpy(entries[i].name, name, 8);
                memcpy(entries[i].ext, ext, 3);
                entries[i].attr = FAT12_ATTR_ARCHIVE;
                entries[i].cluster_lo = cluster;
                entries[i].file_size = 0;

                if (disk_write_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
                    return FAT12_ERR_IO;

                /* Initialize file handle */
                memset(file, 0, sizeof(*file));
                file->active = true;
                file->first_cluster = cluster;
                file->current_cluster = cluster;
                file->file_size = 0;
                file->position = 0;
                file->write_mode = true;
                file->modified = false;
                file->dir_entry_sector = FAT12_ROOT_DIR_START + s;
                file->dir_entry_offset = i * 32;

                return FAT12_OK;
            }
        }
    }

    return FAT12_ERR_DIR_FULL;
}

int fat12_write(fat12_file_t *file, const uint8_t *buf, uint16_t count) {
    if (!file->active || !file->write_mode) return FAT12_ERR_INVALID;
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    uint16_t bytes_written = 0;

    while (bytes_written < count) {
        uint16_t offset_in_cluster = file->position % FAT12_BYTES_PER_SECTOR;

        /* If starting in middle of sector, read existing data */
        if (offset_in_cluster > 0) {
            uint16_t data_sector = FAT12_DATA_START + file->current_cluster - 2;
            disk_read_sector(data_sector, sector_buf);
        } else {
            memset(sector_buf, 0, 512);
        }

        uint16_t space_in_sector = FAT12_BYTES_PER_SECTOR - offset_in_cluster;
        uint16_t remaining = count - bytes_written;
        uint16_t to_write = (remaining < space_in_sector) ? remaining : space_in_sector;

        memcpy(sector_buf + offset_in_cluster, buf + bytes_written, to_write);

        /* Write sector to disk */
        uint16_t data_sector = FAT12_DATA_START + file->current_cluster - 2;
        if (disk_write_sector(data_sector, sector_buf) != 0)
            return bytes_written > 0 ? (int)bytes_written : FAT12_ERR_IO;

        bytes_written += to_write;
        file->position += to_write;
        file->modified = true;

        /* Need next cluster? */
        if (file->position % FAT12_BYTES_PER_SECTOR == 0 &&
            bytes_written < count) {
            int16_t next = fat12_find_free_cluster();
            if (next < 0) return (int)bytes_written;

            fat12_write_fat_entry(file->current_cluster, next);
            fat12_write_fat_entry(next, FAT12_EOC);
            file->current_cluster = next;
        }
    }

    return (int)bytes_written;
}

int fat12_close(fat12_file_t *file) {
    if (!file->active) return FAT12_OK;

    if (file->write_mode && file->modified) {
        /* Update directory entry with final file size */
        if (disk_read_sector(file->dir_entry_sector, sector_buf) != 0)
            return FAT12_ERR_IO;

        fat12_dirent_t *entry = (fat12_dirent_t *)(sector_buf + file->dir_entry_offset);
        entry->file_size = file->position;

        if (disk_write_sector(file->dir_entry_sector, sector_buf) != 0)
            return FAT12_ERR_IO;

        fat12_flush_fat();
    }

    file->active = false;
    return FAT12_OK;
}

/* ---- Delete ---- */

int fat12_delete(const char *name, const char *ext) {
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    for (uint8_t s = 0; s < FAT12_ROOT_DIR_SECTORS; s++) {
        if (disk_read_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
            return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)sector_buf;
        for (uint8_t i = 0; i < 16; i++) {
            if ((uint8_t)entries[i].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;

            if (memcmp(entries[i].name, name, 8) == 0 &&
                memcmp(entries[i].ext, ext, 3) == 0) {

                /* Free cluster chain */
                uint16_t cluster = entries[i].cluster_lo;
                while (cluster >= 2 && cluster < FAT12_EOC_MIN) {
                    uint16_t next = fat12_read_fat_entry(cluster);
                    fat12_write_fat_entry(cluster, FAT12_FREE);
                    cluster = next;
                }

                /* Mark directory entry as deleted */
                entries[i].name[0] = (char)0xE5;
                if (disk_write_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
                    return FAT12_ERR_IO;

                fat12_flush_fat();
                return FAT12_OK;
            }
        }
    }
    return FAT12_ERR_NOT_FOUND;
}

/* ---- Rename ---- */

int fat12_rename(const char *old_name, const char *old_ext,
                  const char *new_name, const char *new_ext) {
    if (!state.mounted) return FAT12_ERR_NOT_MOUNT;

    /* Check if new name already exists */
    fat12_dirent_t tmp;
    if (fat12_find_file(new_name, new_ext, &tmp) == FAT12_OK)
        return FAT12_ERR_EXISTS;

    /* Find old file */
    for (uint8_t s = 0; s < FAT12_ROOT_DIR_SECTORS; s++) {
        if (disk_read_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
            return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)sector_buf;
        for (uint8_t i = 0; i < 16; i++) {
            if ((uint8_t)entries[i].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;

            if (memcmp(entries[i].name, old_name, 8) == 0 &&
                memcmp(entries[i].ext, old_ext, 3) == 0) {
                memcpy(entries[i].name, new_name, 8);
                memcpy(entries[i].ext, new_ext, 3);
                return disk_write_sector(FAT12_ROOT_DIR_START + s, sector_buf) == 0
                    ? FAT12_OK : FAT12_ERR_IO;
            }
        }
    }
    return FAT12_ERR_NOT_FOUND;
}

/* ---- Free space ---- */

uint32_t fat12_free_space(void) {
    if (!state.mounted) return 0;

    uint16_t free_clusters = 0;
    for (uint16_t c = 2; c < FAT12_TOTAL_CLUSTERS + 2; c++) {
        if (fat12_read_fat_entry(c) == FAT12_FREE) {
            free_clusters++;
        }
    }
    return (uint32_t)free_clusters * FAT12_BYTES_PER_SECTOR;
}

/* ---- Format ---- */

int fat12_format(const char *label) {
    /* Write boot sector */
    memset(sector_buf, 0, 512);

    fat12_bpb_t *bpb = (fat12_bpb_t *)sector_buf;
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x3C;
    bpb->jmp[2] = 0x90;
    memcpy(bpb->oem_name, "64KORPPU", 8);
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 1;
    bpb->reserved_sectors = 1;
    bpb->num_fats = 2;
    bpb->root_entries = 224;
    bpb->total_sectors = 2880;
    bpb->media_type = FAT12_MEDIA_BYTE;
    bpb->sectors_per_fat = 9;
    bpb->sectors_per_track = 18;
    bpb->num_heads = 2;
    bpb->boot_sig = 0x29;
    bpb->volume_serial = 0x64C0FFEE;

    if (label) {
        memcpy(bpb->volume_label, label, 11);
    } else {
        memcpy(bpb->volume_label, "64KORPPU   ", 11);
    }
    memcpy(bpb->fs_type, "FAT12   ", 8);

    /* Boot signature */
    sector_buf[510] = 0x55;
    sector_buf[511] = 0xAA;

    if (disk_write_sector(0, sector_buf) != 0) return FAT12_ERR_IO;

    /* Write FAT #1 and #2 */
    for (uint8_t i = 0; i < FAT12_SECTORS_PER_FAT; i++) {
        memset(sector_buf, 0, 512);

        if (i == 0) {
            /* First FAT sector: media byte + 0xFF, 0xFF, 0xFF */
            sector_buf[0] = FAT12_MEDIA_BYTE;
            sector_buf[1] = 0xFF;
            sector_buf[2] = 0xFF;
        }

        if (disk_write_sector(FAT12_FAT1_START + i, sector_buf) != 0)
            return FAT12_ERR_IO;
        if (disk_write_sector(FAT12_FAT2_START + i, sector_buf) != 0)
            return FAT12_ERR_IO;

        /* Also write to SRAM FAT cache */
        sram_write(SRAM_FAT_CACHE + (uint32_t)i * 512, sector_buf, 512);
    }

    /* Write empty root directory */
    memset(sector_buf, 0, 512);

    /* First sector: volume label entry */
    if (label) {
        fat12_dirent_t *vol = (fat12_dirent_t *)sector_buf;
        memcpy(vol->name, label, 8);
        memcpy(vol->ext, label + 8, 3);
        vol->attr = FAT12_ATTR_VOLUME_ID;
    }

    if (disk_write_sector(FAT12_ROOT_DIR_START, sector_buf) != 0)
        return FAT12_ERR_IO;

    /* Clear remaining root directory sectors */
    memset(sector_buf, 0, 512);
    for (uint8_t s = 1; s < FAT12_ROOT_DIR_SECTORS; s++) {
        if (disk_write_sector(FAT12_ROOT_DIR_START + s, sector_buf) != 0)
            return FAT12_ERR_IO;
    }

    /* Mount the freshly formatted disk */
    state.mounted = false;
    return fat12_mount();
}

/* ---- Filename parsing ---- */

void fat12_parse_filename(const char *cbm_name, char *name8, char *ext3) {
    memset(name8, ' ', 8);
    memset(ext3, ' ', 3);

    /* Find dot separator */
    const char *dot = NULL;
    for (const char *p = cbm_name; *p; p++) {
        if (*p == '.') { dot = p; break; }
    }

    /* Copy name part */
    int len = dot ? (int)(dot - cbm_name) : (int)strlen(cbm_name);
    if (len > 8) len = 8;
    for (int i = 0; i < len; i++) {
        name8[i] = toupper((unsigned char)cbm_name[i]);
    }

    /* Copy extension */
    if (dot) {
        int elen = (int)strlen(dot + 1);
        if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++) {
            ext3[i] = toupper((unsigned char)dot[i + 1]);
        }
    } else {
        /* Default extension: PRG */
        memcpy(ext3, "PRG", 3);
    }
}
