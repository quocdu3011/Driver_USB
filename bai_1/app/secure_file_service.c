#include "secure_file_service.h"

#include "driver_client.h"
#include "file_io.h"
#include "hex_utils.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SECURE_STORAGE_DEFAULT_DIR_NAME ".secure_file_manager_storage"
#define SECURE_STORAGE_FILE_SUFFIX ".saes"
#define SECURE_STORAGE_MAGIC "SFMGRV1"
#define SECURE_STORAGE_MAGIC_SIZE 7U

struct secure_storage_file_header {
    unsigned char magic[SECURE_STORAGE_MAGIC_SIZE];
    uint8_t iv[SECURE_AES_IV_SIZE];
};

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

static void secure_zero_memory(void *buffer, size_t length)
{
    volatile unsigned char *cursor = (volatile unsigned char *)buffer;

    if (!cursor)
        return;

    while (length-- > 0)
        *cursor++ = 0;
}

static int secure_storage_copy_text(char *destination,
                                    size_t destination_size,
                                    const char *source)
{
    int written;

    if (!destination || destination_size == 0 || !source)
        return EINVAL;

    written = snprintf(destination, destination_size, "%s", source);
    if (written < 0 || (size_t)written >= destination_size)
        return ENAMETOOLONG;

    return 0;
}

static int secure_storage_join_path(const char *base_path,
                                    const char *name,
                                    char *output,
                                    size_t output_size)
{
    int written;

    if (!base_path || !name || !output || output_size == 0)
        return EINVAL;

    if (strcmp(base_path, "/") == 0)
        written = snprintf(output, output_size, "/%s", name);
    else
        written = snprintf(output, output_size, "%s/%s", base_path, name);

    if (written < 0 || (size_t)written >= output_size)
        return ENAMETOOLONG;

    return 0;
}

static int secure_storage_make_absolute(const char *input,
                                        char *output,
                                        size_t output_size)
{
    char cwd[SECURE_STORAGE_PATH_MAX];

    if (!input || !output || output_size == 0)
        return EINVAL;

    if (input[0] == '/')
        return secure_storage_copy_text(output, output_size, input);

    if (!getcwd(cwd, sizeof(cwd)))
        return errno;

    return secure_storage_join_path(cwd, input, output, output_size);
}

static int secure_storage_mkdir_p(const char *path)
{
    char mutable_path[SECURE_STORAGE_PATH_MAX];
    char *cursor;

    if (!path || path[0] == '\0')
        return EINVAL;

    if (secure_storage_copy_text(mutable_path, sizeof(mutable_path), path) != 0)
        return ENAMETOOLONG;

    for (cursor = mutable_path + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/')
            continue;

        *cursor = '\0';
        if (mkdir(mutable_path, 0700) != 0 && errno != EEXIST)
            return errno;
        *cursor = '/';
    }

    if (mkdir(mutable_path, 0700) != 0 && errno != EEXIST)
        return errno;

    return 0;
}

static int secure_storage_validate_name(const char *file_name,
                                        char *error_buffer,
                                        size_t error_buffer_size)
{
    size_t length;
    size_t index;

    if (!file_name) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tên tệp là bắt buộc");
        return EINVAL;
    }

    length = strlen(file_name);
    if (length == 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tên tệp không được để trống");
        return EINVAL;
    }

    if (length >= SECURE_STORAGE_NAME_MAX) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tên tệp quá dài");
        return ENAMETOOLONG;
    }

    if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0 ||
        strchr(file_name, '/')) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tên tệp phải là một thành phần đường dẫn hợp lệ duy nhất");
        return EINVAL;
    }

    for (index = 0; index < length; ++index) {
        if ((unsigned char)file_name[index] < 32U) {
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Tên tệp chứa ký tự điều khiển không được hỗ trợ");
            return EINVAL;
        }
    }

    return 0;
}

static int secure_storage_build_file_path(const char *storage_dir,
                                          const char *file_name,
                                          char *output,
                                          size_t output_size,
                                          char *error_buffer,
                                          size_t error_buffer_size)
{
    char stored_name[SECURE_STORAGE_NAME_MAX + sizeof(SECURE_STORAGE_FILE_SUFFIX)];
    int ret;

    ret = secure_storage_validate_name(file_name, error_buffer, error_buffer_size);
    if (ret != 0)
        return ret;

    if (snprintf(stored_name, sizeof(stored_name), "%s%s",
                 file_name, SECURE_STORAGE_FILE_SUFFIX) >= (int)sizeof(stored_name)) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tên tệp lưu trữ quá dài");
        return ENAMETOOLONG;
    }

    ret = secure_storage_join_path(storage_dir, stored_name, output, output_size);
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Đường dẫn lưu trữ quá dài");
    }

    return ret;
}

