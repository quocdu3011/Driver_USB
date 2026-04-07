#include "driver_client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t *buffer, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(fd, buffer + offset, length - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        if (written == 0)
            return -EIO;

        offset += (size_t)written;
    }

    return 0;
}

static int read_all(int fd, uint8_t *buffer, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t received = read(fd, buffer + offset, length - offset);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        if (received == 0)
            return -EIO;

        offset += (size_t)received;
    }

    return 0;
}

int secure_aes_process_buffer(const char *device_path,
                              uint32_t mode,
                              const uint8_t *key, size_t key_len,
                              const uint8_t *iv,
                              const uint8_t *input, size_t input_len,
                              uint8_t **output, size_t *output_len)
{
    struct secure_aes_config config;
    struct secure_aes_status status;
    uint8_t *result = NULL;
    int fd = -1;
    int ret = 0;

    if (!device_path || !key || !iv || (!input && input_len != 0) ||
        !output || !output_len)
        return -EINVAL;

    if (key_len > SECURE_AES_MAX_KEY_SIZE)
        return -EINVAL;

    *output = NULL;
    *output_len = 0;

    memset(&config, 0, sizeof(config));
    memset(&status, 0, sizeof(status));

    config.mode = mode;
    config.key_len = (uint32_t)key_len;
    memcpy(config.key, key, key_len);
    memcpy(config.iv, iv, SECURE_AES_IV_SIZE);

    fd = open(device_path, O_RDWR);
    if (fd < 0)
        return -errno;

    if (ioctl(fd, SECURE_AES_IOCTL_SET_CONFIG, &config) < 0) {
        ret = -errno;
        goto out;
    }

    if (input_len != 0) {
        ret = write_all(fd, input, input_len);
        if (ret != 0)
            goto out;
    }

    if (ioctl(fd, SECURE_AES_IOCTL_PROCESS) < 0) {
        ret = -errno;
        goto out;
    }

    if (ioctl(fd, SECURE_AES_IOCTL_GET_STATUS, &status) < 0) {
        ret = -errno;
        goto out;
    }

    if (!status.processed) {
        ret = -EIO;
        goto out;
    }

    if (status.output_len > SIZE_MAX) {
        ret = -EOVERFLOW;
        goto out;
    }

    if (status.output_len != 0) {
        result = (uint8_t *)malloc((size_t)status.output_len);
        if (!result) {
            ret = -ENOMEM;
            goto out;
        }

        ret = read_all(fd, result, (size_t)status.output_len);
        if (ret != 0)
            goto out;
    }

    *output = result;
    *output_len = (size_t)status.output_len;
    result = NULL;

out:
    if (result)
        free(result);
    if (fd >= 0) {
        (void)ioctl(fd, SECURE_AES_IOCTL_RESET);
        close(fd);
    }
    return ret;
}