#include "file_io.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int read_entire_file(const char *path, unsigned char **data, size_t *length)
{
    struct stat st;
    FILE *file;
    unsigned char *buffer = NULL;
    size_t expected_size;
    size_t actual_size;

    if (!path || !data || !length)
        return EINVAL;

    *data = NULL;
    *length = 0;

    if (stat(path, &st) != 0)
        return errno;

    if (st.st_size < 0)
        return EINVAL;

    if ((uintmax_t)st.st_size > SIZE_MAX)
        return EOVERFLOW;

    expected_size = (size_t)st.st_size;

    file = fopen(path, "rb");
    if (!file)
        return errno;

    if (expected_size != 0) {
        buffer = (unsigned char *)malloc(expected_size);
        if (!buffer) {
            fclose(file);
            return ENOMEM;
        }

        actual_size = fread(buffer, 1, expected_size, file);
        if (actual_size != expected_size) {
            int read_failed = ferror(file);
            free(buffer);
            fclose(file);
            return read_failed ? EIO : EINVAL;
        }
    }

    if (fclose(file) != 0) {
        int saved_errno = errno;
        free(buffer);
        return saved_errno;
    }

    *data = buffer;
    *length = expected_size;
    return 0;
}

int write_entire_file(const char *path, const unsigned char *data, size_t length)
{
    FILE *file;
    size_t actual_size;

    if (!path || (!data && length != 0))
        return EINVAL;

    file = fopen(path, "wb");
    if (!file)
        return errno;

    if (length != 0) {
        actual_size = fwrite(data, 1, length, file);
        if (actual_size != length) {
            fclose(file);
            return EIO;
        }
    }

    if (fclose(file) != 0)
        return errno;

    return 0;
}