static int secure_storage_has_suffix(const char *name, const char *suffix)
{
    size_t name_len;
    size_t suffix_len;

    if (!name || !suffix)
        return 0;

    name_len = strlen(name);
    suffix_len = strlen(suffix);

    if (name_len <= suffix_len)
        return 0;

    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static int secure_storage_compare_entries(const void *left, const void *right)
{
    const struct secure_storage_entry *lhs = (const struct secure_storage_entry *)left;
    const struct secure_storage_entry *rhs = (const struct secure_storage_entry *)right;

    return strcmp(lhs->name, rhs->name);
}

static int secure_storage_parse_key(const char *key_hex,
                                    uint8_t *key,
                                    size_t *key_len,
                                    char *error_buffer,
                                    size_t error_buffer_size)
{
    int ret;

    if (!key_hex || !key || !key_len) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Khóa AES là bắt buộc");
        return EINVAL;
    }

    ret = parse_hex_string(key_hex, key, key_len, SECURE_AES_MAX_KEY_SIZE);
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Khóa AES không hợp lệ: %s",
                              secure_file_describe_error(ret));
        return ret;
    }

    if (*key_len != 16 && *key_len != 24 && *key_len != 32) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Khóa AES phải dài 16, 24 hoặc 32 byte ở dạng hex");
        return EINVAL;
    }

    return 0;
}

static int secure_storage_fill_random_bytes(uint8_t *buffer, size_t length)
{
    FILE *random_file;
    size_t actual_size;
    int ret = 0;

    if (!buffer || length == 0)
        return EINVAL;

    random_file = fopen("/dev/urandom", "rb");
    if (!random_file)
        return errno;

    actual_size = fread(buffer, 1, length, random_file);
    if (actual_size != length)
        ret = ferror(random_file) ? EIO : EINVAL;

    if (fclose(random_file) != 0 && ret == 0)
        ret = errno;

    return ret;
}

int secure_storage_resolve_directory(const char *requested_path,
                                     char *resolved_path,
                                     size_t resolved_size,
                                     char *error_buffer,
                                     size_t error_buffer_size)
{
    char requested[SECURE_STORAGE_PATH_MAX];
    char absolute_path[SECURE_STORAGE_PATH_MAX];
    char canonical_path[SECURE_STORAGE_PATH_MAX];
    struct stat st;
    const char *home_directory;
    int ret;

    if (!resolved_path || resolved_size == 0)
        return EINVAL;

    if (requested_path && requested_path[0] != '\0') {
        ret = secure_storage_copy_text(requested, sizeof(requested), requested_path);
        if (ret != 0) {
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Đường dẫn thư mục lưu trữ quá dài");
            return ret;
        }
    } else {
        home_directory = getenv("HOME");
        if (home_directory && home_directory[0] != '\0') {
            ret = secure_storage_join_path(home_directory,
                                           SECURE_STORAGE_DEFAULT_DIR_NAME,
                                           requested,
                                           sizeof(requested));
        } else {
            ret = secure_storage_copy_text(requested,
                                           sizeof(requested),
                                           SECURE_STORAGE_DEFAULT_DIR_NAME);
        }

        if (ret != 0) {
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Đường dẫn thư mục lưu trữ mặc định quá dài");
            return ret;
        }
    }

    ret = secure_storage_make_absolute(requested, absolute_path, sizeof(absolute_path));
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể phân giải thư mục lưu trữ '%s': %s",
                              requested, secure_file_describe_error(ret));
        return ret;
    }

    ret = secure_storage_mkdir_p(absolute_path);
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể tạo thư mục lưu trữ '%s': %s",
                              absolute_path, secure_file_describe_error(ret));
        return ret;
    }

    if (stat(absolute_path, &st) != 0) {
        ret = errno;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể truy cập thư mục lưu trữ '%s': %s",
                              absolute_path, secure_file_describe_error(ret));
        return ret;
    }

    if (!S_ISDIR(st.st_mode)) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "'%s' không phải là thư mục", absolute_path);
        return ENOTDIR;
    }

    if (!realpath(absolute_path, canonical_path)) {
        ret = errno;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể chuẩn hóa thư mục lưu trữ '%s': %s",
                              absolute_path, secure_file_describe_error(ret));
        return ret;
    }

    return secure_storage_copy_text(resolved_path, resolved_size, canonical_path);
}

