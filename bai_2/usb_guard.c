#include <linux/init.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#define USB_GUARD_NAME "usb_guard"
#define USB_GUARD_PROC_DIR "usb_guard"
#define USB_GUARD_PROC_STATUS "status"
#define USB_GUARD_PROC_CONTROL "control"
#define USB_GUARD_MAX_WHITELIST 32
#define USB_GUARD_STRING_LEN 64
#define USB_GUARD_PATH_LEN 128
#define USB_GUARD_UNKNOWN "N/A"
#define USB_GUARD_POLICY_OBSERVE "observe"
#define USB_GUARD_POLICY_WHITELIST "whitelist"

struct usb_guard_rule {
	u16 vendor_id;
	u16 product_id;
	bool has_serial;
	char serial[USB_GUARD_STRING_LEN];
};

struct usb_guard_device_info {
	u16 vendor_id;
	u16 product_id;
	u8 busnum;
	u8 devnum;
	u8 portnum;
	char devpath[32];
	bool has_serial;
	bool whitelisted;
	u32 connect_count;
	unsigned long connect_jiffies;
	u64 connected_for_ms;
	u64 total_connected_ms;
	char manufacturer[USB_GUARD_STRING_LEN];
	char product[USB_GUARD_STRING_LEN];
	char serial[USB_GUARD_STRING_LEN];
};

struct usb_guard_history_entry {
	struct list_head node;
	u16 vendor_id;
	u16 product_id;
	bool has_serial;
	u32 connect_count;
	u32 active_instances;
	u64 total_connected_ms;
	char serial[USB_GUARD_STRING_LEN];
};

struct usb_guard_device_entry {
	struct list_head node;
	struct usb_guard_device_info info;
	struct usb_guard_history_entry *history;
};

struct usb_guard_seed_ctx {
	int ret;
};

struct usb_guard_block_work {
	struct work_struct work;
	struct usb_guard_device_info info;
};

static char *usb_guard_whitelist_param;
module_param_named(whitelist, usb_guard_whitelist_param, charp, 0444);
MODULE_PARM_DESC(whitelist,
		 "Danh sach whitelist USB mass storage theo dinh dang VVVV:PPPP[:SERIAL],...");

static bool usb_guard_block_untrusted = true;
module_param_named(block_untrusted, usb_guard_block_untrusted, bool, 0644);
MODULE_PARM_DESC(block_untrusted,
		 "Tu dong deauthorize USB mass storage ngoai whitelist bang authorized=0");

static LIST_HEAD(usb_guard_active_devices);
static LIST_HEAD(usb_guard_history_devices);
static DEFINE_MUTEX(usb_guard_lock);

static struct proc_dir_entry *usb_guard_proc_dir;
static struct proc_dir_entry *usb_guard_proc_status;
static struct proc_dir_entry *usb_guard_proc_control;

static struct usb_guard_rule usb_guard_whitelist_rules[USB_GUARD_MAX_WHITELIST];
static unsigned int usb_guard_whitelist_count;
static bool usb_guard_whitelist_enabled;
static struct workqueue_struct *usb_guard_block_wq;

static struct usb_guard_device_info usb_guard_last_device;
static bool usb_guard_has_last_device;
static char usb_guard_last_event[8] = "none";

static bool usb_guard_match_whitelist_locked(u16 vendor_id, u16 product_id,
					     const char *serial, bool has_serial);
static void usb_guard_block_workfn(struct work_struct *work);
static void usb_guard_schedule_block_locked(const struct usb_guard_device_info *info);
static void usb_guard_schedule_blocks_for_active_locked(void);


static void usb_guard_clear_device_info(struct usb_guard_device_info *info)
{
	memset(info, 0, sizeof(*info));
	strscpy(info->devpath, USB_GUARD_UNKNOWN, sizeof(info->devpath));
	strscpy(info->manufacturer, USB_GUARD_UNKNOWN,
		sizeof(info->manufacturer));
	strscpy(info->product, USB_GUARD_UNKNOWN, sizeof(info->product));
	strscpy(info->serial, USB_GUARD_UNKNOWN, sizeof(info->serial));
}

static void usb_guard_copy_optional_string(char *dst, size_t dst_size,
					   const char *src)
{
	if (src && *src) {
		strscpy(dst, src, dst_size);
		return;
	}

	strscpy(dst, USB_GUARD_UNKNOWN, dst_size);
}

static bool usb_guard_has_real_string(const char *value)
{
	return value && *value && strcmp(value, USB_GUARD_UNKNOWN) != 0;
}

static const char *usb_guard_policy_mode(void)
{
	return usb_guard_whitelist_enabled ? USB_GUARD_POLICY_WHITELIST :
					     USB_GUARD_POLICY_OBSERVE;
}

static bool usb_guard_should_block_untrusted_locked(const struct usb_guard_device_info *info)
{
	if (!usb_guard_block_untrusted || !usb_guard_whitelist_enabled)
		return false;

	return !usb_guard_match_whitelist_locked(info->vendor_id, info->product_id,
						 info->serial, info->has_serial);
}

