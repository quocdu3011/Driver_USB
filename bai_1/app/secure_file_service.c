#include "secure_file_service.h"

#include "driver_client.h"
#include "file_io.h"
#include "hex_utils.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void secure_file_set_error(char *buffer,
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

static int secure_file_paths_match(const char *input_path, const char *output_path)
{
    struct stat input_stat;
    struct stat output_stat;

    if (!input_path || !output_path)
        return 0;

    if (strcmp(input_path, output_path) == 0)
        return 1;

    if (stat(input_path, &input_stat) != 0)
        return 0;

    if (stat(output_path, &output_stat) != 0)
        return 0;

    return (input_stat.st_dev == output_stat.st_dev &&
            input_stat.st_ino == output_stat.st_ino) ? 1 : 0;
}

int secure_file_mode_from_text(const char *text, uint32_t *mode)
{
    if (!text || !mode)
        return EINVAL;

    if (strcmp(text, "encrypt") == 0) {
        *mode = SECURE_AES_MODE_ENCRYPT;
        return 0;
    }

    if (strcmp(text, "decrypt") == 0) {
        *mode = SECURE_AES_MODE_DECRYPT;
        return 0;
    }

    return EINVAL;
}

const char *secure_file_mode_to_text(uint32_t mode)
{
    switch (mode) {
    case SECURE_AES_MODE_ENCRYPT:
        return "encrypt";
    case SECURE_AES_MODE_DECRYPT:
        return "decrypt";
    default:
        return "unknown";
    }
}

const char *secure_file_describe_error(int error_code)
{
    switch (error_code) {
    case E2BIG:
        return "input file exceeds the driver memory limit (8 MiB)";
    case EBADMSG:
        return "invalid PKCS#7 padding or wrong key/IV";
    case ENODATA:
        return "driver has no buffered data to decrypt";
    case ENOTTY:
        return "driver ioctl is not supported";
    case ENOENT:
        return "device or file not found";
    case EACCES:
    case EPERM:
        return "permission denied when opening the device or file";
    case EINVAL:
        return "invalid input parameters";
    default:
        return strerror(error_code);
    }
}

int secure_file_suggest_output_path(const char *input_path,
                                    uint32_t mode,
                                    char *output_path,
                                    size_t output_size)
{
    static const char encrypted_suffix[] = ".enc";
    static const char decrypted_suffix[] = ".restored";
    size_t input_len;
    int written;

    if (!input_path || !output_path || output_size == 0)
        return EINVAL;

    input_len = strlen(input_path);
    if (input_len == 0)
        return EINVAL;

    if (mode == SECURE_AES_MODE_ENCRYPT) {
        written = snprintf(output_path, output_size, "%s%s",
                           input_path, encrypted_suffix);
    } else if (mode == SECURE_AES_MODE_DECRYPT) {
        size_t suffix_len = strlen(encrypted_suffix);
        if (input_len > suffix_len &&
            strcmp(input_path + input_len - suffix_len, encrypted_suffix) == 0) {
            written = snprintf(output_path, output_size, "%.*s",
                               (int)(input_len - suffix_len), input_path);
        } else {
            written = snprintf(output_path, output_size, "%s%s",
                               input_path, decrypted_suffix);
        }
    } else {
        return EINVAL;
    }

    if (written < 0 || (size_t)written >= output_size)
        return ENAMETOOLONG;

    return 0;
}

int secure_file_process_request(const struct secure_file_request *request,
                                struct secure_file_result *result,
                                char *error_buffer,
                                size_t error_buffer_size)
{
    const char *device_path;
    unsigned char *input_data = NULL;
    unsigned char *output_data = NULL;
    uint8_t key[SECURE_AES_MAX_KEY_SIZE];
    uint8_t iv[SECURE_AES_IV_SIZE];
    size_t key_len = 0;
    size_t input_len = 0;
    size_t output_len = 0;
    int ret;

    if (!request || !request->input_path || !request->output_path ||
        !request->key_hex || !request->iv_hex) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Request is incomplete");
        return EINVAL;
    }

    device_path = (request->device_path && request->device_path[0] != '\0')
                      ? request->device_path
                      : SECURE_AES_DEVICE_NAME;

    if (request->mode != SECURE_AES_MODE_ENCRYPT &&
        request->mode != SECURE_AES_MODE_DECRYPT) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Mode must be encrypt or decrypt");
        return EINVAL;
    }

    if (secure_file_paths_match(request->input_path, request->output_path)) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Input and output must be different files");
        return EINVAL;
    }

    ret = parse_hex_string(request->key_hex, key, &key_len, sizeof(key));
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Invalid AES key: %s",
                              secure_file_describe_error(ret));
        return ret;
    }

    if (key_len != 16 && key_len != 24 && key_len != 32) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "AES key must be 16, 24, or 32 bytes in hex form");
        return EINVAL;
    }

    ret = parse_hex_string_exact(request->iv_hex, iv, sizeof(iv));
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "IV must be exactly 16 bytes in hex form");
        return ret;
    }

    ret = read_entire_file(request->input_path, &input_data, &input_len);
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Cannot read '%s': %s",
                              request->input_path,
                              secure_file_describe_error(ret));
        return ret;
    }

    ret = secure_aes_process_buffer(device_path,
                                    request->mode,
                                    key,
                                    key_len,
                                    iv,
                                    input_data,
                                    input_len,
                                    &output_data,
                                    &output_len);
    if (ret != 0) {
        int driver_error = -ret;
        free(input_data);
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Driver request failed on %s: %s",
                              device_path,
                              secure_file_describe_error(driver_error));
        return driver_error;
    }

    ret = write_entire_file(request->output_path, output_data, output_len);
    if (ret != 0) {
        free(input_data);
        free(output_data);
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Cannot write '%s': %s",
                              request->output_path,
                              secure_file_describe_error(ret));
        return ret;
    }

    if (result) {
        result->input_size = input_len;
        result->output_size = output_len;
    }

    free(input_data);
    free(output_data);
    return 0;
}