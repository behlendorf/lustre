/* object based disk file system
 * 
 * This code is issued under the GNU General Public License.
 * See the file COPYING in this distribution
 * 
 * Copyright (C), 1999, Stelias Computing Inc
 *
 *
 */


#ifndef _LL_H
#define _LL_H
#include <linux/obd_class.h>
#include <linux/obdo.h>
#include <linux/list.h>
#include <linux/lustre_net.h>

#define LL_SUPER_MAGIC 0x0BD00BD0;

#define LL_INLINESZ      60
struct ll_inode_info {
        int              lli_flags;
	__u64            lli_objid; 
        char             lli_inline[LL_INLINESZ];
};

struct ll_sb_info {
        struct list_head         ll_list;      /* list of supers */
        struct obd_conn          ll_conn;
        struct super_block      *ll_super;
	//        struct obd_device       *ll_obd;
        //struct obd_ops          *ll_ops;
        ino_t                    ll_rootino;   /* number of root inode */
        int                      ll_minor;     /* minor of /dev/obdX */
        struct list_head         ll_inodes;    /* list of dirty inodes */
        unsigned long            ll_cache_count;
        struct semaphore         ll_list_mutex;
	struct lustre_peer       ll_peer;
	struct lustre_peer      *ll_peer_ptr;
};


static inline struct ll_inode_info *ll_i2info(struct inode *inode)
{
        return (struct ll_inode_info *)&(inode->u.generic_ip);
}

static inline int ll_has_inline(struct inode *inode)
{
        return (ll_i2info(inode)->lli_flags & OBD_FL_INLINEDATA);
}







/* super.c */ 
struct ll_pgrq {
        struct list_head         rq_plist;      /* linked list of req's */
        unsigned long            rq_jiffies;
        struct page             *rq_page;       /* page to be written */
};

extern struct list_head ll_super_list;       /* list of all LL superblocks */



/* dir.c */
#define EXT2_DIR_PAD                    4
#define EXT2_DIR_ROUND                  (EXT2_DIR_PAD - 1)
#define EXT2_DIR_REC_LEN(name_len)      (((name_len) + 8 + EXT2_DIR_ROUND) & \
                                         ~EXT2_DIR_ROUND)
#define EXT2_NAME_LEN 255

int ll_check_dir_entry (const char * function, struct inode * dir,
                          struct ext2_dir_entry_2 * de, struct page * page,
                          unsigned long offset);
extern struct file_operations ll_dir_operations;
extern struct inode_operations ll_dir_inode_operations;

/* file.c */
extern struct file_operations ll_file_operations;
extern struct inode_operations ll_file_inode_operations;

/* flush.c */
void ll_dequeue_pages(struct inode *inode);
int ll_flushd_init(void);
int ll_flushd_cleanup(void);
int ll_flush_reqs(struct list_head *inode_list, unsigned long check_time);
int ll_flush_dirty_pages(unsigned long check_time);

/* namei.c */
/*
 * Structure of the super block
 */


#define EXT2_SB(sb)     (&((sb)->u.ext2_sb))
/*
 * Maximal count of links to a file
 */
#define EXT2_LINK_MAX           32000
/*
 * Ext2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define EXT2_FT_UNKNOWN         0
#define EXT2_FT_REG_FILE        1
#define EXT2_FT_DIR             2
#define EXT2_FT_CHRDEV          3
#define EXT2_FT_BLKDEV          4
#define EXT2_FT_FIFO            5
#define EXT2_FT_SOCK            6
#define EXT2_FT_SYMLINK         7

#define EXT2_FT_MAX             8

#define EXT2_BTREE_FL                   0x00001000 /* btree format dir */
#define EXT2_RESERVED_FL                0x80000000 /* reserved for ext2 lib */
#define EXT2_FEATURE_INCOMPAT_FILETYPE          0x0002
#define EXT2_HAS_COMPAT_FEATURE(sb,mask)                        \
        ( EXT2_SB(sb)->s_es->s_feature_compat & cpu_to_le32(mask) )
#define EXT2_HAS_INCOMPAT_FEATURE(sb,mask)                      \
        ( EXT2_SB(sb)->s_es->s_feature_incompat & cpu_to_le32(mask) )

/* rw.c */
int ll_do_writepage(struct page *, int sync);
int ll_init_pgrqcache(void);
void ll_cleanup_pgrqcache(void);
inline void ll_pgrq_del(struct ll_pgrq *pgrq);
int ll_readpage(struct file *file, struct page *page);
int ll_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to);
int ll_commit_write(struct file *file, struct page *page, unsigned from, unsigned to);
int ll_writepage(struct page *page);
struct page *ll_getpage(struct inode *inode, unsigned long offset,
                           int create, int locked);
int ll_write_one_page(struct file *file, struct page *page,
                         unsigned long offset, unsigned long bytes,
                         const char * buf);
int ll_do_vec_wr(struct inode **inodes, obd_count num_io, obd_count num_oa,
                    struct obdo **obdos, obd_count *oa_bufs,
                    struct page **pages, char **bufs, obd_size *counts,
                    obd_off *offsets, obd_flag *flags);
void ll_truncate(struct inode *inode);

/* super.c */
extern long ll_cache_count;
extern long ll_mutex_start;

/* symlink.c */
extern struct inode_operations ll_fast_symlink_inode_operations;
extern struct inode_operations ll_symlink_inode_operations;

/* sysctl.c */
void ll_sysctl_init(void);
void ll_sysctl_clean(void);

static inline struct ll_sb_info *ll_i2sbi(struct inode *inode)
{
        return (struct ll_sb_info *) &(inode->i_sb->u.generic_sbp);
}


static inline struct list_head *ll_slist(struct inode *inode) 
{
        struct ll_sb_info *sbi = ll_i2sbi(inode);

        return &sbi->ll_inodes;
}

static void inline ll_set_size (struct inode *inode, obd_size size)
{  
       inode->i_size = size;
       inode->i_blocks = (inode->i_size + inode->i_sb->s_blocksize - 1) >>
               inode->i_sb->s_blocksize_bits;
} /* ll_set_size */



#define obd_down(mutex) {                                               \
        /* CDEBUG(D_INFO, "get lock\n"); */                             \
        ll_mutex_start = jiffies;                                    \
        down(mutex);                                                    \
        if (jiffies - ll_mutex_start)                                \
                CDEBUG(D_CACHE, "waited on mutex %ld jiffies\n",        \
                       jiffies - ll_mutex_start);                    \
}

#define obd_up(mutex) {                                                 \
        up(mutex);                                                      \
        if (jiffies - ll_mutex_start > 1)                            \
                CDEBUG(D_CACHE, "held mutex for %ld jiffies\n",         \
                       jiffies - ll_mutex_start);                    \
        /* CDEBUG(D_INFO, "free lock\n"); */                            \
}

/* We track if a page has been added to the OBD page cache by stting a
 * flag on the page.  We have chosen a bit that will hopefully not be
 * used for a while.
 */
#define PG_obdcache 29
#define OBDAddCachePage(page)   test_and_set_bit(PG_obdcache, &(page)->flags)
#define OBDClearCachePage(page) clear_bit(PG_obdcache, &(page)->flags)

#endif