static void usb_guard_schedule_block_locked(const struct usb_guard_device_info *info)
{
	struct usb_guard_block_work *block_work;

	if (!usb_guard_block_wq || !usb_guard_should_block_untrusted_locked(info))
		return;

	block_work = kzalloc(sizeof(*block_work), GFP_KERNEL);
	if (!block_work) {
		pr_err("%s: khong the cap phat block work cho %s\n",
		       USB_GUARD_NAME, info->devpath);
		return;
	}

	INIT_WORK(&block_work->work, usb_guard_block_workfn);
	block_work->info = *info;
	queue_work(usb_guard_block_wq, &block_work->work);

	pr_warn("%s: xep lich deauthorize USB ngoai whitelist devpath=%s vid=%04x pid=%04x serial=\"%s\"\n",
		USB_GUARD_NAME, info->devpath, info->vendor_id,
		info->product_id, info->serial);
}

static void usb_guard_schedule_blocks_for_active_locked(void)
{
	struct usb_guard_device_entry *entry;

	list_for_each_entry(entry, &usb_guard_active_devices, node)
		usb_guard_schedule_block_locked(&entry->info);
}

static bool usb_guard_match_whitelist_locked(u16 vendor_id, u16 product_id,
					     const char *serial, bool has_serial)
{
	unsigned int i;

	if (!usb_guard_whitelist_enabled)
		return false;

	for (i = 0; i < usb_guard_whitelist_count; i++) {
		if (usb_guard_whitelist_rules[i].vendor_id == vendor_id &&
		    usb_guard_whitelist_rules[i].product_id == product_id) {
			if (!usb_guard_whitelist_rules[i].has_serial)
				return true;

			if (has_serial &&
			    !strncmp(usb_guard_whitelist_rules[i].serial,
				     serial, sizeof(usb_guard_whitelist_rules[i].serial)))
				return true;
		}
	}

	return false;
}

static bool usb_guard_match_whitelist(u16 vendor_id, u16 product_id,
				      const char *serial, bool has_serial)
{
	bool matched;

	mutex_lock(&usb_guard_lock);
	matched = usb_guard_match_whitelist_locked(vendor_id, product_id,
						 serial, has_serial);
	mutex_unlock(&usb_guard_lock);

	return matched;
}

static bool usb_guard_rule_equals(const struct usb_guard_rule *lhs,
				  const struct usb_guard_rule *rhs)
{
	if (lhs->vendor_id != rhs->vendor_id ||
	    lhs->product_id != rhs->product_id ||
	    lhs->has_serial != rhs->has_serial)
		return false;

	if (!lhs->has_serial)
		return true;

	return !strncmp(lhs->serial, rhs->serial, sizeof(lhs->serial));
}

static void usb_guard_recalculate_whitelist_state_locked(void)
{
	struct usb_guard_device_entry *entry;

	usb_guard_whitelist_enabled = usb_guard_whitelist_count > 0;

	list_for_each_entry(entry, &usb_guard_active_devices, node)
		entry->info.whitelisted =
			usb_guard_match_whitelist_locked(entry->info.vendor_id,
							 entry->info.product_id,
							 entry->info.serial,
							 entry->info.has_serial);

	if (usb_guard_has_last_device)
		usb_guard_last_device.whitelisted =
			usb_guard_match_whitelist_locked(usb_guard_last_device.vendor_id,
							 usb_guard_last_device.product_id,
							 usb_guard_last_device.serial,
							 usb_guard_last_device.has_serial);

	if (usb_guard_block_untrusted && usb_guard_whitelist_enabled)
		usb_guard_schedule_blocks_for_active_locked();
}

static int usb_guard_find_rule_locked(const struct usb_guard_rule *rule)
{
	unsigned int i;

	for (i = 0; i < usb_guard_whitelist_count; i++) {
		if (usb_guard_rule_equals(&usb_guard_whitelist_rules[i], rule))
			return (int)i;
	}

	return -ENOENT;
}

static void usb_guard_format_rule(const struct usb_guard_rule *rule, char *buffer,
				  size_t buffer_size)
{
	if (rule->has_serial)
		snprintf(buffer, buffer_size, "%04x:%04x:%s",
			 rule->vendor_id, rule->product_id, rule->serial);
	else
		snprintf(buffer, buffer_size, "%04x:%04x",
			 rule->vendor_id, rule->product_id);
}

static int usb_guard_add_rule(const struct usb_guard_rule *rule)
{
	char rule_text[USB_GUARD_STRING_LEN + 16];
	int ret = 0;

	mutex_lock(&usb_guard_lock);

	if (usb_guard_whitelist_count >= USB_GUARD_MAX_WHITELIST) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	if (usb_guard_find_rule_locked(rule) >= 0) {
		ret = -EEXIST;
		goto out_unlock;
	}

	usb_guard_whitelist_rules[usb_guard_whitelist_count++] = *rule;
	usb_guard_recalculate_whitelist_state_locked();

out_unlock:
	mutex_unlock(&usb_guard_lock);

	if (!ret) {
		usb_guard_format_rule(rule, rule_text, sizeof(rule_text));
		pr_info("%s: da them whitelist rule %s\n",
			USB_GUARD_NAME, rule_text);
	}

	return ret;
}