int secure_storage_list_files(const char *storage_dir,
                              struct secure_storage_entry **entries,
                              size_t *entry_count,
                              char *error_buffer,
                              size_t error_buffer_size)
{
    char resolved_storage[SECURE_STORAGE_PATH_MAX];
    DIR *directory = NULL;
    struct dirent *entry;
    struct secure_storage_entry *result = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int ret;

    if (!entries || !entry_count)
        return EINVAL;

    *entries = NULL;
    *entry_count = 0;

    ret = secure_storage_resolve_directory(storage_dir,
                                           resolved_storage,
                                           sizeof(resolved_storage),
                                           error_buffer,
                                           error_buffer_size);
    if (ret != 0)
        return ret;

    directory = opendir(resolved_storage);
    if (!directory) {
        ret = errno;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể đọc thư mục lưu trữ '%s': %s",
                              resolved_storage, secure_file_describe_error(ret));
        return ret;
    }

    while ((entry = readdir(directory)) != NULL) {
        struct stat st;
        struct secure_storage_entry *grown;
        char full_path[SECURE_STORAGE_PATH_MAX];
        size_t visible_name_len;

        if (entry->d_name[0] == '.')
            continue;

        if (!secure_storage_has_suffix(entry->d_name, SECURE_STORAGE_FILE_SUFFIX))
            continue;

        ret = secure_storage_join_path(resolved_storage, entry->d_name,
                                       full_path, sizeof(full_path));
        if (ret != 0) {
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Đường dẫn tệp lưu trữ quá dài");
            goto out;
        }

        if (stat(full_path, &st) != 0) {
            ret = errno;
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Không thể đọc thông tin '%s': %s",
                                  full_path, secure_file_describe_error(ret));
            goto out;
        }

        if (!S_ISREG(st.st_mode))
            continue;

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 16U : capacity * 2U;

            grown = (struct secure_storage_entry *)realloc(result,
                                                           new_capacity * sizeof(*result));
            if (!grown) {
                ret = ENOMEM;
                secure_file_set_error(error_buffer, error_buffer_size,
                                      "Không đủ bộ nhớ khi liệt kê tệp bảo mật");
                goto out;
            }

            result = grown;
            capacity = new_capacity;
        }

        memset(&result[count], 0, sizeof(result[count]));
        visible_name_len = strlen(entry->d_name) - strlen(SECURE_STORAGE_FILE_SUFFIX);
        if (visible_name_len >= sizeof(result[count].name)) {
            ret = ENAMETOOLONG;
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Tên tệp lưu trữ '%s' quá dài",
                                  entry->d_name);
            goto out;
        }

        memcpy(result[count].name, entry->d_name, visible_name_len);
        result[count].name[visible_name_len] = '\0';
        result[count].encrypted_size = (size_t)st.st_size;
        result[count].modified_time = st.st_mtime;
        ++count;
    }

    qsort(result, count, sizeof(*result), secure_storage_compare_entries);
    *entries = result;
    *entry_count = count;
    result = NULL;
    ret = 0;

out:
    if (directory)
        closedir(directory);
    free(result);
    return ret;
}

void secure_storage_free_entries(struct secure_storage_entry *entries)
{
    free(entries);
}

