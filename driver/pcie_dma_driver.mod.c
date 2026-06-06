#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
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

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x25f8bfc1, "module_layout" },
	{ 0xd1ba9b3c, "pci_unregister_driver" },
	{ 0x755b420c, "__pci_register_driver" },
	{ 0xa6257a2f, "complete" },
	{ 0xdcb764ad, "memset" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x908e5601, "cpu_hwcaps" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x89940875, "mutex_lock_interruptible" },
	{ 0x89c9a638, "_dev_err" },
	{ 0x69f38847, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0x4a3ad70e, "wait_for_completion_timeout" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x65f1e662, "sysfs_create_group" },
	{ 0x396ecf2f, "device_create" },
	{ 0xeab3563e, "__class_create" },
	{ 0xd5a0871d, "cdev_add" },
	{ 0x5d7a3971, "cdev_init" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0x18a235d6, "pci_enable_msi" },
	{ 0x8ec7dbb2, "dma_alloc_attrs" },
	{ 0xf1d49b35, "pci_iomap" },
	{ 0xa841a81e, "pci_request_regions" },
	{ 0x18aac8af, "dma_set_coherent_mask" },
	{ 0x39513822, "dma_set_mask" },
	{ 0x80c0af65, "pci_set_master" },
	{ 0x2debd5a1, "pci_enable_device" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x4e5b36a5, "devm_kmalloc" },
	{ 0x4b0a3f52, "gic_nonsecure_priorities" },
	{ 0x5821f3a1, "pci_disable_device" },
	{ 0xee2b8603, "pci_release_regions" },
	{ 0x9b4f6cce, "pci_iounmap" },
	{ 0xdf204797, "dma_free_attrs" },
	{ 0xd6651aa5, "pci_disable_msi" },
	{ 0xc1514a3b, "free_irq" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x277c232f, "cdev_del" },
	{ 0x8cb3f82a, "class_destroy" },
	{ 0xcd9f81af, "device_destroy" },
	{ 0x57d303c4, "sysfs_remove_group" },
	{ 0xe783e261, "sysfs_emit" },
	{ 0x85fd2dbe, "_dev_info" },
	{ 0x1fdc7df2, "_mcount" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v00001234d0000A100sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "E2AA6DA9DCADE8ABD0088A0");
