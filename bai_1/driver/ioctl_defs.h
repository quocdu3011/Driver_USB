#ifndef SECURE_AES_IOCTL_DEFS_H
#define SECURE_AES_IOCTL_DEFS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SECURE_AES_DEVICE_NAME "/dev/secure_aes"
#define SECURE_AES_NODE_NAME "secure_aes"
#define SECURE_AES_BLOCK_SIZE 16U
#define SECURE_AES_MAX_KEY_SIZE 32U
#define SECURE_AES_IV_SIZE 16U
#define SECURE_AES_MAX_BUFFER_SIZE (8U * 1024U * 1024U)

enum secure_aes_mode {
    SECURE_AES_MODE_INVALID = 0,
    SECURE_AES_MODE_ENCRYPT = 1,
    SECURE_AES_MODE_DECRYPT = 2,
};

struct secure_aes_config {
    __u32 mode;
    __u32 key_len;
    __u8 key[SECURE_AES_MAX_KEY_SIZE];
    __u8 iv[SECURE_AES_IV_SIZE];
};

struct secure_aes_status {
    __u64 input_len;
    __u64 output_len;
    __u32 config_set;
    __u32 processed;
};

#define SECURE_AES_IOCTL_MAGIC 'q'
#define SECURE_AES_IOCTL_SET_CONFIG _IOW(SECURE_AES_IOCTL_MAGIC, 0x01, struct secure_aes_config)
#define SECURE_AES_IOCTL_PROCESS _IO(SECURE_AES_IOCTL_MAGIC, 0x02)
#define SECURE_AES_IOCTL_GET_STATUS _IOR(SECURE_AES_IOCTL_MAGIC, 0x03, struct secure_aes_status)
#define SECURE_AES_IOCTL_RESET _IO(SECURE_AES_IOCTL_MAGIC, 0x04)

#endif