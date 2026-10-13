#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/namei.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
static struct dentry *fsfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
  struct super_block *sb = dir->i_sb;
  __u64 child = 0;
  __u8 type = 0;
  struct inode *inode = NULL;
  char *name = (char *)dentry->d_name.name;
  if (dentry->d_name.len > FSFS_NAME_MAX) return ERR_PTR(-ENAMETOOLONG);
  if (fsfs_btree_lookup(sb, dir->i_ino, name, &child, &type) == 0) {
    inode = fsfs_iget(sb, child);
    if (IS_ERR(inode)) return ERR_CAST(inode);
  }
  return d_splice_alias(inode, dentry);
}
static int fsfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
  struct super_block *sb = dir->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct inode *inode;
  __u64 ni;
  char *name = (char *)dentry->d_name.name;
  mutex_lock(&sbi->lock);
  ni = fsfs_inode_create(sb, mode & 0xFFFF, 0);
  if (ni == 0) { mutex_unlock(&sbi->lock); return -ENOSPC; }
  if (fsfs_dir_add(sb, dir->i_ino, name, ni, 1) != 0) {
    sbi->inodes[ni].dead = 1;
    mutex_unlock(&sbi->lock);
    return -ENOSPC;
  }
  mutex_unlock(&sbi->lock);
  inode = fsfs_iget(sb, ni);
  if (IS_ERR(inode)) return PTR_ERR(inode);
  d_instantiate(dentry, inode);
  return 0;
}
static struct dentry *fsfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
  struct super_block *sb = dir->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct inode *inode;
  __u64 ni;
  char *name = (char *)dentry->d_name.name;
  mutex_lock(&sbi->lock);
  ni = fsfs_inode_create(sb, mode & 0xFFFF, 1);
  if (ni == 0) { mutex_unlock(&sbi->lock); return ERR_PTR(-ENOSPC); }
  if (fsfs_dir_add(sb, dir->i_ino, name, ni, 2) != 0) {
    sbi->inodes[ni].dead = 1;
    mutex_unlock(&sbi->lock);
    return ERR_PTR(-ENOSPC);
  }
  sbi->inodes[ni].nlink = 2;
  mutex_unlock(&sbi->lock);
  inode = fsfs_iget(sb, ni);
  if (IS_ERR(inode)) return ERR_CAST(inode);
  inc_nlink(dir);
  d_instantiate(dentry, inode);
  return NULL;
}
static int fsfs_unlink(struct inode *dir, struct dentry *dentry) {
  struct super_block *sb = dir->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  char *name = (char *)dentry->d_name.name;
  mutex_lock(&sbi->lock);
  fsfs_dir_del(sb, dir->i_ino, name);
  mutex_unlock(&sbi->lock);
  return 0;
}
static int fsfs_rmdir(struct inode *dir, struct dentry *dentry) {
  struct super_block *sb = dir->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  char *name = (char *)dentry->d_name.name;
  mutex_lock(&sbi->lock);
  fsfs_dir_del(sb, dir->i_ino, name);
  mutex_unlock(&sbi->lock);
  drop_nlink(dir);
  return 0;
}

static int fsfs_readdir(struct file *file, struct dir_context *ctx) {
  struct inode *inode = file_inode(file);
  struct super_block *sb = inode->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_dirent_t *ents;
  int got = 0;
  int i;
  if (!dir_emit_dots(file, ctx)) return 0;
  ents = kvzalloc(sizeof(fsfs_dirent_t) * 64, GFP_NOFS);
  if (!ents) return -ENOMEM;
  mutex_lock(&sbi->lock);
  fsfs_btree_list(sb, inode->i_ino, ents, 64, &got);
  mutex_unlock(&sbi->lock);
  for (i = 0; i < got; i++) {
    unsigned long ino = (unsigned long)ents[i].ino;
    unsigned char dtype = ents[i].type == 2 ? DT_DIR : DT_REG;
    if (!dir_emit(ctx, ents[i].name, ents[i].nlen, ino, dtype)) break;
    ctx->pos++;
  }
  kvfree(ents);
  return 0;
}
static ssize_t fsfs_file_read(struct file *file, char __user *buf, size_t n, loff_t *ppos) {
  struct inode *inode = file_inode(file);
  struct super_block *sb = inode->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  char *k;
  ssize_t r;
  if (*ppos >= inode->i_size) return 0;
  if (*ppos + n > inode->i_size) n = inode->i_size - *ppos;
  k = kvmalloc(n, GFP_NOFS);
  if (!k) return -ENOMEM;
  mutex_lock(&sbi->lock);
  r = fsfs_inode_read(sb, inode->i_ino, *ppos, k, n);
  mutex_unlock(&sbi->lock);
  if (r < 0) { kvfree(k); return -EIO; }
  if (copy_to_user(buf, k, r)) { kvfree(k); return -EFAULT; }
  *ppos += r;
  kvfree(k);
  return r;
}
static ssize_t fsfs_file_write(struct file *file, const char __user *buf, size_t n, loff_t *ppos) {
  struct inode *inode = file_inode(file);
  struct super_block *sb = inode->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  char *k;
  ssize_t r;
  k = kvmalloc(n, GFP_NOFS);
  if (!k) return -ENOMEM;
  if (copy_from_user(k, buf, n)) { kvfree(k); return -EFAULT; }
  mutex_lock(&sbi->lock);
  r = fsfs_inode_write(sb, inode->i_ino, *ppos, k, n);
  if (r >= 0) {
    inode->i_size = sbi->inodes[inode->i_ino].size;
    *ppos += n;
    r = n;
  }
  mutex_unlock(&sbi->lock);
  kvfree(k);
  return r < 0 ? -EIO : r;
}
static int fsfs_fsync(struct file *file, loff_t start, loff_t end, int datasync) {
  struct inode *inode = file_inode(file);
  sync_blockdev(inode->i_sb->s_bdev);
  return 0;
}
const struct file_operations fsfs_dir_ops = {
  .owner = THIS_MODULE,
  .iterate_shared = fsfs_readdir,
  .fsync = fsfs_fsync,
  .llseek = generic_file_llseek,
};
const struct file_operations fsfs_file_ops = {
  .owner = THIS_MODULE,
  .read = fsfs_file_read,
  .write = fsfs_file_write,
  .fsync = fsfs_fsync,
  .llseek = generic_file_llseek,
  .unlocked_ioctl = fsfs_ioctl,
};
const struct inode_operations fsfs_dir_inode_ops = {
  .lookup = fsfs_lookup,
  .create = fsfs_create,
  .mkdir = fsfs_mkdir,
  .unlink = fsfs_unlink,
  .rmdir = fsfs_rmdir,
};
const struct inode_operations fsfs_file_inode_ops = {
  .getattr = fsfs_getattr,
  .setattr = fsfs_setattr,
};
#endif
