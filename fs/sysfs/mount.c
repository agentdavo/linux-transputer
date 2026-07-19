// SPDX-License-Identifier: GPL-2.0
/*
 * fs/sysfs/symlink.c - operations for initializing and mounting sysfs
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.rst for more information.
 */

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include <net/net_namespace.h>

#include "sysfs.h"
#include "../kernfs/kernfs-internal.h"

static struct kernfs_root *sysfs_root;
struct kernfs_node *sysfs_root_kn;

static void sysfs_fs_context_free(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;

	if (kfc->ns_tag)
		kobj_ns_drop(KOBJ_NS_TYPE_NET, kfc->ns_tag);
	kernfs_free_fs_context(fc);
	kfree(kfc);
}

static const struct fs_context_operations sysfs_fs_context_ops = {
	.free		= sysfs_fs_context_free,
	.get_tree	= kernfs_get_tree,
};

static int sysfs_init_fs_context(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc;
	struct ns_common *ns;

	if (!(fc->sb_flags & SB_KERNMOUNT)) {
		if (!kobj_ns_current_may_mount(KOBJ_NS_TYPE_NET))
			return -EPERM;
	}

	kfc = kzalloc_obj(struct kernfs_fs_context);
	if (!kfc)
		return -ENOMEM;

	kfc->ns_tag = ns = kobj_ns_grab_current(KOBJ_NS_TYPE_NET);
	kfc->root = sysfs_root;
	kfc->magic = SYSFS_MAGIC;
	fc->fs_private = kfc;
	fc->ops = &sysfs_fs_context_ops;
	if (ns) {
		struct net *netns = to_net_ns(ns);

		put_user_ns(fc->user_ns);
		fc->user_ns = get_user_ns(netns->user_ns);
	}
	fc->global = true;
	return 0;
}

static void sysfs_kill_sb(struct super_block *sb)
{
	struct ns_common *ns = (struct ns_common *)kernfs_super_ns(sb);

	kernfs_kill_sb(sb);
	kobj_ns_drop(KOBJ_NS_TYPE_NET, ns);
}

static struct file_system_type sysfs_fs_type = {
	.name			= "sysfs",
	.init_fs_context	= sysfs_init_fs_context,
	.kill_sb		= sysfs_kill_sb,
	.fs_flags		= FS_USERNS_MOUNT | FS_USERNS_MOUNT_RESTRICTED,
};

int __init sysfs_init(void)
{
	int err;

	{
		const char *s = "S0\n";
		while (*s)
			*(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
	}
	sysfs_root = kernfs_create_root(NULL, KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK,
					NULL);
	{
		const char *s = "S1\n";
		while (*s)
			*(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
	}
	if (IS_ERR(sysfs_root))
		return PTR_ERR(sysfs_root);

	sysfs_root_kn = kernfs_root_to_node(sysfs_root);

	{
		const char *s = "S2\n";
		while (*s)
			*(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
	}
	err = register_filesystem(&sysfs_fs_type);
	{
		const char *s = "S3\n";
		int sh;
		unsigned int e = (unsigned int)err;
		while (*s)
			*(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
		*(volatile unsigned int *)0x10000000u = 'e';
		*(volatile unsigned int *)0x10000000u = '=';
		*(volatile unsigned int *)0x10000000u = '0';
		*(volatile unsigned int *)0x10000000u = 'x';
		for (sh = 28; sh >= 0; sh -= 4) {
			unsigned int d = (e >> sh) & 0xfu;
			*(volatile unsigned int *)0x10000000u =
				(unsigned int)(d < 10u ? '0' + d : 'a' + d - 10u);
		}
		*(volatile unsigned int *)0x10000000u = '\n';
	}
	if (err) {
		{
			const char *s = "SEfail\n";
			while (*s)
				*(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
		}
		kernfs_destroy_root(sysfs_root);
		return err;
	}

	/* Dump root rwsem count after create+activate+register */
	{
		unsigned int c = (unsigned int)atomic_long_read(&sysfs_root->kernfs_rwsem.count);
		unsigned int p = (unsigned int)(unsigned long)sysfs_root;
		int sh;
		const char *s = "SC";
		while (*s)
			*(volatile unsigned int *)0x10000000u = (unsigned char)*s++;
		*(volatile unsigned int *)0x10000000u = '0';
		*(volatile unsigned int *)0x10000000u = 'x';
		for (sh = 28; sh >= 0; sh -= 4) {
			unsigned int d = (c >> sh) & 0xfu;
			*(volatile unsigned int *)0x10000000u =
				(unsigned int)(d < 10u ? '0' + d : 'a' + d - 10u);
		}
		*(volatile unsigned int *)0x10000000u = '@';
		*(volatile unsigned int *)0x10000000u = '0';
		*(volatile unsigned int *)0x10000000u = 'x';
		for (sh = 28; sh >= 0; sh -= 4) {
			unsigned int d = (p >> sh) & 0xfu;
			*(volatile unsigned int *)0x10000000u =
				(unsigned int)(d < 10u ? '0' + d : 'a' + d - 10u);
		}
		*(volatile unsigned int *)0x10000000u = '\n';
	}

	return 0;
}
