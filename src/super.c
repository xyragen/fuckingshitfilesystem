#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/crc32c.h>
static struct kmem_cache *fsfs_inode_cachep;
static struct inode *fsfs_alloc_inode(struct super_block *sb) {
  struct inode *inode = alloc_inode_sb(sb, fsfs_inode_cachep, GFP_NOFS);
  return inode;
}
static void fsfs_free_inode(struct inode *inode) {
  kmem_cache_free(fsfs_inode_cachep, inode);
}
static int fsfs_sync_fs(struct super_block *sb, int wait) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u8 blk[FSFS_BLOCK];
  __u32 c;
  memset(blk, 0, FSFS_BLOCK);
  sbi->super.crc_super = 0;
  c = fsfs_crc32c(&sbi->super, sizeof(fsfs_super_t));
  sbi->super.crc_super = c;
  memcpy(blk, &sbi->super, sizeof(fsfs_super_t));
  fsfs_blk_write(sb, 0, blk);
  fsfs_blk_write(sb, 1, sbi->blk_bmp);
  sync_blockdev(sb->s_bdev);
  return 0;
}
static int fsfs_statfs(struct dentry *dentry, struct kstatfs *buf) {
  struct super_block *sb = dentry->d_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  buf->f_type = FSFS_MAGIC;
  buf->f_bsize = FSFS_BLOCK;
  buf->f_blocks = sbi->super.total_blocks;
  buf->f_bfree = sbi->super.free_blocks;
  buf->f_bavail = sbi->super.free_blocks;
  buf->f_files = FSFS_MAX_INODES;
  buf->f_ffree = sbi->super.free_blocks;
  buf->f_namelen = FSFS_NAME_MAX;
  return 0;
}
static const struct super_operations fsfs_sops = {
  .alloc_inode = fsfs_alloc_inode,
  .free_inode = fsfs_free_inode,
  .sync_fs = fsfs_sync_fs,
  .statfs = fsfs_statfs,
};
void fsfs_fill_inode(struct super_block *sb, struct inode *inode) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x = &sbi->inodes[inode->i_ino];
  inode->i_mode = x->mode;
  if (x->is_dir) inode->i_mode |= S_IFDIR;
  else inode->i_mode |= S_IFREG;
  i_uid_write(inode, x->uid);
  i_gid_write(inode, x->gid);
  inode->i_size = x->size;
  set_nlink(inode, x->nlink ? x->nlink : 1);
  inode_set_atime(inode, x->atime, 0);
  inode_set_mtime(inode, x->mtime, 0);
  inode_set_ctime(inode, x->ctime, 0);
  inode->i_blkbits = sb->s_blocksize_bits;
  if (x->is_dir) {
    inode->i_op = &fsfs_dir_inode_ops;
    inode->i_fop = &fsfs_dir_ops;
  } else {
    inode->i_op = &fsfs_file_inode_ops;
    inode->i_fop = &fsfs_file_ops;
  }
}
struct inode *fsfs_iget(struct super_block *sb, __u64 ino) {
  struct inode *inode;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (ino >= FSFS_MAX_INODES) return ERR_PTR(-ESTALE);
  inode = iget_locked(sb, ino);
  if (!inode) return ERR_PTR(-ENOMEM);
  if (!(inode_state_read_once(inode) & I_NEW)) return inode;
  if (sbi->inodes[ino].ino == 0) {
    iget_failed(inode);
    return ERR_PTR(-ESTALE);
  }
  fsfs_fill_inode(sb, inode);
  unlock_new_inode(inode);
  return inode;
}
int fsfs_load_inodes(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u8 *raw;
  int i;
  raw = kvzalloc(sizeof(fsfs_inode_t) * FSFS_MAX_INODES, GFP_NOFS);
  if (!raw) return -1;
  for (i = 0; i < 64; i++) {
    fsfs_blk_read(sb, 8 + i, raw + i * FSFS_BLOCK);
  }
  sbi->inodes = (fsfs_inode_t *)raw;
  return 0;
}

