/*
 * mock_fat12.c — Simple in-memory filesystem mock for testing.
 *
 * Provides the FAT12 API from vendor/64korppu/include/fat12.h backed
 * by an array of mock files so that cbm_dos_open() and friends can
 * find test data without a real floppy disk.
 *
 * Only the operations needed for IEC integration tests are fully
 * implemented.  Write/format/delete/rename are stubs that return
 * success or appropriate error codes.
 */

#include "mock_hardware.h"
#include "fat12.h"
#include <string.h>

/* ---- Mock file storage ---- */

#define MAX_MOCK_FILES    8
#define MAX_FILE_SIZE     65536

typedef struct {
    bool     used;
    char     name[8];
    char     ext[3];
    uint8_t  data[MAX_FILE_SIZE];
    uint32_t size;
} mock_file_t;

static mock_file_t mock_files[MAX_MOCK_FILES];

/* ---- Test helpers ---- */

void mock_fat12_add_file(const char *name8, const char *ext3,
                         const uint8_t *data, uint32_t size) {
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (!mock_files[i].used) {
            mock_files[i].used = true;
            memcpy(mock_files[i].name, name8, 8);
            memcpy(mock_files[i].ext, ext3, 3);
            if (size > MAX_FILE_SIZE) size = MAX_FILE_SIZE;
            memcpy(mock_files[i].data, data, size);
            mock_files[i].size = size;
            return;
        }
    }
}

void mock_fat12_reset(void) {
    memset(mock_files, 0, sizeof(mock_files));
}

/* ---- FAT12 API implementation ---- */

static bool mounted = false;

int fat12_mount(void) {
    mounted = true;
    return FAT12_OK;
}

void fat12_unmount(void) {
    mounted = false;
}

uint16_t fat12_read_fat_entry(uint16_t cluster) {
    (void)cluster;
    return FAT12_EOC;
}

void fat12_write_fat_entry(uint16_t cluster, uint16_t value) {
    (void)cluster;
    (void)value;
}

int fat12_flush_fat(void) {
    return FAT12_OK;
}

/* Find a mock file by 8.3 name */
static mock_file_t *find_mock_file(const char *name, const char *ext) {
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (mock_files[i].used &&
            memcmp(mock_files[i].name, name, 8) == 0 &&
            memcmp(mock_files[i].ext, ext, 3) == 0) {
            return &mock_files[i];
        }
    }
    return NULL;
}

int fat12_find_file(const char *name, const char *ext, fat12_dirent_t *entry) {
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    mock_file_t *f = find_mock_file(name, ext);
    if (!f) return FAT12_ERR_NOT_FOUND;

    if (entry) {
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->name, f->name, 8);
        memcpy(entry->ext, f->ext, 3);
        entry->attr = FAT12_ATTR_ARCHIVE;
        entry->cluster_lo = 2;          /* Dummy cluster */
        entry->file_size = f->size;
    }
    return FAT12_OK;
}

int fat12_open_read(const char *name, const char *ext, fat12_file_t *file) {
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    mock_file_t *f = find_mock_file(name, ext);
    if (!f) return FAT12_ERR_NOT_FOUND;

    memset(file, 0, sizeof(*file));
    file->active = true;
    file->first_cluster = 2;
    file->current_cluster = 2;
    file->file_size = f->size;
    file->position = 0;
    file->write_mode = false;

    return FAT12_OK;
}

int fat12_read(fat12_file_t *file, uint8_t *buf, uint16_t count) {
    if (!file->active || file->write_mode) return FAT12_ERR_INVALID;
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    /*
     * To read data we need to find the mock file.  The mock uses
     * cluster_lo == index+2 convention but the simplest approach is
     * to scan by matching the cluster value set in fat12_open_read.
     * Since we only support one open file at a time in the mock,
     * we find the file by scanning for the one whose size matches.
     *
     * A cleaner approach: stash the mock_file_t pointer.  But we
     * want to keep the fat12_file_t struct untouched.  Instead we
     * store the file index in dir_entry_offset (unused in mock).
     */

    /* Search all mock files for a matching size (set during open) */
    mock_file_t *f = NULL;
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (mock_files[i].used && mock_files[i].size == file->file_size) {
            f = &mock_files[i];
            break;
        }
    }
    if (!f) return 0;

    uint16_t bytes_read = 0;
    while (bytes_read < count && file->position < file->file_size) {
        uint32_t remaining = file->file_size - file->position;
        uint16_t to_copy = count - bytes_read;
        if (to_copy > remaining) to_copy = (uint16_t)remaining;

        memcpy(buf + bytes_read, f->data + file->position, to_copy);
        bytes_read += to_copy;
        file->position += to_copy;
    }

    return (int)bytes_read;
}

