#ifndef SECURE_AES_PKCS7_H
#define SECURE_AES_PKCS7_H

#include <linux/types.h>

int pkcs7_pad_alloc(const u8 *input, size_t input_len, size_t block_size,
                    u8 **output, size_t *output_len);
int pkcs7_unpad_inplace(u8 *buffer, size_t input_len, size_t block_size,
                        size_t *output_len);

#endif