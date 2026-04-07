#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "pkcs7.h"

int pkcs7_pad_alloc(const u8 *input, size_t input_len, size_t block_size,
                    u8 **output, size_t *output_len)
{
    u8 *buffer;
    size_t pad_len;
    size_t padded_len;

    if (!output || !output_len || block_size == 0)
        return -EINVAL;

    if (!input && input_len != 0)
        return -EINVAL;

    pad_len = block_size - (input_len % block_size);
    if (pad_len == 0)
        pad_len = block_size;

    padded_len = input_len + pad_len;
    buffer = kmalloc(padded_len, GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    if (input_len != 0)
        memcpy(buffer, input, input_len);

    memset(buffer + input_len, (u8)pad_len, pad_len);

    *output = buffer;
    *output_len = padded_len;
    return 0;
}

int pkcs7_unpad_inplace(u8 *buffer, size_t input_len, size_t block_size,
                        size_t *output_len)
{
    size_t index;
    u8 pad_len;

    if (!buffer || !output_len || block_size == 0)
        return -EINVAL;

    if (input_len == 0 || (input_len % block_size) != 0)
        return -EINVAL;

    pad_len = buffer[input_len - 1];
    if (pad_len == 0 || pad_len > block_size || pad_len > input_len)
        return -EBADMSG;

    for (index = 0; index < pad_len; ++index) {
        if (buffer[input_len - 1 - index] != pad_len)
            return -EBADMSG;
    }

    *output_len = input_len - pad_len;
    return 0;
}