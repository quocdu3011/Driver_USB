#include "file_manager.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static void file_manager_set_error(char *buffer,
                                   size_t buffer_size,
                                   const char *format,
                                   ...)
{
    va_list args;

    if (!buffer || buffer_size == 0)
        return;

    va_start(args, format);
    vsnprintf(buffer, buffer_size, format, args);
    va_end(args);
}

static int file_manager_copy_text(char *destination,
                                  size_t destination_size,
                                  const char *source)
{
    if (!destination || destination_size == 0 || !source)
        return EINVAL;

    if (snprintf(destination, destination_size, "%s", source) >= (int)destination_size)
        return ENAMETOOLONG;

    return 0;
}

static int file_manager_compare_entries(const void *left, const void *right)
{
    const struct file_manager_entry *lhs = (const struct file_manager_entry *)left;
    const struct file_manager_entry *rhs = (const struct file_manager_entry *)right;

    if (lhs->is_directory != rhs->is_directory)
        return rhs->is_directory - lhs->is_directory;

    return strcasecmp(lhs->name, rhs->name);
}

static const char *file_manager_detect_type_label(mode_t mode)
{
    if (S_ISDIR(mode))
        return "Directory";
    if (S_ISREG(mode))
        return "File";
    if (S_ISLNK(mode))
        return "Symlink";
    if (S_ISCHR(mode))
        return "Char device";
    if (S_ISBLK(mode))
        return "Block device";
    if (S_ISFIFO(mode))
        return "FIFO";
    if (S_ISSOCK(mode))
        return "Socket";
    return "Other";
}

int file_manager_join_path(const char *base_path,
                           const char *name,
                           char *output,
                           size_t output_size)
{
    int written;

    if (!base_path || !name || !output || output_size == 0)
        return EINVAL;

    if (strcmp(base_path, "/") == 0)
        written = snprintf(output, output_size, "/%s", name);
    else
        written = snprintf(output, output_size, "%s/%s", base_path, name);

    if (written < 0 || (size_t)written >= output_size)
        return ENAMETOOLONG;

    return 0;
}

int file_manager_normalize_directory(const char *input,
                                     char *output,
                                     size_t output_size,
                                     char *error_buffer,
                                     size_t error_buffer_size)
{
    char resolved[FILE_MANAGER_PATH_MAX];
    struct stat st;

    if (!input || !output || output_size == 0)
        return EINVAL;

    if (!realpath(input, resolved)) {
        int error_code = errno;
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Cannot open directory '%s': %s",
                               input, strerror(error_code));
        return error_code;
    }

    if (stat(resolved, &st) != 0) {
        int error_code = errno;
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Cannot stat '%s': %s",
                               resolved, strerror(error_code));
        return error_code;
    }

    if (!S_ISDIR(st.st_mode)) {
        file_manager_set_error(error_buffer, error_buffer_size,
                               "'%s' is not a directory", resolved);
        return ENOTDIR;
    }

    return file_manager_copy_text(output, output_size, resolved);
}

int file_manager_list_directory(const char *directory_path,
                                struct file_manager_entry **entries,
                                size_t *entry_count,
                                char *error_buffer,
                                size_t error_buffer_size)
{
    DIR *directory = NULL;
    struct dirent *entry;
    struct file_manager_entry *result = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int ret = 0;

    if (!directory_path || !entries || !entry_count)
        return EINVAL;

    *entries = NULL;
    *entry_count = 0;

    directory = opendir(directory_path);
    if (!directory) {
        ret = errno;
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Cannot read directory '%s': %s",
                               directory_path, strerror(ret));
        return ret;
    }

    while ((entry = readdir(directory)) != NULL) {
        struct stat st;
        struct file_manager_entry *grown;
        char full_path[FILE_MANAGER_PATH_MAX];
        int join_ret;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        join_ret = file_manager_join_path(directory_path, entry->d_name,
                                          full_path, sizeof(full_path));
        if (join_ret != 0) {
            ret = join_ret;
            file_manager_set_error(error_buffer, error_buffer_size,
                                   "Path is too long inside '%s'",
                                   directory_path);
            goto out;
        }

        if (lstat(full_path, &st) != 0) {
            ret = errno;
            file_manager_set_error(error_buffer, error_buffer_size,
                                   "Cannot stat '%s': %s",
                                   full_path, strerror(ret));
            goto out;
        }

        if (count == capacity) {
            size_t new_capacity = (capacity == 0) ? 16 : capacity * 2;
            grown = (struct file_manager_entry *)realloc(result,
                                                         new_capacity * sizeof(*result));
            if (!grown) {
                ret = ENOMEM;
                file_manager_set_error(error_buffer, error_buffer_size,
                                       "Out of memory while listing '%s'",
                                       directory_path);
                goto out;
            }
            result = grown;
            capacity = new_capacity;
        }

        memset(&result[count], 0, sizeof(result[count]));
        file_manager_copy_text(result[count].name, sizeof(result[count].name), entry->d_name);
        file_manager_copy_text(result[count].path, sizeof(result[count].path), full_path);
        file_manager_copy_text(result[count].type_label, sizeof(result[count].type_label),
                               file_manager_detect_type_label(st.st_mode));
        result[count].is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
        result[count].size_bytes = S_ISREG(st.st_mode) ? (unsigned long long)st.st_size : 0ULL;
        result[count].modified_time = st.st_mtime;
        ++count;
    }

    qsort(result, count, sizeof(*result), file_manager_compare_entries);
    *entries = result;
    *entry_count = count;
    result = NULL;
    ret = 0;