int fat12_create(const char *name, const char *ext, fat12_file_t *file) {
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    /* Find or allocate a slot */
    mock_file_t *f = find_mock_file(name, ext);
    if (!f) {
        for (int i = 0; i < MAX_MOCK_FILES; i++) {
            if (!mock_files[i].used) {
                f = &mock_files[i];
                break;
            }
        }
    }
    if (!f) return FAT12_ERR_DIR_FULL;

    f->used = true;
    memcpy(f->name, name, 8);
    memcpy(f->ext, ext, 3);
    f->size = 0;
    memset(f->data, 0, MAX_FILE_SIZE);

    memset(file, 0, sizeof(*file));
    file->active = true;
    file->first_cluster = 2;
    file->current_cluster = 2;
    file->file_size = 0;
    file->position = 0;
    file->write_mode = true;
    file->modified = false;

    return FAT12_OK;
}

int fat12_write(fat12_file_t *file, const uint8_t *buf, uint16_t count) {
    if (!file->active || !file->write_mode) return FAT12_ERR_INVALID;
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    /* Find the mock file by scanning for matching write-mode entry */
    mock_file_t *f = NULL;
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (mock_files[i].used && mock_files[i].size == file->file_size) {
            f = &mock_files[i];
            break;
        }
    }
    if (!f) return FAT12_ERR_IO;

    uint16_t bytes_written = 0;
    while (bytes_written < count && file->position < MAX_FILE_SIZE) {
        f->data[file->position] = buf[bytes_written];
        file->position++;
        bytes_written++;
    }
    f->size = file->position;
    file->file_size = file->position;
    file->modified = true;

    return (int)bytes_written;
}

int fat12_close(fat12_file_t *file) {
    if (!file->active) return FAT12_OK;
    file->active = false;
    return FAT12_OK;
}

int fat12_delete(const char *name, const char *ext) {
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    mock_file_t *f = find_mock_file(name, ext);
    if (!f) return FAT12_ERR_NOT_FOUND;

    f->used = false;
    return FAT12_OK;
}

int fat12_rename(const char *old_name, const char *old_ext,
                  const char *new_name, const char *new_ext) {
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    /* Check new name doesn't exist */
    if (find_mock_file(new_name, new_ext)) return FAT12_ERR_EXISTS;

    mock_file_t *f = find_mock_file(old_name, old_ext);
    if (!f) return FAT12_ERR_NOT_FOUND;

    memcpy(f->name, new_name, 8);
    memcpy(f->ext, new_ext, 3);
    return FAT12_OK;
}

int fat12_readdir(uint16_t *index, fat12_dirent_t *entry) {
    if (!mounted) return FAT12_ERR_NOT_MOUNT;

    while (*index < MAX_MOCK_FILES) {
        uint16_t i = *index;
        (*index)++;

        if (mock_files[i].used) {
            memset(entry, 0, sizeof(*entry));
            memcpy(entry->name, mock_files[i].name, 8);
            memcpy(entry->ext, mock_files[i].ext, 3);
            entry->attr = FAT12_ATTR_ARCHIVE;
            entry->cluster_lo = (uint16_t)(i + 2);
            entry->file_size = mock_files[i].size;
            return FAT12_OK;
        }
    }
    return FAT12_ERR_NOT_FOUND;
}

uint32_t fat12_free_space(void) {
    /* Report plenty of space */
    return (uint32_t)(2880 - 33) * 512;
}

int fat12_format(const char *label) {
    (void)label;
    mock_fat12_reset();
    mounted = true;
    return FAT12_OK;
}

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
        char c = cbm_name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        name8[i] = c;
    }

    /* Copy extension */
    if (dot) {
        int elen = (int)strlen(dot + 1);
        if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++) {
            char c = dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 32;
            ext3[i] = c;
        }
    } else {
        /* Default extension: PRG */
        memcpy(ext3, "PRG", 3);
    }
}
