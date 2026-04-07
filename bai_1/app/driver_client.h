#ifndef SECURE_AES_DRIVER_CLIENT_H
#define SECURE_AES_DRIVER_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "ioctl_defs.h"

int secure_aes_process_buffer(const char *device_path,
                              uint32_t mode,
                              const uint8_t *key, size_t key_len,
                              const uint8_t *iv,
                              const uint8_t *input, size_t input_len,
                              uint8_t **output, size_t *output_len);

#endif