#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x888b8f57, "strcmp" },
	{ 0x73d975eb, "seq_write" },
	{ 0xcb971444, "seq_putc" },
	{ 0x40a621c5, "snprintf" },
	{ 0xbd03ed67, "random_kmalloc_seed" },
	{ 0xfaabfe5e, "kmalloc_caches" },
	{ 0xc064623f, "__kmalloc_cache_noprof" },
	{ 0x47886e07, "usb_unregister_notify" },
	{ 0x102ef4a9, "proc_remove" },
	{ 0x89b04984, "proc_mkdir" },
	{ 0x530282a0, "proc_create_single_data" },
	{ 0x47886e07, "usb_register_notify" },
	{ 0x1404c363, "usb_for_each_dev" },
	{ 0xce4af33b, "kstrdup" },
	{ 0x41495f0d, "strim" },
	{ 0xf00d45ac, "kstrtou16" },
	{ 0xa5c7582d, "strsep" },
	{ 0x296b9459, "strchr" },
	{ 0x0040afbe, "param_ops_charp" },
	{ 0xd272d446, "__fentry__" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x90a48d82, "__ubsan_handle_out_of_bounds" },
	{ 0xf46d5bf3, "mutex_lock" },
	{ 0xcb8b6ec6, "kfree" },
	{ 0xf46d5bf3, "mutex_unlock" },
	{ 0xe8213e80, "_printk" },
	{ 0xe4de56b4, "__ubsan_handle_load_invalid_value" },
	{ 0xbd03ed67, "__ref_stack_chk_guard" },
	{ 0x9479a1e8, "strnlen" },
	{ 0xd70733be, "sized_strscpy" },
	{ 0x2435d559, "strncmp" },
	{ 0x058c185a, "jiffies" },
	{ 0xe199f25f, "jiffies_to_msecs" },
	{ 0xe54e0a6b, "__fortify_panic" },
	{ 0xd272d446, "__stack_chk_fail" },
	{ 0xb61837ba, "seq_printf" },
	{ 0xbebe66ff, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0x888b8f57,
	0x73d975eb,
	0xcb971444,
	0x40a621c5,
	0xbd03ed67,
	0xfaabfe5e,
	0xc064623f,
	0x47886e07,
	0x102ef4a9,
	0x89b04984,
	0x530282a0,
	0x47886e07,
	0x1404c363,
	0xce4af33b,
	0x41495f0d,
	0xf00d45ac,
	0xa5c7582d,
	0x296b9459,
	0x0040afbe,
	0xd272d446,
	0xd272d446,
	0x90a48d82,
	0xf46d5bf3,
	0xcb8b6ec6,
	0xf46d5bf3,
	0xe8213e80,
	0xe4de56b4,
	0xbd03ed67,
	0x9479a1e8,
	0xd70733be,
	0x2435d559,
	0x058c185a,
	0xe199f25f,
	0xe54e0a6b,
	0xd272d446,
	0xb61837ba,
	0xbebe66ff,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"strcmp\0"
	"seq_write\0"
	"seq_putc\0"
	"snprintf\0"
	"random_kmalloc_seed\0"
	"kmalloc_caches\0"
	"__kmalloc_cache_noprof\0"
	"usb_unregister_notify\0"
	"proc_remove\0"
	"proc_mkdir\0"
	"proc_create_single_data\0"
	"usb_register_notify\0"
	"usb_for_each_dev\0"
	"kstrdup\0"
	"strim\0"
	"kstrtou16\0"
	"strsep\0"
	"strchr\0"
	"param_ops_charp\0"
	"__fentry__\0"
	"__x86_return_thunk\0"
	"__ubsan_handle_out_of_bounds\0"
	"mutex_lock\0"
	"kfree\0"
	"mutex_unlock\0"
	"_printk\0"
	"__ubsan_handle_load_invalid_value\0"
	"__ref_stack_chk_guard\0"
	"strnlen\0"
	"sized_strscpy\0"
	"strncmp\0"
	"jiffies\0"
	"jiffies_to_msecs\0"
	"__fortify_panic\0"
	"__stack_chk_fail\0"
	"seq_printf\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "A52772AD6744A1C106077B2");
