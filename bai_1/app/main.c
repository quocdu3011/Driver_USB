#include "secure_file_service.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s encrypt <input> <output> --key <hex> --iv <hex> [--device <path>]\n"
            "  %s decrypt <input> <output> --key <hex> --iv <hex> [--device <path>]\n",
            program_name, program_name);
}

int main(int argc, char *argv[])
{
    struct secure_file_request request;
    struct secure_file_result result;
    const char *action;
    char error_message[SECURE_FILE_ERROR_MAX];
    int arg_index;
    int ret;

    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    memset(error_message, 0, sizeof(error_message));

    if (argc < 7) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    action = argv[1];
    request.input_path = argv[2];
    request.output_path = argv[3];
    request.device_path = SECURE_AES_DEVICE_NAME;

    ret = secure_file_mode_from_text(action, &request.mode);
    if (ret != 0) {
        fprintf(stderr, "Invalid mode '%s'. Use encrypt or decrypt.\n", action);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    for (arg_index = 4; arg_index < argc; ++arg_index) {
        if (strcmp(argv[arg_index], "--key") == 0 && arg_index + 1 < argc) {
            request.key_hex = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--iv") == 0 && arg_index + 1 < argc) {
            request.iv_hex = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--device") == 0 && arg_index + 1 < argc) {
            request.device_path = argv[++arg_index];
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[arg_index]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!request.key_hex || !request.iv_hex) {
        fprintf(stderr, "Both --key and --iv are required.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    ret = secure_file_process_request(&request, &result,
                                      error_message, sizeof(error_message));
    if (ret != 0) {
        fprintf(stderr, "%s\n",
                error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        return EXIT_FAILURE;
    }

    printf("Success: %s '%s' -> '%s' via %s (%zu byte(s) -> %zu byte(s)).\n",
           secure_file_mode_to_text(request.mode),
           request.input_path,
           request.output_path,
           request.device_path,
           result.input_size,
           result.output_size);
    return EXIT_SUCCESS;
}