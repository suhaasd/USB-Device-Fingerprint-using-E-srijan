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
	{ 0x80222ceb, "proc_create" },
	{ 0x02f9bbf0, "timer_init_key" },
	{ 0x47886e07, "usb_register_notify" },
	{ 0xbeb1d261, "destroy_workqueue" },
	{ 0xa8f96c6e, "usb_deregister" },
	{ 0xf46d5bf3, "mutex_lock" },
	{ 0xf46d5bf3, "mutex_unlock" },
	{ 0xe931a49e, "single_open" },
	{ 0x73d975eb, "seq_write" },
	{ 0xb61837ba, "seq_printf" },
	{ 0x49733ad6, "queue_work_on" },
	{ 0xbd03ed67, "random_kmalloc_seed" },
	{ 0xfaabfe5e, "kmalloc_caches" },
	{ 0xc064623f, "__kmalloc_cache_noprof" },
	{ 0x6c00f410, "usb_control_msg" },
	{ 0x47886e07, "usb_unregister_notify" },
	{ 0x2352b148, "timer_delete_sync" },
	{ 0xbeb1d261, "drain_workqueue" },
	{ 0xc0f19660, "remove_proc_entry" },
	{ 0x67628f51, "msleep" },
	{ 0xcb8b6ec6, "kfree" },
	{ 0x9479a1e8, "strnlen" },
	{ 0xd70733be, "sized_strscpy" },
	{ 0xe54e0a6b, "__fortify_panic" },
	{ 0xaa9a3b35, "seq_read" },
	{ 0x253f0c1d, "seq_lseek" },
	{ 0x34d5450c, "single_release" },
	{ 0xd272d446, "__fentry__" },
	{ 0x058c185a, "jiffies" },
	{ 0xe1e1f979, "_raw_spin_lock_irqsave" },
	{ 0xe8213e80, "_printk" },
	{ 0x81a1a811, "_raw_spin_unlock_irqrestore" },
	{ 0x32feeafc, "mod_timer" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x90a48d82, "__ubsan_handle_out_of_bounds" },
	{ 0xdf4bee3d, "alloc_workqueue_noprof" },
	{ 0xadb55ac9, "usb_register_driver" },
	{ 0xbebe66ff, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0x80222ceb,
	0x02f9bbf0,
	0x47886e07,
	0xbeb1d261,
	0xa8f96c6e,
	0xf46d5bf3,
	0xf46d5bf3,
	0xe931a49e,
	0x73d975eb,
	0xb61837ba,
	0x49733ad6,
	0xbd03ed67,
	0xfaabfe5e,
	0xc064623f,
	0x6c00f410,
	0x47886e07,
	0x2352b148,
	0xbeb1d261,
	0xc0f19660,
	0x67628f51,
	0xcb8b6ec6,
	0x9479a1e8,
	0xd70733be,
	0xe54e0a6b,
	0xaa9a3b35,
	0x253f0c1d,
	0x34d5450c,
	0xd272d446,
	0x058c185a,
	0xe1e1f979,
	0xe8213e80,
	0x81a1a811,
	0x32feeafc,
	0xd272d446,
	0x90a48d82,
	0xdf4bee3d,
	0xadb55ac9,
	0xbebe66ff,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"proc_create\0"
	"timer_init_key\0"
	"usb_register_notify\0"
	"destroy_workqueue\0"
	"usb_deregister\0"
	"mutex_lock\0"
	"mutex_unlock\0"
	"single_open\0"
	"seq_write\0"
	"seq_printf\0"
	"queue_work_on\0"
	"random_kmalloc_seed\0"
	"kmalloc_caches\0"
	"__kmalloc_cache_noprof\0"
	"usb_control_msg\0"
	"usb_unregister_notify\0"
	"timer_delete_sync\0"
	"drain_workqueue\0"
	"remove_proc_entry\0"
	"msleep\0"
	"kfree\0"
	"strnlen\0"
	"sized_strscpy\0"
	"__fortify_panic\0"
	"seq_read\0"
	"seq_lseek\0"
	"single_release\0"
	"__fentry__\0"
	"jiffies\0"
	"_raw_spin_lock_irqsave\0"
	"_printk\0"
	"_raw_spin_unlock_irqrestore\0"
	"mod_timer\0"
	"__x86_return_thunk\0"
	"__ubsan_handle_out_of_bounds\0"
	"alloc_workqueue_noprof\0"
	"usb_register_driver\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");

MODULE_ALIAS("usb:v16C0p05DCd*dc*dsc*dp*ic*isc*ip*in*");

MODULE_INFO(srcversion, "A85ECDBB179117B9B688966");