int secure_storage_save_file(const char *storage_dir,
                             const char *device_path,
                             const char *file_name,
                             const char *key_hex,
                             const unsigned char *plain_data,
                             size_t plain_len,
                             int overwrite_existing,
                             size_t *encrypted_size,
                             char *error_buffer,
                             size_t error_buffer_size)
{
    char resolved_storage[SECURE_STORAGE_PATH_MAX];
    char final_path[SECURE_STORAGE_PATH_MAX];
    char temp_path[SECURE_STORAGE_PATH_MAX];
    uint8_t key[SECURE_AES_MAX_KEY_SIZE];
    uint8_t iv[SECURE_AES_IV_SIZE];
    size_t key_len = 0;
    unsigned char *encrypted_data = NULL;
    unsigned char *stored_data = NULL;
    size_t cipher_len = 0;
    size_t stored_len;
    int ret;

    if (!file_name || !key_hex || (!plain_data && plain_len != 0)) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Yêu cầu lưu chưa đầy đủ");
        return EINVAL;
    }

    ret = secure_storage_resolve_directory(storage_dir,
                                           resolved_storage,
                                           sizeof(resolved_storage),
                                           error_buffer,
                                           error_buffer_size);
    if (ret != 0)
        return ret;

    ret = secure_storage_build_file_path(resolved_storage,
                                         file_name,
                                         final_path,
                                         sizeof(final_path),
                                         error_buffer,
                                         error_buffer_size);
    if (ret != 0)
        return ret;

    if (!overwrite_existing) {
        struct stat st;

        if (stat(final_path, &st) == 0) {
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Tệp bảo mật '%s' đã tồn tại",
                                  file_name);
            return EEXIST;
        }

        if (errno != ENOENT) {
            ret = errno;
            secure_file_set_error(error_buffer, error_buffer_size,
                                  "Không thể truy cập '%s': %s",
                                  final_path, secure_file_describe_error(ret));
            return ret;
        }
    }

    ret = secure_storage_parse_key(key_hex, key, &key_len,
                                   error_buffer, error_buffer_size);
    if (ret != 0)
        goto out;

    ret = secure_storage_fill_random_bytes(iv, sizeof(iv));
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể tạo IV ngẫu nhiên: %s",
                              secure_file_describe_error(ret));
        goto out;
    }

    ret = secure_aes_process_buffer(device_path && device_path[0] != '\0'
                                        ? device_path
                                        : SECURE_AES_DEVICE_NAME,
                                    SECURE_AES_MODE_ENCRYPT,
                                    key,
                                    key_len,
                                    iv,
                                    plain_data,
                                    plain_len,
                                    &encrypted_data,
                                    &cipher_len);
    if (ret != 0) {
        ret = -ret;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể mã hóa '%s': %s",
                              file_name, secure_file_describe_error(ret));
        goto out;
    }

    if (cipher_len > SIZE_MAX - sizeof(struct secure_storage_file_header)) {
        ret = EOVERFLOW;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tệp đã mã hóa quá lớn");
        goto out;
    }

    stored_len = sizeof(struct secure_storage_file_header) + cipher_len;
    stored_data = (unsigned char *)malloc(stored_len);
    if (!stored_data) {
        ret = ENOMEM;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không đủ bộ nhớ khi chuẩn bị dữ liệu mã hóa");
        goto out;
    }

    memcpy(stored_data, SECURE_STORAGE_MAGIC, SECURE_STORAGE_MAGIC_SIZE);
    memcpy(stored_data + SECURE_STORAGE_MAGIC_SIZE, iv, sizeof(iv));
    if (cipher_len != 0)
        memcpy(stored_data + sizeof(struct secure_storage_file_header),
               encrypted_data, cipher_len);

    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld",
                 final_path, (long)getpid()) >= (int)sizeof(temp_path)) {
        ret = ENAMETOOLONG;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Đường dẫn tạm thời của kho lưu trữ quá dài");
        goto out;
    }

    ret = write_entire_file(temp_path, stored_data, stored_len);
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể ghi tệp đã mã hóa '%s': %s",
                              temp_path, secure_file_describe_error(ret));
        unlink(temp_path);
        goto out;
    }

    if (rename(temp_path, final_path) != 0) {
        ret = errno;
        unlink(temp_path);
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể hoàn tất '%s': %s",
                              final_path, secure_file_describe_error(ret));
        goto out;
    }

    if (encrypted_size)
        *encrypted_size = stored_len;

    ret = 0;

out:
    secure_zero_memory(key, sizeof(key));
    secure_zero_memory(iv, sizeof(iv));
    secure_storage_free_buffer(encrypted_data, cipher_len);
    secure_storage_free_buffer(stored_data, stored_data ? stored_len : 0);
    return ret;
}

