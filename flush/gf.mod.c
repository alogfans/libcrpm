#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x64491f21, "module_layout" },
	{ 0x7da3900c, "class_unregister" },
	{ 0xd6de966e, "device_destroy" },
	{ 0x1dbd1ee9, "class_destroy" },
	{ 0x69512d2b, "device_create" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0x303b348c, "__class_create" },
	{ 0x2d5589da, "__register_chrdev" },
	{ 0x8f2703b7, "wbinvd_on_all_cpus" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "F4E7ED8726236AFBE673CF0");