out:
    if (directory)
        closedir(directory);
    free(result);
    return ret;
}

void file_manager_free_entries(struct file_manager_entry *entries)
{
    free(entries);
}

int file_manager_create_directory(const char *parent_directory,
                                  const char *folder_name,
                                  char *error_buffer,
                                  size_t error_buffer_size)
{
    char full_path[FILE_MANAGER_PATH_MAX];
    int ret;

    if (!parent_directory || !folder_name)
        return EINVAL;

    if (folder_name[0] == '\0' || strchr(folder_name, '/')) {
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Folder name must be a single path component");
        return EINVAL;
    }

    ret = file_manager_join_path(parent_directory, folder_name,
                                 full_path, sizeof(full_path));
    if (ret != 0) {
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Folder path is too long");
        return ret;
    }

    if (mkdir(full_path, 0755) != 0) {
        ret = errno;
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Cannot create '%s': %s",
                               full_path, strerror(ret));
        return ret;
    }

    return 0;
}

static int file_manager_delete_path_internal(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0)
        return errno;

    if (S_ISDIR(st.st_mode)) {
        DIR *directory;
        struct dirent *entry;
        int ret = 0;

        directory = opendir(path);
        if (!directory)
            return errno;

        while ((entry = readdir(directory)) != NULL) {
            char child_path[FILE_MANAGER_PATH_MAX];
            int join_ret;

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            join_ret = file_manager_join_path(path, entry->d_name,
                                              child_path, sizeof(child_path));
            if (join_ret != 0) {
                closedir(directory);
                return join_ret;
            }

            ret = file_manager_delete_path_internal(child_path);
            if (ret != 0) {
                closedir(directory);
                return ret;
            }
        }

        closedir(directory);
        if (rmdir(path) != 0)
            return errno;
        return 0;
    }

    if (unlink(path) != 0)
        return errno;

    return 0;
}

int file_manager_delete_path(const char *path,
                             char *error_buffer,
                             size_t error_buffer_size)
{
    int ret;

    if (!path)
        return EINVAL;

    ret = file_manager_delete_path_internal(path);
    if (ret != 0) {
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Cannot delete '%s': %s",
                               path, strerror(ret));
    }

    return ret;
}

int file_manager_get_parent_directory(const char *path,
                                      char *output,
                                      size_t output_size,
                                      char *error_buffer,
                                      size_t error_buffer_size)
{
    char resolved[FILE_MANAGER_PATH_MAX];
    char *last_separator;

    if (!path || !output || output_size == 0)
        return EINVAL;

    if (!realpath(path, resolved)) {
        int ret = errno;
        file_manager_set_error(error_buffer, error_buffer_size,
                               "Cannot resolve '%s': %s",
                               path, strerror(ret));
        return ret;
    }

    if (strcmp(resolved, "/") == 0)
        return file_manager_copy_text(output, output_size, "/");

    last_separator = strrchr(resolved, '/');
    if (!last_separator)
        return file_manager_copy_text(output, output_size, ".");

    if (last_separator == resolved)
        resolved[1] = '\0';
    else
        *last_separator = '\0';

    return file_manager_copy_text(output, output_size, resolved);
}

int file_manager_is_regular_file(const char *path)
{
    struct stat st;

    if (!path)
        return 0;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISREG(st.st_mode) ? 1 : 0;
}

int file_manager_is_directory(const char *path)
{
    struct stat st;

    if (!path)
        return 0;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISDIR(st.st_mode) ? 1 : 0;
}