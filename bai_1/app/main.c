#include "file_io.h"
#include "secure_file_service.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s list [--storage <dir>]\n"
            "  %s create <name> --key <hex> (--text <text> | --from-file <path>) [--storage <dir>] [--device <path>]\n"
            "  %s update <name> --key <hex> (--text <text> | --from-file <path>) [--storage <dir>] [--device <path>]\n"
            "  %s read <name> --key <hex> [--output <path>] [--storage <dir>] [--device <path>]\n"
            "  %s delete <name> [--storage <dir>]\n",
            program_name, program_name, program_name, program_name, program_name);
}

static int load_plain_input(const char *text_value,
                            const char *input_path,
                            unsigned char **data,
                            size_t *length,
                            char *error_buffer,
                            size_t error_buffer_size)
{
    size_t text_length;
    unsigned char *buffer;
    int ret;

    if (!data || !length)
        return EINVAL;

    *data = NULL;
    *length = 0;

    if ((text_value && input_path) || (!text_value && !input_path)) {
        snprintf(error_buffer, error_buffer_size,
                 "Use exactly one of --text or --from-file");
        return EINVAL;
    }

    if (text_value) {
        text_length = strlen(text_value);
        if (text_length == 0) {
            *length = 0;
            return 0;
        }

        buffer = (unsigned char *)malloc(text_length);
        if (!buffer) {
            snprintf(error_buffer, error_buffer_size,
                     "Out of memory while copying plaintext");
            return ENOMEM;
        }

        memcpy(buffer, text_value, text_length);
        *data = buffer;
        *length = text_length;
        return 0;
    }

    ret = read_entire_file(input_path, data, length);
    if (ret != 0) {
        snprintf(error_buffer, error_buffer_size,
                 "Cannot read '%s': %s",
                 input_path, secure_file_describe_error(ret));
    }

    return ret;
}

