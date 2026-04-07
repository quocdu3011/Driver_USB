#ifndef SECURE_AES_FILE_MANAGER_H
#define SECURE_AES_FILE_MANAGER_H

#include <stddef.h>
#include <time.h>

#define FILE_MANAGER_PATH_MAX 4096
#define FILE_MANAGER_NAME_MAX 256
#define FILE_MANAGER_TYPE_MAX 32

struct file_manager_entry {
    char name[FILE_MANAGER_NAME_MAX];
    char path[FILE_MANAGER_PATH_MAX];
    char type_label[FILE_MANAGER_TYPE_MAX];
    unsigned long long size_bytes;
    time_t modified_time;
    int is_directory;
};

int file_manager_normalize_directory(const char *input,
                                     char *output,
                                     size_t output_size,
                                     char *error_buffer,
                                     size_t error_buffer_size);
int file_manager_list_directory(const char *directory_path,
                                struct file_manager_entry **entries,
                                size_t *entry_count,
                                char *error_buffer,
                                size_t error_buffer_size);
void file_manager_free_entries(struct file_manager_entry *entries);
int file_manager_create_directory(const char *parent_directory,
                                  const char *folder_name,
                                  char *error_buffer,
                                  size_t error_buffer_size);
int file_manager_delete_path(const char *path,
                             char *error_buffer,
                             size_t error_buffer_size);
int file_manager_get_parent_directory(const char *path,
                                      char *output,
                                      size_t output_size,
                                      char *error_buffer,
                                      size_t error_buffer_size);
int file_manager_join_path(const char *base_path,
                           const char *name,
                           char *output,
                           size_t output_size);
int file_manager_is_regular_file(const char *path);
int file_manager_is_directory(const char *path);

#endif