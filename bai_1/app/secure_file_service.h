#ifndef SECURE_AES_FILE_SERVICE_H
#define SECURE_AES_FILE_SERVICE_H

#include <stddef.h>
#include <time.h>

#include "ioctl_defs.h"

#define SECURE_FILE_ERROR_MAX 512
#define SECURE_STORAGE_PATH_MAX 4096
#define SECURE_STORAGE_NAME_MAX 256

struct secure_storage_entry {
    char name[SECURE_STORAGE_NAME_MAX];
    size_t encrypted_size;
    time_t modified_time;
};

const char *secure_file_describe_error(int error_code);

int secure_storage_resolve_directory(const char *requested_path,
                                     char *resolved_path,
                                     size_t resolved_size,
                                     char *error_buffer,
                                     size_t error_buffer_size);

int secure_storage_list_files(const char *storage_dir,
                              struct secure_storage_entry **entries,
                              size_t *entry_count,
                              char *error_buffer,
                              size_t error_buffer_size);

void secure_storage_free_entries(struct secure_storage_entry *entries);

int secure_storage_save_file(const char *storage_dir,
                             const char *device_path,
                             const char *file_name,
                             const char *key_hex,
                             const unsigned char *plain_data,
                             size_t plain_len,
                             int overwrite_existing,
                             size_t *encrypted_size,
                             char *error_buffer,
                             size_t error_buffer_size);

int secure_storage_read_file(const char *storage_dir,
                             const char *device_path,
                             const char *file_name,
                             const char *key_hex,
                             unsigned char **plain_data,
                             size_t *plain_len,
                             char *error_buffer,
                             size_t error_buffer_size);

void secure_storage_free_buffer(unsigned char *data, size_t length);

int secure_storage_delete_file(const char *storage_dir,
                               const char *file_name,
                               char *error_buffer,
                               size_t error_buffer_size);

#endif
