#ifndef SECURE_AES_FILE_IO_H
#define SECURE_AES_FILE_IO_H

#include <stddef.h>

int read_entire_file(const char *path, unsigned char **data, size_t *length);
int write_entire_file(const char *path, const unsigned char *data, size_t length);

#endif