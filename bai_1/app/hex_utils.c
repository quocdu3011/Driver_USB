#include "hex_utils.h"

#include <errno.h>
#include <string.h>

static int hex_char_to_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

int parse_hex_string(const char *hex,
                     uint8_t *output,
                     size_t *output_len,
                     size_t max_output_len)
{
    size_t hex_len;
    size_t index;

    if (!hex || !output || !output_len)
        return EINVAL;

    hex_len = strlen(hex);
    if (hex_len == 0 || (hex_len % 2) != 0)
        return EINVAL;

    if ((hex_len / 2) > max_output_len)
        return E2BIG;

    for (index = 0; index < hex_len; index += 2) {
        int high = hex_char_to_value(hex[index]);
        int low = hex_char_to_value(hex[index + 1]);

        if (high < 0 || low < 0)
            return EINVAL;

        output[index / 2] = (uint8_t)((high << 4) | low);
    }

    *output_len = hex_len / 2;
    return 0;
}

int parse_hex_string_exact(const char *hex,
                           uint8_t *output,
                           size_t expected_output_len)
{
    size_t actual_len = 0;
    int ret;

    ret = parse_hex_string(hex, output, &actual_len, expected_output_len);
    if (ret != 0)
        return ret;

    if (actual_len != expected_output_len)
        return EINVAL;

    return 0;
}