static int usb_guard_remove_rule(const struct usb_guard_rule *rule)
{
	char rule_text[USB_GUARD_STRING_LEN + 16];
	int index;

	mutex_lock(&usb_guard_lock);

	index = usb_guard_find_rule_locked(rule);
	if (index < 0) {
		mutex_unlock(&usb_guard_lock);
		return index;
	}

	for (; index < (int)usb_guard_whitelist_count - 1; index++)
		usb_guard_whitelist_rules[index] =
			usb_guard_whitelist_rules[index + 1];

	memset(&usb_guard_whitelist_rules[usb_guard_whitelist_count - 1], 0,
	       sizeof(usb_guard_whitelist_rules[0]));
	usb_guard_whitelist_count--;
	usb_guard_recalculate_whitelist_state_locked();
	mutex_unlock(&usb_guard_lock);

	usb_guard_format_rule(rule, rule_text, sizeof(rule_text));
	pr_info("%s: da xoa whitelist rule %s\n", USB_GUARD_NAME, rule_text);

	return 0;
}

static void usb_guard_clear_whitelist_rules(void)
{
	mutex_lock(&usb_guard_lock);
	memset(usb_guard_whitelist_rules, 0, sizeof(usb_guard_whitelist_rules));
	usb_guard_whitelist_count = 0;
	usb_guard_recalculate_whitelist_state_locked();
	mutex_unlock(&usb_guard_lock);

	pr_info("%s: da xoa toan bo whitelist runtime\n", USB_GUARD_NAME);
}

static void usb_guard_set_block_untrusted(bool enabled)
{
	mutex_lock(&usb_guard_lock);
	usb_guard_block_untrusted = enabled;
	if (usb_guard_block_untrusted && usb_guard_whitelist_enabled)
		usb_guard_schedule_blocks_for_active_locked();
	mutex_unlock(&usb_guard_lock);

	pr_info("%s: che do tu dong deauthorize USB ngoai whitelist %s\n",
		USB_GUARD_NAME, enabled ? "bat" : "tat");
}

static int usb_guard_parse_rule(char *token, struct usb_guard_rule *rule)
{
	char *sep;
	char *serial_sep;
	u16 vendor_id;
	u16 product_id;
	int ret;

	token = strim(token);
	if (!*token)
		return -EINVAL;

	memset(rule, 0, sizeof(*rule));

	sep = strchr(token, ':');
	if (!sep)
		return -EINVAL;

	*sep = '\0';
	sep++;
	if (!*sep)
		return -EINVAL;

	serial_sep = strchr(sep, ':');
	if (serial_sep) {
		*serial_sep = '\0';
		serial_sep++;
		serial_sep = strim(serial_sep);
		if (!*serial_sep)
			return -EINVAL;
	}

	ret = kstrtou16(strim(token), 16, &vendor_id);
	if (ret)
		return ret;

	ret = kstrtou16(strim(sep), 16, &product_id);
	if (ret)
		return ret;

	rule->vendor_id = vendor_id;
	rule->product_id = product_id;
	if (serial_sep) {
		rule->has_serial = true;
		strscpy(rule->serial, serial_sep, sizeof(rule->serial));
	}

	return 0;
}

static int usb_guard_parse_whitelist(void)
{
	char *raw;
	char *cursor;
	char *token;

	usb_guard_whitelist_count = 0;
	usb_guard_whitelist_enabled = false;

	if (!usb_guard_whitelist_param || !*usb_guard_whitelist_param)
		return 0;

	raw = kstrdup(usb_guard_whitelist_param, GFP_KERNEL);
	if (!raw)
		return -ENOMEM;

	cursor = raw;
	while ((token = strsep(&cursor, ",")) != NULL) {
		int ret;

		if (usb_guard_whitelist_count >= USB_GUARD_MAX_WHITELIST) {
			pr_err("%s: whitelist vuot qua gioi han %u muc\n",
			       USB_GUARD_NAME, USB_GUARD_MAX_WHITELIST);
			kfree(raw);
			return -EINVAL;
		}

		ret = usb_guard_parse_rule(token,
					   &usb_guard_whitelist_rules
						[usb_guard_whitelist_count]);
		if (ret) {
			pr_err("%s: whitelist khong hop le: \"%s\"\n",
			       USB_GUARD_NAME, token);
			kfree(raw);
			return -EINVAL;
		}

		usb_guard_whitelist_count++;
	}

	kfree(raw);
	usb_guard_whitelist_enabled = usb_guard_whitelist_count > 0;

	return 0;
}

