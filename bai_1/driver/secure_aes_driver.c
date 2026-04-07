#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "aes_core.h"
#include "ioctl_defs.h"

#define SECURE_AES_MODULE_NAME "secure_aes"
#define SECURE_AES_CLASS_NAME "secure_aes"

struct secure_aes_session {
    struct mutex lock;
    bool config_set;
    bool processed;
    struct secure_aes_crypto_params params;
    u8 *input_buf;
    size_t input_len;
    u8 *output_buf;
    size_t output_len;
    size_t output_offset;
};

static dev_t secure_aes_dev_number;
static struct cdev secure_aes_cdev;
static struct class *secure_aes_class;
static struct device *secure_aes_device;

static void secure_aes_free_sensitive_buffer(u8 **buffer, size_t *length)
{
    if (buffer && *buffer) {
        if (length && *length != 0)
            memzero_explicit(*buffer, *length);
        kfree(*buffer);
        *buffer = NULL;
    }

    if (length)
        *length = 0;
}

static void secure_aes_clear_output(struct secure_aes_session *session)
{
    secure_aes_free_sensitive_buffer(&session->output_buf, &session->output_len);
    session->output_offset = 0;
    session->processed = false;
}

static void secure_aes_reset_io_buffers(struct secure_aes_session *session)
{
    secure_aes_free_sensitive_buffer(&session->input_buf, &session->input_len);
    secure_aes_clear_output(session);
}

static void secure_aes_reset_session(struct secure_aes_session *session)
{
    secure_aes_reset_io_buffers(session);
    memzero_explicit(&session->params, sizeof(session->params));
    session->config_set = false;
}

static int secure_aes_validate_config(const struct secure_aes_config *config)
{
    if (!config)
        return -EINVAL;

    if (config->mode != SECURE_AES_MODE_ENCRYPT &&
        config->mode != SECURE_AES_MODE_DECRYPT)
        return -EINVAL;

    if (config->key_len != 16 && config->key_len != 24 && config->key_len != 32)
        return -EINVAL;

    return 0;
}

static int secure_aes_open(struct inode *inode, struct file *filp)
{
    struct secure_aes_session *session;

    session = kzalloc(sizeof(*session), GFP_KERNEL);
    if (!session)
        return -ENOMEM;

    mutex_init(&session->lock);
    filp->private_data = session;

    pr_info("secure_aes: device opened\n");
    return nonseekable_open(inode, filp);
}

static int secure_aes_release(struct inode *inode, struct file *filp)
{
    struct secure_aes_session *session = filp->private_data;

    if (session) {
        secure_aes_reset_session(session);
        kfree(session);
    }

    pr_info("secure_aes: device released\n");
    return 0;
}

static ssize_t secure_aes_write(struct file *filp, const char __user *buf,
                                size_t count, loff_t *ppos)
{
    struct secure_aes_session *session = filp->private_data;
    u8 *user_data = NULL;
    u8 *new_input_buf;
    ssize_t ret = count;

    if (!session || !buf)
        return -EINVAL;

    if (count == 0)
        return 0;

    if (count > SECURE_AES_MAX_BUFFER_SIZE)
        return -E2BIG;

    if (mutex_lock_interruptible(&session->lock))
        return -ERESTARTSYS;

    if (!session->config_set) {
        ret = -EINVAL;
        goto out_unlock;
    }

    if (session->input_len + count > SECURE_AES_MAX_BUFFER_SIZE) {
        ret = -E2BIG;
        goto out_unlock;
    }

    user_data = memdup_user(buf, count);
    if (IS_ERR(user_data)) {
        ret = PTR_ERR(user_data);
        user_data = NULL;
        goto out_unlock;
    }

    new_input_buf = krealloc(session->input_buf, session->input_len + count,
                             GFP_KERNEL);
    if (!new_input_buf) {
        ret = -ENOMEM;
        goto out_unlock;
    }

    session->input_buf = new_input_buf;
    memcpy(session->input_buf + session->input_len, user_data, count);
    session->input_len += count;
    secure_aes_clear_output(session);

    if (ppos)
        *ppos = session->input_len;

    pr_info("secure_aes: buffered %zu byte(s), total=%zu\n",
            count, session->input_len);

out_unlock:
    if (user_data) {
        memzero_explicit(user_data, count);
        kfree(user_data);
    }
    mutex_unlock(&session->lock);
    return ret;
}

static ssize_t secure_aes_read(struct file *filp, char __user *buf,
                               size_t count, loff_t *ppos)
{
    struct secure_aes_session *session = filp->private_data;
    size_t available;
    size_t to_copy;
    ssize_t ret;

    if (!session || !buf)
        return -EINVAL;

    if (count == 0)
        return 0;

    if (mutex_lock_interruptible(&session->lock))
        return -ERESTARTSYS;

    if (!session->processed || !session->output_buf) {
        ret = 0;
        goto out_unlock;
    }

    if (session->output_offset >= session->output_len) {
        ret = 0;
        goto out_unlock;
    }

    available = session->output_len - session->output_offset;
    to_copy = min(count, available);

    if (copy_to_user(buf, session->output_buf + session->output_offset, to_copy)) {
        ret = -EFAULT;
        goto out_unlock;
    }

    session->output_offset += to_copy;
    if (ppos)
        *ppos = session->output_offset;

    ret = (ssize_t)to_copy;

out_unlock:
    mutex_unlock(&session->lock);
    return ret;
}