static int write_plain_output(const char *output_path,
                              const unsigned char *data,
                              size_t length,
                              char *error_buffer,
                              size_t error_buffer_size)
{
    size_t written;

    if (!data && length != 0)
        return EINVAL;

    if (output_path && output_path[0] != '\0') {
        int ret = write_entire_file(output_path, data, length);
        if (ret != 0) {
            snprintf(error_buffer, error_buffer_size,
                     "Cannot write '%s': %s",
                     output_path, secure_file_describe_error(ret));
        }
        return ret;
    }

    if (length == 0)
        return 0;

    written = fwrite(data, 1, length, stdout);
    if (written != length) {
        snprintf(error_buffer, error_buffer_size,
                 "Cannot write plaintext to stdout");
        return EIO;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *command;
    const char *file_name = NULL;
    const char *storage_dir = NULL;
    const char *device_path = SECURE_AES_DEVICE_NAME;
    const char *key_hex = NULL;
    const char *text_value = NULL;
    const char *input_path = NULL;
    const char *output_path = NULL;
    char error_message[SECURE_FILE_ERROR_MAX];
    char resolved_storage[SECURE_STORAGE_PATH_MAX];
    int arg_index;

    memset(error_message, 0, sizeof(error_message));

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    command = argv[1];

    if ((strcmp(command, "list") == 0) && argc >= 2) {
        for (arg_index = 2; arg_index < argc; ++arg_index) {
            if (strcmp(argv[arg_index], "--storage") == 0 && arg_index + 1 < argc) {
                storage_dir = argv[++arg_index];
            } else {
                fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[arg_index]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }

        {
            struct secure_storage_entry *entries = NULL;
            size_t entry_count = 0;
            size_t index;
            int ret = secure_storage_resolve_directory(storage_dir,
                                                       resolved_storage,
                                                       sizeof(resolved_storage),
                                                       error_message,
                                                       sizeof(error_message));
            if (ret != 0) {
                fprintf(stderr, "%s\n", error_message);
                return EXIT_FAILURE;
            }

            ret = secure_storage_list_files(resolved_storage,
                                            &entries,
                                            &entry_count,
                                            error_message,
                                            sizeof(error_message));
            if (ret != 0) {
                fprintf(stderr, "%s\n", error_message);
                return EXIT_FAILURE;
            }

            printf("Secure storage: %s\n", resolved_storage);
            if (entry_count == 0) {
                printf("No secure files found.\n");
            } else {
                for (index = 0; index < entry_count; ++index) {
                    printf("%s\t%zu byte(s)\n",
                           entries[index].name,
                           entries[index].encrypted_size);
                }
            }

            secure_storage_free_entries(entries);
            return EXIT_SUCCESS;
        }
    }

    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    file_name = argv[2];

    for (arg_index = 3; arg_index < argc; ++arg_index) {
        if (strcmp(argv[arg_index], "--storage") == 0 && arg_index + 1 < argc) {
            storage_dir = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--device") == 0 && arg_index + 1 < argc) {
            device_path = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--key") == 0 && arg_index + 1 < argc) {
            key_hex = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--text") == 0 && arg_index + 1 < argc) {
            text_value = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--from-file") == 0 && arg_index + 1 < argc) {
            input_path = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--output") == 0 && arg_index + 1 < argc) {
            output_path = argv[++arg_index];
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[arg_index]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (strcmp(command, "create") == 0 || strcmp(command, "update") == 0) {
        unsigned char *plain_data = NULL;
        size_t plain_len = 0;
        size_t encrypted_size = 0;
        int overwrite_existing = strcmp(command, "update") == 0;
        int ret;

        if (!key_hex) {
            fprintf(stderr, "--key is required for %s\n", command);
            return EXIT_FAILURE;
        }

        ret = load_plain_input(text_value,
                               input_path,
                               &plain_data,
                               &plain_len,
                               error_message,
                               sizeof(error_message));
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        ret = secure_storage_resolve_directory(storage_dir,
                                               resolved_storage,
                                               sizeof(resolved_storage),
                                               error_message,
                                               sizeof(error_message));
        if (ret != 0) {
            secure_storage_free_buffer(plain_data, plain_len);
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        ret = secure_storage_save_file(resolved_storage,
                                       device_path,
                                       file_name,
                                       key_hex,
                                       plain_data,
                                       plain_len,
                                       overwrite_existing,
                                       &encrypted_size,
                                       error_message,
                                       sizeof(error_message));
        secure_storage_free_buffer(plain_data, plain_len);
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        printf("Saved secure file '%s' in %s (%zu encrypted byte(s)).\n",
               file_name, resolved_storage, encrypted_size);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "read") == 0) {
        unsigned char *plain_data = NULL;
        size_t plain_len = 0;
        int ret;

        if (!key_hex) {
            fprintf(stderr, "--key is required for read\n");
            return EXIT_FAILURE;
        }

        ret = secure_storage_resolve_directory(storage_dir,
                                               resolved_storage,
                                               sizeof(resolved_storage),
                                               error_message,
                                               sizeof(error_message));
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        ret = secure_storage_read_file(resolved_storage,
                                       device_path,
                                       file_name,
                                       key_hex,
                                       &plain_data,
                                       &plain_len,
                                       error_message,
                                       sizeof(error_message));
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        ret = write_plain_output(output_path,
                                 plain_data,
                                 plain_len,
                                 error_message,
                                 sizeof(error_message));
        secure_storage_free_buffer(plain_data, plain_len);
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        if (output_path && output_path[0] != '\0') {
            printf("Decrypted '%s' to '%s'.\n", file_name, output_path);
        }

        return EXIT_SUCCESS;
    }

    if (strcmp(command, "delete") == 0) {
        int ret = secure_storage_resolve_directory(storage_dir,
                                                   resolved_storage,
                                                   sizeof(resolved_storage),
                                                   error_message,
                                                   sizeof(error_message));
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        ret = secure_storage_delete_file(resolved_storage,
                                         file_name,
                                         error_message,
                                         sizeof(error_message));
        if (ret != 0) {
            fprintf(stderr, "%s\n", error_message);
            return EXIT_FAILURE;
        }

        printf("Deleted secure file '%s' from %s.\n",
               file_name, resolved_storage);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Unknown command: %s\n", command);
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