static bool usb_guard_is_mass_storage(struct usb_device *udev)
{
	struct usb_host_config *config;
	int i;

	if (!udev || !udev->actconfig)
		return false;

	config = udev->actconfig;
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = config->interface[i];

		if (!intf || !intf->cur_altsetting)
			continue;

		if (intf->cur_altsetting->desc.bInterfaceClass ==
		    USB_CLASS_MASS_STORAGE)
			return true;
	}

	return false;
}

static struct usb_guard_history_entry *
usb_guard_find_history_locked(const struct usb_guard_device_info *info)
{
	struct usb_guard_history_entry *history;

	list_for_each_entry(history, &usb_guard_history_devices, node) {
		if (history->vendor_id != info->vendor_id ||
		    history->product_id != info->product_id ||
		    history->has_serial != info->has_serial)
			continue;

		if (!history->has_serial ||
		    !strncmp(history->serial, info->serial,
			     sizeof(history->serial)))
			return history;
	}

	return NULL;
}

static struct usb_guard_history_entry *
usb_guard_get_or_create_history_locked(const struct usb_guard_device_info *info)
{
	struct usb_guard_history_entry *history;

	history = usb_guard_find_history_locked(info);
	if (history)
		return history;

	history = kzalloc(sizeof(*history), GFP_KERNEL);
	if (!history)
		return ERR_PTR(-ENOMEM);

	history->vendor_id = info->vendor_id;
	history->product_id = info->product_id;
	history->has_serial = info->has_serial;
	if (info->has_serial)
		strscpy(history->serial, info->serial, sizeof(history->serial));
	else
		strscpy(history->serial, USB_GUARD_UNKNOWN,
			sizeof(history->serial));

	list_add_tail(&history->node, &usb_guard_history_devices);

	return history;
}

static void usb_guard_fill_device_info(struct usb_device *udev,
				       struct usb_guard_device_info *info)
{
	const char *device_name;

	usb_guard_clear_device_info(info);

	info->vendor_id = le16_to_cpu(udev->descriptor.idVendor);
	info->product_id = le16_to_cpu(udev->descriptor.idProduct);
	info->busnum = udev->bus ? (u8)udev->bus->busnum : 0;
	info->devnum = udev->devnum;
	info->portnum = udev->portnum;
	info->connect_jiffies = udev->connect_time ? udev->connect_time : jiffies;
	info->has_serial = usb_guard_has_real_string(udev->serial);

	device_name = dev_name(&udev->dev);
	if (device_name && *device_name)
		strscpy(info->devpath, device_name, sizeof(info->devpath));
	else if (udev->devpath[0])
		strscpy(info->devpath, udev->devpath, sizeof(info->devpath));

	usb_guard_copy_optional_string(info->manufacturer,
				       sizeof(info->manufacturer),
				       udev->manufacturer);
	usb_guard_copy_optional_string(info->product, sizeof(info->product),
				       udev->product);
	usb_guard_copy_optional_string(info->serial, sizeof(info->serial),
				       udev->serial);
	info->whitelisted = usb_guard_match_whitelist(info->vendor_id,
						      info->product_id,
						      info->serial,
						      info->has_serial);
}

static void usb_guard_log_event(const char *event,
				const struct usb_guard_device_info *info)
{
	pr_info("%s: %s bus=%u dev=%u devpath=%s vid=%04x pid=%04x whitelisted=%u connect_count=%u connected_for_ms=%llu total_connected_ms=%llu manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
		USB_GUARD_NAME, event, info->busnum, info->devnum,
		info->devpath, info->vendor_id, info->product_id,
		info->whitelisted ? 1 : 0, info->connect_count,
		(unsigned long long)info->connected_for_ms,
		(unsigned long long)info->total_connected_ms,
		info->manufacturer, info->product,
		info->serial);
}

static void usb_guard_warn_untrusted(const struct usb_guard_device_info *info)
{
	if (!usb_guard_whitelist_enabled || info->whitelisted)
		return;

	pr_warn("%s: canh bao USB ngoai whitelist bus=%u dev=%u devpath=%s vid=%04x pid=%04x serial=\"%s\"\n",
		USB_GUARD_NAME, info->busnum, info->devnum, info->devpath,
		info->vendor_id, info->product_id, info->serial);
}

