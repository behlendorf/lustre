/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  lustre/smfs/inode.c
 *  Lustre filesystem abstraction routines
 *
 *  Copyright (C) 2004 Cluster File Systems, Inc.
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define DEBUG_SUBSYSTEM S_SM

#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/obd_class.h>
#include <linux/obd_support.h>
#include <linux/lustre_lib.h>
#include <linux/lustre_idl.h>
#include <linux/lustre_fsfilt.h>
#include <linux/lustre_smfs.h>
#include "smfs_internal.h"
static void smfs_init_cache_inode (struct inode *inode, void *opaque)
{
        struct smfs_inode_info *sm_info = (struct smfs_inode_info *)opaque;
        struct inode *cache_inode = NULL;
 
        cache_inode = iget(S2CSB(inode->i_sb), inode->i_ino);
        
        if (sm_info) 
                memcpy(I2SMI(inode), sm_info, sizeof(struct smfs_inode_info)); 
        
        I2CI(inode) = cache_inode;
        post_smfs_inode(inode, cache_inode);
        sm_set_inode_ops(cache_inode, inode);
#if CONFIG_SNAPFS
        if (sm_info) {
                smfs_init_snap_inode_info(inode, &sm_info->sm_sninfo);
        }
#endif
}

static void smfs_read_inode2(struct inode *inode, void *opaque)
{
        ENTRY;
        if (!inode)
                return;

        CDEBUG(D_INODE, "read_inode ino %lu\n", inode->i_ino);
        smfs_init_cache_inode(inode, opaque);
        CDEBUG(D_INODE, "read_inode ino %lu icount %d \n",
               inode->i_ino, atomic_read(&inode->i_count));
        EXIT;
        return;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static int smfs_test_inode(struct inode *inode, unsigned long ino, 
                                  void *opaque)
#else
static int smfs_test_inode(struct inode *inode, void *opaque)
#endif
{
        struct snap_inode_info *sn_info = &I2SMI(inode)->sm_sninfo;
        struct snap_inode_info *test_info = (struct snap_inode_info *)opaque;
       
        if (!sn_info || !test_info)
                return 0;
        if (sn_info->sn_index != test_info->sn_index)
                return 0;
        if (sn_info->sn_gen != test_info->sn_gen)
                return 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
        smfs_init_cache_inode(inode, opaque);
#endif
        return 1;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
int smfs_set_inode(struct inode *inode, void *opaque)
{
        smfs_read_inode2(inode, opaque);
        return 0;
}

struct inode *smfs_iget(struct super_block *sb, ino_t hash,
                        struct smfs_inode_info *si_info)
{
        struct inode *inode;

        LASSERT(hash != 0);
        inode = iget5_locked(sb, hash, smfs_test_inode, smfs_set_inode, 
                             si_info);

        if (inode) {
                if (inode->i_state & I_NEW)
                        unlock_new_inode(inode);
                CDEBUG(D_VFSTRACE, "inode: %lu/%u(%p)\n", inode->i_ino,
                       inode->i_generation, inode);
        }
        return inode;
}
#else
struct inode *smfs_iget(struct super_block *sb, ino_t hash,
                        struct smfs_inode_info *si_info)
{
        struct inode *inode;
        LASSERT(hash != 0);

        inode = iget4(sb, hash, smfs_test_inode, si_info);

        if (inode)
                CDEBUG(D_VFSTRACE, "inode: %lu/%u(%p)\n", inode->i_ino,
                       inode->i_generation, inode);
        return inode;
}
#endif
struct inode *smfs_get_inode (struct super_block *sb, ino_t hash,
                              struct inode *dir, int index)
{
        struct smfs_inode_info sm_info;
        struct inode *inode = NULL;       
        ENTRY;
        
        sm_info.smi_flags = I2SMI(dir)->smi_flags;
        sm_info.sm_sninfo.sn_flags = I2SMI(dir)->sm_sninfo.sn_flags;
        sm_info.sm_sninfo.sn_index = index;
        sm_info.sm_sninfo.sn_gen = I2SMI(dir)->sm_sninfo.sn_gen;
        inode = smfs_iget(sb, hash, &sm_info);

        RETURN(inode);
} 
EXPORT_SYMBOL(smfs_get_inode);
static void smfs_delete_inode(struct inode *inode)
{
        struct inode *cache_inode;

        ENTRY;
        cache_inode = I2CI(inode);

        if (!cache_inode || !S2CSB(inode->i_sb))
                return;

        /* FIXME-WANGDI: because i_count of cache_inode may not be 0 or 1 in
         * before smfs_delete inode, So we need to dec it to 1 before we call
         * delete_inode of the bellow cache filesystem Check again latter. */

        if (atomic_read(&cache_inode->i_count) < 1)
                BUG();

        while (atomic_read(&cache_inode->i_count) != 1)
                atomic_dec(&cache_inode->i_count);

        pre_smfs_inode(inode, cache_inode);

        if (atomic_read(&cache_inode->i_count) < 1)
                LBUG();

        while (atomic_read(&cache_inode->i_count) != 1) {
                atomic_dec(&cache_inode->i_count);
        }

        pre_smfs_inode(inode, cache_inode);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
        list_del(&cache_inode->i_hash);
        INIT_LIST_HEAD(&cache_inode->i_hash);
#else
        hlist_del_init(&cache_inode->i_hash);
#endif

        list_del(&cache_inode->i_list);
        INIT_LIST_HEAD(&cache_inode->i_list);

        if (cache_inode->i_data.nrpages)
                truncate_inode_pages(&cache_inode->i_data, 0);

        if (S2CSB(inode->i_sb)->s_op->delete_inode)
                S2CSB(inode->i_sb)->s_op->delete_inode(cache_inode);

        post_smfs_inode(inode, cache_inode);

        I2CI(inode) = NULL;
        return;
}

static void smfs_write_inode(struct inode *inode, int wait)
{
        struct inode *cache_inode;

        ENTRY;
        cache_inode = I2CI(inode);

        if (!cache_inode) {
                CWARN("cache inode null\n");
                return;
        }
        pre_smfs_inode(inode, cache_inode);
        if (S2CSB(inode->i_sb)->s_op->write_inode)
                S2CSB(inode->i_sb)->s_op->write_inode(cache_inode, wait);
        
        post_smfs_inode(inode, cache_inode);
        EXIT;
}

static void smfs_dirty_inode(struct inode *inode)
{
        struct inode *cache_inode;

        ENTRY;
        cache_inode = I2CI(inode);

        if (!cache_inode || !S2CSB(inode->i_sb))
                return;

        pre_smfs_inode(inode, cache_inode);
        if (S2CSB(inode->i_sb)->s_op->dirty_inode)
                S2CSB(inode->i_sb)->s_op->dirty_inode(cache_inode);

        post_smfs_inode(inode, cache_inode);
        EXIT;
}

static void smfs_put_inode(struct inode *inode)
{
        struct inode *cache_inode;

        ENTRY;
        cache_inode = I2CI(inode);

        if (!cache_inode) {
                CWARN("cache inode null\n");
                return;
        }
        
        if (atomic_read(&cache_inode->i_count) > 1 && 
            cache_inode != cache_inode->i_sb->s_root->d_inode)
                iput(cache_inode);
        
        if (S2CSB(inode->i_sb)->s_op->put_inode)
                S2CSB(inode->i_sb)->s_op->put_inode(cache_inode);
        
        EXIT;
}

static void smfs_write_super(struct super_block *sb)
{
        ENTRY;

        if (!S2CSB(sb))
                return;

        if (S2CSB(sb)->s_op->write_super)
                S2CSB(sb)->s_op->write_super(S2CSB(sb));
        duplicate_sb(sb, S2CSB(sb));
        EXIT;
        return;
}

static void smfs_clear_inode(struct inode *inode)
{
        struct inode *cache_inode;

        ENTRY;

        if (!inode) return;

        cache_inode = I2CI(inode);

        if (cache_inode != cache_inode->i_sb->s_root->d_inode) {
                iput(cache_inode);
                SMFS_CLEAN_INODE_REC(inode);
                I2CI(inode) = NULL;
        }
        EXIT;
        return;
}

static void smfs_write_super_lockfs(struct super_block *sb)
{
        struct super_block *cache_sb;
        ENTRY;

        cache_sb = S2CSB(sb);
        if (!cache_sb)
                return;

        if (cache_sb->s_op->write_super_lockfs)
                cache_sb->s_op->write_super_lockfs(cache_sb);

        duplicate_sb(sb, cache_sb);
        EXIT;
}

static void smfs_unlockfs(struct super_block *sb)
{
        struct super_block *cache_sb;
        ENTRY;

        cache_sb = S2CSB(sb);
        if (!cache_sb)
                return;

        if (cache_sb->s_op->unlockfs)
                cache_sb->s_op->unlockfs(cache_sb);

        duplicate_sb(sb, cache_sb);
        EXIT;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static int smfs_statfs(struct super_block *sb, struct statfs *buf)
#else
static int smfs_statfs(struct super_block *sb, struct kstatfs *buf)
#endif
{
        struct super_block *cache_sb;
        int rc = 0;
        ENTRY;

        cache_sb = S2CSB(sb);
        if (!cache_sb)
                RETURN(-EINVAL);

        if (cache_sb->s_op->statfs)
                rc = cache_sb->s_op->statfs(cache_sb, buf);

        duplicate_sb(sb, cache_sb);

        RETURN(rc);
}
static int smfs_remount(struct super_block *sb, int *flags, char *data)
{
        struct super_block *cache_sb;
        int rc = 0;
        ENTRY;

        cache_sb = S2CSB(sb);

        if (!cache_sb)
                RETURN(-EINVAL);

        if (cache_sb->s_op->remount_fs)
                rc = cache_sb->s_op->remount_fs(cache_sb, flags, data);

        duplicate_sb(sb, cache_sb);
        RETURN(rc);
}
struct super_operations smfs_super_ops = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
        .read_inode2        = smfs_read_inode2,
#endif 
        .clear_inode        = smfs_clear_inode,
        .put_super          = smfs_put_super,
        .delete_inode       = smfs_delete_inode,
        .write_inode        = smfs_write_inode,
        .dirty_inode        = smfs_dirty_inode, /* BKL not held. */
        .put_inode          = smfs_put_inode,   /* BKL not held. */
        .write_super        = smfs_write_super, /* BKL held */
        .write_super_lockfs = smfs_write_super_lockfs, /* BKL not held. */
        .unlockfs           = smfs_unlockfs,    /* BKL not held. */
        .statfs             = smfs_statfs,      /* BKL held */
        .remount_fs         = smfs_remount,     /* BKL held */
};

int is_smfs_sb(struct super_block *sb)
{
        return (sb->s_op->put_super == smfs_super_ops.put_super);
}
EXPORT_SYMBOL(is_smfs_sb);