int secure_storage_read_file(const char *storage_dir,
                             const char *device_path,
                             const char *file_name,
                             const char *key_hex,
                             unsigned char **plain_data,
                             size_t *plain_len,
                             char *error_buffer,
                             size_t error_buffer_size)
{
    char resolved_storage[SECURE_STORAGE_PATH_MAX];
    char stored_path[SECURE_STORAGE_PATH_MAX];
    uint8_t key[SECURE_AES_MAX_KEY_SIZE];
    uint8_t iv[SECURE_AES_IV_SIZE];
    size_t key_len = 0;
    unsigned char *stored_data = NULL;
    size_t stored_len = 0;
    unsigned char *decrypted_data = NULL;
    size_t decrypted_len = 0;
    size_t cipher_len;
    int ret;

    if (!file_name || !key_hex || !plain_data || !plain_len) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Yêu cầu đọc chưa đầy đủ");
        return EINVAL;
    }

    *plain_data = NULL;
    *plain_len = 0;

    ret = secure_storage_resolve_directory(storage_dir,
                                           resolved_storage,
                                           sizeof(resolved_storage),
                                           error_buffer,
                                           error_buffer_size);
    if (ret != 0)
        return ret;

    ret = secure_storage_build_file_path(resolved_storage,
                                         file_name,
                                         stored_path,
                                         sizeof(stored_path),
                                         error_buffer,
                                         error_buffer_size);
    if (ret != 0)
        return ret;

    ret = secure_storage_parse_key(key_hex, key, &key_len,
                                   error_buffer, error_buffer_size);
    if (ret != 0)
        goto out;

    ret = read_entire_file(stored_path, &stored_data, &stored_len);
    if (ret != 0) {
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể đọc tệp đã mã hóa '%s': %s",
                              stored_path, secure_file_describe_error(ret));
        goto out;
    }

    if (stored_len < sizeof(struct secure_storage_file_header)) {
        ret = EBADMSG;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tệp lưu trữ '%s' có phần đầu bảo mật không hợp lệ",
                              file_name);
        goto out;
    }

    if (memcmp(stored_data, SECURE_STORAGE_MAGIC, SECURE_STORAGE_MAGIC_SIZE) != 0) {
        ret = EBADMSG;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tệp lưu trữ '%s' không phải tệp của kho bảo mật",
                              file_name);
        goto out;
    }

    memcpy(iv, stored_data + SECURE_STORAGE_MAGIC_SIZE, sizeof(iv));
    cipher_len = stored_len - sizeof(struct secure_storage_file_header);
    if (cipher_len == 0) {
        ret = EBADMSG;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Tệp lưu trữ '%s' thiếu dữ liệu đã mã hóa",
                              file_name);
        goto out;
    }

    ret = secure_aes_process_buffer(device_path && device_path[0] != '\0'
                                        ? device_path
                                        : SECURE_AES_DEVICE_NAME,
                                    SECURE_AES_MODE_DECRYPT,
                                    key,
                                    key_len,
                                    iv,
                                    stored_data + sizeof(struct secure_storage_file_header),
                                    cipher_len,
                                    &decrypted_data,
                                    &decrypted_len);
    if (ret != 0) {
        ret = -ret;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể giải mã '%s': %s",
                              file_name, secure_file_describe_error(ret));
        goto out;
    }

    *plain_data = decrypted_data;
    *plain_len = decrypted_len;
    decrypted_data = NULL;
    ret = 0;

out:
    secure_zero_memory(key, sizeof(key));
    secure_zero_memory(iv, sizeof(iv));
    secure_storage_free_buffer(stored_data, stored_len);
    secure_storage_free_buffer(decrypted_data, decrypted_len);
    return ret;
}

void secure_storage_free_buffer(unsigned char *data, size_t length)
{
    if (!data)
        return;

    secure_zero_memory(data, length);
    free(data);
}

int secure_storage_delete_file(const char *storage_dir,
                               const char *file_name,
                               char *error_buffer,
                               size_t error_buffer_size)
{
    char resolved_storage[SECURE_STORAGE_PATH_MAX];
    char stored_path[SECURE_STORAGE_PATH_MAX];
    int ret;

    ret = secure_storage_resolve_directory(storage_dir,
                                           resolved_storage,
                                           sizeof(resolved_storage),
                                           error_buffer,
                                           error_buffer_size);
    if (ret != 0)
        return ret;

    ret = secure_storage_build_file_path(resolved_storage,
                                         file_name,
                                         stored_path,
                                         sizeof(stored_path),
                                         error_buffer,
                                         error_buffer_size);
    if (ret != 0)
        return ret;

    if (unlink(stored_path) != 0) {
        ret = errno;
        secure_file_set_error(error_buffer, error_buffer_size,
                              "Không thể xóa '%s': %s",
                              stored_path, secure_file_describe_error(ret));
        return ret;
    }

    return 0;
}

const char *secure_file_describe_error(int error_code)
{
    switch (error_code) {
    case E2BIG:
        return "Dữ liệu đầu vào vượt quá giới hạn bộ nhớ của driver (8 MiB)";
    case EBADMSG:
        return "PKCS#7 padding không hợp lệ, khóa AES sai hoặc tệp mã hóa đã hỏng";
    case ENODATA:
        return "Driver không có dữ liệu đệm để giải mã";
    case ENOTTY:
        return "Driver không hỗ trợ ioctl này";
    case ENOENT:
        return "Không tìm thấy tệp hoặc thiết bị";
    case EACCES:
    case EPERM:
        return "Không có quyền truy cập";
    case EEXIST:
        return "Tệp đã tồn tại";
    case ENOTDIR:
        return "Đường dẫn thư mục bắt buộc không hợp lệ";
    case EISDIR:
        return "Đường dẫn tệp lại trỏ tới một thư mục";
    case ENAMETOOLONG:
        return "Đường dẫn quá dài";
    case EINVAL:
        return "Tham số đầu vào không hợp lệ";
    default:
        return strerror(error_code);
    }
}
