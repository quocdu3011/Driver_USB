#ifndef SECURE_AES_FILE_SERVICE_H
#define SECURE_AES_FILE_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "ioctl_defs.h"

#define SECURE_FILE_ERROR_MAX 512

struct secure_file_request {
    const char *device_path;
    uint32_t mode;
    const char *input_path;
    const char *output_path;
    const char *key_hex;
    const char *iv_hex;
};

struct secure_file_result {
    size_t input_size;
    size_t output_size;
};

int secure_file_mode_from_text(const char *text, uint32_t *mode);
const char *secure_file_mode_to_text(uint32_t mode);
const char *secure_file_describe_error(int error_code);
int secure_file_process_request(const struct secure_file_request *request,
                                struct secure_file_result *result,
                                char *error_buffer,
                                size_t error_buffer_size);
int secure_file_suggest_output_path(const char *input_path,
                                    uint32_t mode,
                                    char *output_path,
                                    size_t output_size);

#endif