static void usb_guard_block_workfn(struct work_struct *work)
{
	struct usb_guard_block_work *block_work =
		container_of(work, struct usb_guard_block_work, work);
	struct file *file;
	char path[USB_GUARD_PATH_LEN];
	loff_t pos = 0;
	ssize_t written;
	bool should_block;

	mutex_lock(&usb_guard_lock);
	should_block = usb_guard_should_block_untrusted_locked(&block_work->info);
	mutex_unlock(&usb_guard_lock);

	if (!should_block) {
		pr_info("%s: bo qua deauthorize devpath=%s vi chinh sach da thay doi\n",
			USB_GUARD_NAME, block_work->info.devpath);
		goto out_free;
	}

	snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/authorized",
		 block_work->info.devpath);
	file = filp_open(path, O_WRONLY, 0);
	if (IS_ERR(file)) {
		pr_err("%s: khong the mo %s de deauthorize (%ld)\n",
		       USB_GUARD_NAME, path, PTR_ERR(file));
		goto out_free;
	}

	written = kernel_write(file, "0\n", 2, &pos);
	filp_close(file, NULL);

	if (written != 2) {
		pr_err("%s: ghi authorized=0 that bai cho %s (ret=%zd)\n",
		       USB_GUARD_NAME, path, written);
		goto out_free;
	}

	pr_warn("%s: da deauthorize USB ngoai whitelist devpath=%s vid=%04x pid=%04x serial=\"%s\"\n",
		USB_GUARD_NAME, block_work->info.devpath,
		block_work->info.vendor_id, block_work->info.product_id,
		block_work->info.serial);

out_free:
	kfree(block_work);
}

static struct usb_guard_device_entry *
usb_guard_find_device_locked(u8 busnum, const char *devpath)
{
	struct usb_guard_device_entry *entry;

	list_for_each_entry(entry, &usb_guard_active_devices, node) {
		if (entry->info.busnum == busnum &&
		    !strncmp(entry->info.devpath, devpath,
			     sizeof(entry->info.devpath)))
			return entry;
	}

	return NULL;
}

static int usb_guard_upsert_device(const struct usb_guard_device_info *info,
				   bool update_last_event,
				   struct usb_guard_device_info *stored_info)
{
	struct usb_guard_device_entry *entry;
	struct usb_guard_history_entry *history;
	bool is_new_entry = false;

	mutex_lock(&usb_guard_lock);

	entry = usb_guard_find_device_locked(info->busnum, info->devpath);
	if (!entry) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			mutex_unlock(&usb_guard_lock);
			return -ENOMEM;
		}

		list_add_tail(&entry->node, &usb_guard_active_devices);
		is_new_entry = true;
	}

	history = usb_guard_get_or_create_history_locked(info);
	if (IS_ERR(history)) {
		if (is_new_entry) {
			list_del(&entry->node);
			kfree(entry);
		}
		mutex_unlock(&usb_guard_lock);
		return PTR_ERR(history);
	}

	if (is_new_entry) {
		history->connect_count++;
		history->active_instances++;
	}

	entry->info = *info;
	entry->info.connect_count = history->connect_count;
	entry->info.connected_for_ms = 0;
	entry->info.total_connected_ms = history->total_connected_ms;
	entry->history = history;

	if (update_last_event) {
		usb_guard_last_device = entry->info;
		usb_guard_has_last_device = true;
		strscpy(usb_guard_last_event, "add",
			sizeof(usb_guard_last_event));
	}

	if (stored_info)
		*stored_info = entry->info;

	mutex_unlock(&usb_guard_lock);

	return 0;
}

static void usb_guard_remove_device(struct usb_device *udev)
{
	struct usb_guard_device_entry *entry;
	struct usb_guard_device_info removed_info;
	bool found = false;
	u8 busnum;
	const char *device_name;
	char devpath[sizeof(removed_info.devpath)];

	if (!udev)
		return;

	busnum = udev->bus ? (u8)udev->bus->busnum : 0;
	usb_guard_clear_device_info(&removed_info);
	memset(devpath, 0, sizeof(devpath));

	device_name = dev_name(&udev->dev);
	if (device_name && *device_name)
		strscpy(devpath, device_name, sizeof(devpath));
	else if (udev->devpath[0])
		strscpy(devpath, udev->devpath, sizeof(devpath));
	else
		strscpy(devpath, USB_GUARD_UNKNOWN, sizeof(devpath));

	mutex_lock(&usb_guard_lock);

	entry = usb_guard_find_device_locked(busnum, devpath);
	if (entry) {
		u64 duration_ms = jiffies_to_msecs(jiffies -
						 entry->info.connect_jiffies);

		removed_info = entry->info;
		removed_info.connected_for_ms = duration_ms;
		if (entry->history) {
			entry->history->total_connected_ms += duration_ms;
			removed_info.total_connected_ms =
				entry->history->total_connected_ms;
			removed_info.connect_count = entry->history->connect_count;
			if (entry->history->active_instances > 0)
				entry->history->active_instances--;
		}
		usb_guard_last_device = entry->info;
		usb_guard_last_device.connected_for_ms = removed_info.connected_for_ms;
		usb_guard_last_device.total_connected_ms =
			removed_info.total_connected_ms;
		usb_guard_last_device.connect_count = removed_info.connect_count;
		usb_guard_has_last_device = true;
		strscpy(usb_guard_last_event, "remove",
			sizeof(usb_guard_last_event));
		list_del(&entry->node);
		kfree(entry);
		found = true;
	}

	mutex_unlock(&usb_guard_lock);

	if (found)
		usb_guard_log_event("remove", &removed_info);
}