static int fsfs_fill_super(struct super_block *sb, struct fs_context *fc) {
  struct fsfs_sb_info *sbi;
  __u8 blk[FSFS_BLOCK];
  fsfs_super_t *s;
  __u32 want;
  __u32 got;
  __u64 tb;
  struct dentry *root;
  sbi = kzalloc(sizeof(*sbi), GFP_NOFS);
  if (!sbi) return -ENOMEM;
  sb->s_fs_info = sbi;
  mutex_init(&sbi->lock);
  sb_set_blocksize(sb, FSFS_BLOCK);
  if (fsfs_blk_read(sb, 0, blk) != 0) return -EIO;
  s = (fsfs_super_t *)blk;
  if (s->magic != FSFS_MAGIC) return -EINVAL;
  memcpy(&sbi->super, s, sizeof(fsfs_super_t));
  want = sbi->super.crc_super;
  sbi->super.crc_super = 0;
  got = fsfs_crc32c(&sbi->super, sizeof(fsfs_super_t));
  sbi->super.crc_super = want;
  if (got != want) return -EINVAL;
  tb = sbi->super.total_blocks;
  sbi->blk_bmp = kvzalloc(tb, GFP_NOFS);
  sbi->blk_ref = kvzalloc(tb * sizeof(__u32), GFP_NOFS);
  sbi->snaps = kvzalloc(sizeof(fsfs_snap_t) * FSFS_MAX_SNAPS, GFP_NOFS);
  sbi->journal = kvzalloc(FSFS_BLOCK * FSFS_JOURNAL_BLOCKS, GFP_NOFS);
  if (!sbi->blk_bmp || !sbi->blk_ref || !sbi->snaps || !sbi->journal) return -ENOMEM;
  if (fsfs_blk_read(sb, 1, sbi->blk_bmp) != 0) return -EIO;
  {
    __u64 i;
    for (i = 0; i < tb; i++) sbi->blk_ref[i] = sbi->blk_bmp[i] ? 1 : 0;
  }
  if (fsfs_load_inodes(sb) != 0) return -EIO;
  sbi->cur_gen = sbi->super.next_gen + 1;
  fsfs_journal_replay(sb);
  sb->s_magic = FSFS_MAGIC;
  sb->s_op = &fsfs_sops;
  sb->s_maxbytes = 1ULL << 40;
  sb->s_time_gran = 1;
  root = d_make_root(fsfs_iget(sb, sbi->super.root_ino));
  if (!root) return -ENOMEM;
  sb->s_root = root;
  return 0;
}
static int fsfs_get_tree(struct fs_context *fc) {
  return get_tree_bdev(fc, fsfs_fill_super);
}
static int fsfs_parse_param(struct fs_context *fc, struct fs_parameter *param) {
  return 0;
}
static const struct fs_context_operations fsfs_ctx_ops = {
  .get_tree = fsfs_get_tree,
  .parse_param = fsfs_parse_param,
};
static int fsfs_init_fs_context(struct fs_context *fc) {
  fc->ops = &fsfs_ctx_ops;
  return 0;
}
static struct file_system_type fsfs_fs_type = {
  .name = "fsfs",
  .init_fs_context = fsfs_init_fs_context,
  .kill_sb = kill_block_super,
  .fs_flags = FS_REQUIRES_DEV,
};
static void fsfs_inode_init_once(void *obj) {
  struct inode *inode = obj;
  inode_init_once(inode);
}
static int __init fsfs_init(void) {
  int r;
  fsfs_inode_cachep = kmem_cache_create("fsfs_inode_cache",
    sizeof(struct inode), 0, SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
    fsfs_inode_init_once);
  if (!fsfs_inode_cachep) return -ENOMEM;
  r = register_filesystem(&fsfs_fs_type);
  if (r) {
    kmem_cache_destroy(fsfs_inode_cachep);
    return r;
  }
  return 0;
}
static void __exit fsfs_exit(void) {
  unregister_filesystem(&fsfs_fs_type);
  kmem_cache_destroy(fsfs_inode_cachep);
}
module_init(fsfs_init);
module_exit(fsfs_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("fsfs");
MODULE_DESCRIPTION("fsfs");
#endif
