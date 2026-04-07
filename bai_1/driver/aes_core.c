#include <crypto/skcipher.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "aes_core.h"
#include "pkcs7.h"

static void secure_aes_free_sensitive(void *buffer, size_t length)
{
    if (!buffer)
        return;

    if (length != 0)
        memzero_explicit(buffer, length);

    kfree(buffer);
}

int aes_cbc_process_buffer(const struct secure_aes_crypto_params *params,
                           const u8 *input, size_t input_len,
                           u8 **output, size_t *output_len)
{
    struct crypto_skcipher *tfm = NULL;
    struct skcipher_request *request = NULL;
    struct scatterlist src_sg;
    struct scatterlist dst_sg;
    u8 iv[SECURE_AES_IV_SIZE];
    u8 *src_buf = NULL;
    u8 *dst_buf = NULL;
    size_t crypt_len = input_len;
    int ret;

    if (!params || !output || !output_len)
        return -EINVAL;

    if (!input && input_len != 0)
        return -EINVAL;

    if (params->key_len != 16 && params->key_len != 24 && params->key_len != 32)
        return -EINVAL;

    if (params->mode != SECURE_AES_MODE_ENCRYPT &&
        params->mode != SECURE_AES_MODE_DECRYPT)
        return -EINVAL;

    if (params->mode == SECURE_AES_MODE_ENCRYPT) {
        ret = pkcs7_pad_alloc(input, input_len, SECURE_AES_BLOCK_SIZE,
                              &src_buf, &crypt_len);
        if (ret != 0)
            return ret;
    } else {
        if (input_len == 0 || (input_len % SECURE_AES_BLOCK_SIZE) != 0)
            return -EINVAL;

        src_buf = kmemdup(input, input_len, GFP_KERNEL);
        if (!src_buf)
            return -ENOMEM;
    }

    dst_buf = kmalloc(crypt_len, GFP_KERNEL);
    if (!dst_buf) {
        ret = -ENOMEM;
        goto out;
    }

    tfm = crypto_alloc_skcipher("cbc(aes)", 0, 0);
    if (IS_ERR(tfm)) {
        ret = PTR_ERR(tfm);
        tfm = NULL;
        goto out;
    }

    ret = crypto_skcipher_setkey(tfm, params->key, params->key_len);
    if (ret != 0)
        goto out;

    request = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (!request) {
        ret = -ENOMEM;
        goto out;
    }

    sg_init_one(&src_sg, src_buf, crypt_len);
    sg_init_one(&dst_sg, dst_buf, crypt_len);
    memcpy(iv, params->iv, sizeof(iv));

    skcipher_request_set_crypt(request, &src_sg, &dst_sg, crypt_len, iv);

    if (params->mode == SECURE_AES_MODE_ENCRYPT)
        ret = crypto_skcipher_encrypt(request);
    else
        ret = crypto_skcipher_decrypt(request);

    if (ret != 0)
        goto out;

    if (params->mode == SECURE_AES_MODE_DECRYPT) {
        ret = pkcs7_unpad_inplace(dst_buf, crypt_len, SECURE_AES_BLOCK_SIZE,
                                  output_len);
        if (ret != 0)
            goto out;
    } else {
        *output_len = crypt_len;
    }

    *output = dst_buf;
    dst_buf = NULL;
    ret = 0;

out:
    memzero_explicit(iv, sizeof(iv));
    if (request)
        skcipher_request_free(request);
    if (tfm)
        crypto_free_skcipher(tfm);
    secure_aes_free_sensitive(src_buf, crypt_len);
    secure_aes_free_sensitive(dst_buf, crypt_len);
    return ret;
}