static void usb_guard_track_device(struct usb_device *udev, bool update_last,
				   bool log_event)
{
	struct usb_guard_device_info info;
	struct usb_guard_device_info stored_info;
	int ret;

	if (!udev || !usb_guard_is_mass_storage(udev))
		return;

	usb_guard_fill_device_info(udev, &info);

	ret = usb_guard_upsert_device(&info, update_last, &stored_info);
	if (ret) {
		pr_err("%s: khong the cap nhat trang thai cho %s (%d)\n",
		       USB_GUARD_NAME, info.devpath, ret);
		return;
	}

	usb_guard_warn_untrusted(&info);
	mutex_lock(&usb_guard_lock);
	usb_guard_schedule_block_locked(&stored_info);
	mutex_unlock(&usb_guard_lock);

	if (log_event)
		usb_guard_log_event("add", &stored_info);
}

static int usb_guard_seed_one_device(struct usb_device *udev, void *data)
{
	struct usb_guard_seed_ctx *ctx = data;
	struct usb_guard_device_info info;
	int ret;

	if (!udev || !usb_guard_is_mass_storage(udev))
		return 0;

	usb_guard_fill_device_info(udev, &info);
	info.connect_jiffies = jiffies;
	ret = usb_guard_upsert_device(&info, false, NULL);
	if (ret) {
		ctx->ret = ret;
		return ret;
	}

	usb_guard_warn_untrusted(&info);
	mutex_lock(&usb_guard_lock);
	usb_guard_schedule_block_locked(&info);
	mutex_unlock(&usb_guard_lock);

	return 0;
}

static int usb_guard_seed_existing_devices(void)
{
	struct usb_guard_seed_ctx ctx = {
		.ret = 0,
	};

	return usb_for_each_dev(&ctx, usb_guard_seed_one_device);
}