static long secure_aes_ioctl(struct file *filp, unsigned int cmd,
                             unsigned long arg)
{
    struct secure_aes_session *session = filp->private_data;
    struct secure_aes_config user_config;
    struct secure_aes_status status;
    long ret = 0;

    if (!session)
        return -EINVAL;

    switch (cmd) {
    case SECURE_AES_IOCTL_SET_CONFIG:
        if (copy_from_user(&user_config, (void __user *)arg, sizeof(user_config)))
            return -EFAULT;

        ret = secure_aes_validate_config(&user_config);
        if (ret != 0)
            return ret;

        if (mutex_lock_interruptible(&session->lock))
            return -ERESTARTSYS;

        secure_aes_reset_io_buffers(session);
        memzero_explicit(&session->params, sizeof(session->params));

        session->params.mode = user_config.mode;
        session->params.key_len = user_config.key_len;
        memcpy(session->params.key, user_config.key, user_config.key_len);
        memcpy(session->params.iv, user_config.iv, sizeof(session->params.iv));
        session->config_set = true;

        mutex_unlock(&session->lock);

        pr_info("secure_aes: configured mode=%u key_len=%u\n",
                user_config.mode, user_config.key_len);
        return 0;

    case SECURE_AES_IOCTL_PROCESS:
        if (mutex_lock_interruptible(&session->lock))
            return -ERESTARTSYS;

        if (!session->config_set) {
            ret = -EINVAL;
            goto out_ioctl_unlock;
        }

        if (session->params.mode == SECURE_AES_MODE_DECRYPT &&
            session->input_len == 0) {
            ret = -ENODATA;
            goto out_ioctl_unlock;
        }

        secure_aes_clear_output(session);

        ret = aes_cbc_process_buffer(&session->params, session->input_buf,
                                     session->input_len, &session->output_buf,
                                     &session->output_len);
        if (ret == 0) {
            session->processed = true;
            session->output_offset = 0;
            pr_info("secure_aes: processed %zu byte(s) -> %zu byte(s)\n",
                    session->input_len, session->output_len);
        } else {
            session->processed = false;
            pr_err("secure_aes: processing failed with error %ld\n", ret);
        }

        goto out_ioctl_unlock;

    case SECURE_AES_IOCTL_GET_STATUS:
        memset(&status, 0, sizeof(status));

        if (mutex_lock_interruptible(&session->lock))
            return -ERESTARTSYS;

        status.input_len = session->input_len;
        status.output_len = session->output_len;
        status.config_set = session->config_set ? 1U : 0U;
        status.processed = session->processed ? 1U : 0U;

        mutex_unlock(&session->lock);

        if (copy_to_user((void __user *)arg, &status, sizeof(status)))
            return -EFAULT;

        return 0;

    case SECURE_AES_IOCTL_RESET:
        if (mutex_lock_interruptible(&session->lock))
            return -ERESTARTSYS;

        secure_aes_reset_session(session);
        mutex_unlock(&session->lock);

        pr_info("secure_aes: session reset\n");
        return 0;

    default:
        return -ENOTTY;
    }

out_ioctl_unlock:
    mutex_unlock(&session->lock);
    return ret;
}

static const struct file_operations secure_aes_fops = {
    .owner = THIS_MODULE,
    .open = secure_aes_open,
    .release = secure_aes_release,
    .read = secure_aes_read,
    .write = secure_aes_write,
    .unlocked_ioctl = secure_aes_ioctl,
};

static char *secure_aes_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;

    return NULL;
}

static int __init secure_aes_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&secure_aes_dev_number, 0, 1, SECURE_AES_MODULE_NAME);
    if (ret != 0) {
        pr_err("secure_aes: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&secure_aes_cdev, &secure_aes_fops);
    secure_aes_cdev.owner = THIS_MODULE;

    ret = cdev_add(&secure_aes_cdev, secure_aes_dev_number, 1);
    if (ret != 0) {
        pr_err("secure_aes: cdev_add failed: %d\n", ret);
        goto err_unregister_chrdev;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    secure_aes_class = class_create(SECURE_AES_CLASS_NAME);
#else
    secure_aes_class = class_create(THIS_MODULE, SECURE_AES_CLASS_NAME);
#endif
    if (IS_ERR(secure_aes_class)) {
        ret = PTR_ERR(secure_aes_class);
        secure_aes_class = NULL;
        pr_err("secure_aes: class_create failed: %d\n", ret);
        goto err_cdev_del;
    }

    secure_aes_class->devnode = secure_aes_devnode;

    secure_aes_device = device_create(secure_aes_class, NULL, secure_aes_dev_number,
                                      NULL, SECURE_AES_NODE_NAME);
    if (IS_ERR(secure_aes_device)) {
        ret = PTR_ERR(secure_aes_device);
        secure_aes_device = NULL;
        pr_err("secure_aes: device_create failed: %d\n", ret);
        goto err_class_destroy;
    }

    pr_info("secure_aes: loaded successfully (major=%d minor=%d)\n",
            MAJOR(secure_aes_dev_number), MINOR(secure_aes_dev_number));
    pr_info("secure_aes: device path is %s\n", SECURE_AES_DEVICE_NAME);
    return 0;

err_class_destroy:
    class_destroy(secure_aes_class);
    secure_aes_class = NULL;

err_cdev_del:
    cdev_del(&secure_aes_cdev);

err_unregister_chrdev:
    unregister_chrdev_region(secure_aes_dev_number, 1);
    return ret;
}

static void __exit secure_aes_exit(void)
{
    if (secure_aes_device)
        device_destroy(secure_aes_class, secure_aes_dev_number);

    if (secure_aes_class)
        class_destroy(secure_aes_class);

    cdev_del(&secure_aes_cdev);
    unregister_chrdev_region(secure_aes_dev_number, 1);

    pr_info("secure_aes: unloaded\n");
}

module_init(secure_aes_init);
module_exit(secure_aes_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI Codex");
MODULE_DESCRIPTION("Character device driver for AES-CBC file encryption/decryption");
MODULE_VERSION("1.0");