static int usb_guard_usb_notify(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct usb_device *udev = data;

	switch (action) {
	case USB_DEVICE_ADD:
		usb_guard_track_device(udev, true, true);
		break;
	case USB_DEVICE_REMOVE:
		usb_guard_remove_device(udev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block usb_guard_usb_nb = {
	.notifier_call = usb_guard_usb_notify,
};

static void usb_guard_print_whitelist_entries(struct seq_file *m)
{
	unsigned int i;

	if (!usb_guard_whitelist_enabled) {
		seq_puts(m, "(none)");
		return;
	}

	for (i = 0; i < usb_guard_whitelist_count; i++) {
		if (i)
			seq_putc(m, ',');

		seq_printf(m, "%04x:%04x",
			   usb_guard_whitelist_rules[i].vendor_id,
			   usb_guard_whitelist_rules[i].product_id);
		if (usb_guard_whitelist_rules[i].has_serial)
			seq_printf(m, ":%s",
				   usb_guard_whitelist_rules[i].serial);
	}
}

static unsigned int usb_guard_active_device_count_locked(void)
{
	struct usb_guard_device_entry *entry;
	unsigned int count = 0;

	list_for_each_entry(entry, &usb_guard_active_devices, node)
		count++;

	return count;
}

static unsigned int usb_guard_history_device_count_locked(void)
{
	struct usb_guard_history_entry *entry;
	unsigned int count = 0;

	list_for_each_entry(entry, &usb_guard_history_devices, node)
		count++;

	return count;
}

static void usb_guard_seq_print_device(struct seq_file *m, const char *prefix,
				       const struct usb_guard_device_info *info)
{
	seq_printf(m,
		   "%s bus=%u dev=%u devpath=%s vid=%04x pid=%04x whitelisted=%u connect_count=%u connected_for_ms=%llu total_connected_ms=%llu manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
		   prefix, info->busnum, info->devnum, info->devpath,
		   info->vendor_id, info->product_id,
		   info->whitelisted ? 1 : 0, info->connect_count,
		   (unsigned long long)info->connected_for_ms,
		   (unsigned long long)info->total_connected_ms,
		   info->manufacturer,
		   info->product, info->serial);
}

static void usb_guard_seq_print_history(struct seq_file *m, unsigned int index,
					const struct usb_guard_history_entry *entry)
{
	seq_printf(m,
		   "history[%u]: vid=%04x pid=%04x serial=\"%s\" connect_count=%u active_instances=%u total_connected_ms=%llu\n",
		   index, entry->vendor_id, entry->product_id, entry->serial,
		   entry->connect_count, entry->active_instances,
		   (unsigned long long)entry->total_connected_ms);
}

static int usb_guard_proc_show(struct seq_file *m, void *v)
{
	struct usb_guard_device_entry *entry;
	struct usb_guard_history_entry *history;
	unsigned int index = 0;
	unsigned int history_index = 0;

	mutex_lock(&usb_guard_lock);

	seq_printf(m, "module: %s\n", USB_GUARD_NAME);
	seq_printf(m, "whitelist_enabled: %u\n",
		   usb_guard_whitelist_enabled ? 1 : 0);
	seq_printf(m, "policy_mode: %s\n", usb_guard_policy_mode());
	seq_printf(m, "block_untrusted_enabled: %u\n",
		   usb_guard_block_untrusted ? 1 : 0);
	seq_puts(m, "whitelist_entries: ");
	usb_guard_print_whitelist_entries(m);
	seq_putc(m, '\n');
	seq_printf(m, "last_event: %s\n", usb_guard_last_event);
	seq_printf(m, "active_count: %u\n",
		   usb_guard_active_device_count_locked());
	seq_printf(m, "history_count: %u\n",
		   usb_guard_history_device_count_locked());

	if (usb_guard_has_last_device)
		usb_guard_seq_print_device(m, "last_device:",
					   &usb_guard_last_device);
	else
		seq_puts(m, "last_device: none\n");

	list_for_each_entry(entry, &usb_guard_active_devices, node) {
		struct usb_guard_device_info active_info = entry->info;
		char prefix[32];

		active_info.connected_for_ms =
			jiffies_to_msecs(jiffies - active_info.connect_jiffies);
		if (entry->history) {
			active_info.connect_count = entry->history->connect_count;
			active_info.total_connected_ms =
				entry->history->total_connected_ms +
				active_info.connected_for_ms;
		} else {
			active_info.total_connected_ms =
				active_info.connected_for_ms;
		}

			snprintf(prefix, sizeof(prefix), "active[%u]:", index++);
			usb_guard_seq_print_device(m, prefix, &active_info);
	}

	list_for_each_entry(history, &usb_guard_history_devices, node)
		usb_guard_seq_print_history(m, history_index++, history);

	mutex_unlock(&usb_guard_lock);

	return 0;
}

static int usb_guard_proc_control_show(struct seq_file *m, void *v)
{
	mutex_lock(&usb_guard_lock);
	seq_puts(m, "commands:\n");
	seq_puts(m, "  add VVVV:PPPP[:SERIAL]\n");
	seq_puts(m, "  remove VVVV:PPPP[:SERIAL]\n");
	seq_puts(m, "  clear\n");
	seq_puts(m, "  block on|off\n");
	seq_printf(m, "block_untrusted_enabled: %u\n",
		   usb_guard_block_untrusted ? 1 : 0);
	seq_puts(m, "current_whitelist: ");
	usb_guard_print_whitelist_entries(m);
	seq_putc(m, '\n');
	mutex_unlock(&usb_guard_lock);

	return 0;
}

static int usb_guard_proc_control_open(struct inode *inode, struct file *file)
{
	return single_open(file, usb_guard_proc_control_show, NULL);
}

static ssize_t usb_guard_proc_control_write(struct file *file,
					   const char __user *buffer,
					   size_t count, loff_t *ppos)
{
	char *kbuf;
	char *cursor;
	char *arg = NULL;
	struct usb_guard_rule rule;
	int ret = 0;

	if (count == 0)
		return 0;

	if (*ppos != 0)
		return -EINVAL;

	if (count > 256)
		return -E2BIG;

	kbuf = memdup_user_nul(buffer, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	cursor = strim(kbuf);
	if (!*cursor) {
		ret = -EINVAL;
		goto out_free;
	}

	arg = strpbrk(cursor, " \t");
	if (arg) {
		*arg = '\0';
		arg = strim(arg + 1);
	}

	if (!strcmp(cursor, "add")) {
		if (!arg || !*arg) {
			ret = -EINVAL;
			goto out_free;
		}

		ret = usb_guard_parse_rule(arg, &rule);
		if (ret)
			goto out_free;

		ret = usb_guard_add_rule(&rule);
	} else if (!strcmp(cursor, "remove")) {
		if (!arg || !*arg) {
			ret = -EINVAL;
			goto out_free;
		}

		ret = usb_guard_parse_rule(arg, &rule);
		if (ret)
			goto out_free;

		ret = usb_guard_remove_rule(&rule);
	} else if (!strcmp(cursor, "clear")) {
		usb_guard_clear_whitelist_rules();
	} else if (!strcmp(cursor, "block")) {
		if (!arg || !*arg) {
			ret = -EINVAL;
			goto out_free;
		}

		if (!strcmp(arg, "on") || !strcmp(arg, "1") ||
		    !strcmp(arg, "enable")) {
			usb_guard_set_block_untrusted(true);
		} else if (!strcmp(arg, "off") || !strcmp(arg, "0") ||
			   !strcmp(arg, "disable")) {
			usb_guard_set_block_untrusted(false);
		} else {
			ret = -EINVAL;
		}
	} else {
		ret = -EINVAL;
	}

out_free:
	kfree(kbuf);

	if (ret)
		return ret;

	*ppos += count;
	return count;
}

static const struct proc_ops usb_guard_proc_control_ops = {
	.proc_open = usb_guard_proc_control_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = usb_guard_proc_control_write,
};

static void usb_guard_remove_proc_entries(void)
{
	if (usb_guard_proc_control) {
		proc_remove(usb_guard_proc_control);
		usb_guard_proc_control = NULL;
	}

	if (usb_guard_proc_status) {
		proc_remove(usb_guard_proc_status);
		usb_guard_proc_status = NULL;
	}

	if (usb_guard_proc_dir) {
		proc_remove(usb_guard_proc_dir);
		usb_guard_proc_dir = NULL;
	}
}

static void usb_guard_free_active_devices(void)
{
	struct usb_guard_device_entry *entry;
	struct usb_guard_device_entry *tmp;

	mutex_lock(&usb_guard_lock);

	list_for_each_entry_safe(entry, tmp, &usb_guard_active_devices, node) {
		list_del(&entry->node);
		kfree(entry);
	}

	mutex_unlock(&usb_guard_lock);
}

static void usb_guard_free_history_devices(void)
{
	struct usb_guard_history_entry *entry;
	struct usb_guard_history_entry *tmp;

	mutex_lock(&usb_guard_lock);

	list_for_each_entry_safe(entry, tmp, &usb_guard_history_devices, node) {
		list_del(&entry->node);
		kfree(entry);
	}

	mutex_unlock(&usb_guard_lock);
}

static void usb_guard_destroy_block_wq(void)
{
	if (!usb_guard_block_wq)
		return;

	destroy_workqueue(usb_guard_block_wq);
	usb_guard_block_wq = NULL;
}

static int __init usb_guard_init(void)
{
	int ret;

	usb_guard_clear_device_info(&usb_guard_last_device);
	usb_guard_has_last_device = false;
	strscpy(usb_guard_last_event, "none", sizeof(usb_guard_last_event));

	ret = usb_guard_parse_whitelist();
	if (ret)
		return ret;

	usb_guard_block_wq = alloc_workqueue("usb_guard_block_wq",
						 WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!usb_guard_block_wq) {
		pr_err("%s: khong the tao workqueue deauthorize\n",
		       USB_GUARD_NAME);
		return -ENOMEM;
	}

	usb_guard_proc_dir = proc_mkdir(USB_GUARD_PROC_DIR, NULL);
	if (!usb_guard_proc_dir) {
		pr_err("%s: khong the tao /proc/%s\n", USB_GUARD_NAME,
		       USB_GUARD_PROC_DIR);
		usb_guard_destroy_block_wq();
		return -ENOMEM;
	}

	usb_guard_proc_status = proc_create_single_data(USB_GUARD_PROC_STATUS,
							0444,
							usb_guard_proc_dir,
							usb_guard_proc_show,
							NULL);
	if (!usb_guard_proc_status) {
		pr_err("%s: khong the tao /proc/%s/%s\n", USB_GUARD_NAME,
		       USB_GUARD_PROC_DIR, USB_GUARD_PROC_STATUS);
		usb_guard_remove_proc_entries();
		usb_guard_destroy_block_wq();
		return -ENOMEM;
	}

	usb_guard_proc_control = proc_create_data(USB_GUARD_PROC_CONTROL, 0644,
						 usb_guard_proc_dir,
						 &usb_guard_proc_control_ops,
						 NULL);
	if (!usb_guard_proc_control) {
		pr_err("%s: khong the tao /proc/%s/%s\n", USB_GUARD_NAME,
		       USB_GUARD_PROC_DIR, USB_GUARD_PROC_CONTROL);
		usb_guard_remove_proc_entries();
		usb_guard_destroy_block_wq();
		return -ENOMEM;
	}

	usb_register_notify(&usb_guard_usb_nb);

	ret = usb_guard_seed_existing_devices();
	if (ret) {
		pr_err("%s: khong the quet USB hien co (%d)\n",
		       USB_GUARD_NAME, ret);
		usb_unregister_notify(&usb_guard_usb_nb);
		usb_guard_remove_proc_entries();
		usb_guard_free_active_devices();
		usb_guard_free_history_devices();
		usb_guard_destroy_block_wq();
		return ret;
	}

	pr_info("%s: da nap module, policy_mode=%s whitelist=%s block_untrusted=%s\n",
		USB_GUARD_NAME, usb_guard_policy_mode(),
		usb_guard_whitelist_enabled ? "enabled" : "disabled",
		usb_guard_block_untrusted ? "enabled" : "disabled");

	return 0;
}

static void __exit usb_guard_exit(void)
{
	usb_unregister_notify(&usb_guard_usb_nb);
	usb_guard_destroy_block_wq();
	usb_guard_remove_proc_entries();
	usb_guard_free_active_devices();
	usb_guard_free_history_devices();
	pr_info("%s: da go module\n", USB_GUARD_NAME);
}

module_init(usb_guard_init);
module_exit(usb_guard_exit);

MODULE_AUTHOR("OpenAI Codex");
MODULE_DESCRIPTION("USB mass storage guard module with VID:PID[:SERIAL] whitelist");
MODULE_LICENSE("GPL");
