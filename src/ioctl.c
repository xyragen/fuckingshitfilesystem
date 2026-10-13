#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/time.h>
int fsfs_hist_record(struct super_block *sb, __u64 snapid, char *label) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (sbi->hist_n < FSFS_MAX_SNAPS) sbi->hist[sbi->hist_n++] = snapid;
  return 0;
}
int fsfs_hist_list(struct super_block *sb, char *out, size_t osz) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int n = 0;
  __u64 i;
  n = snprintf(out, osz, "history %llu\n", sbi->hist_n);
  for (i = 0; i < sbi->hist_n && (size_t)n < osz - 64; i++) {
    n += snprintf(out + n, osz - n, "%llu\n", sbi->hist[i]);
  }
  return 0;
}
int fsfs_hist_gc(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int k = 0;
  __u64 i;
  __u64 w = 0;
  for (i = 0; i < sbi->hist_n; i++) {
    int alive = 0;
    int j;
    for (j = 0; j < FSFS_MAX_SNAPS; j++) {
      if (sbi->snaps[j].active && sbi->snaps[j].id == sbi->hist[i]) { alive = 1; break; }
    }
    if (alive) sbi->hist[w++] = sbi->hist[i];
    else k++;
  }
  sbi->hist_n = w;
  return k;
}
static long fsfs_ioc_snap_create(struct super_block *sb, struct fsfs_ioc_snap *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_snap a;
  __u64 id = 0;
  int r;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  a.label[63] = 0;
  mutex_lock(&sbi->lock);
  r = fsfs_snap_create(sb, a.label, &id);
  if (r == 0) fsfs_hist_record(sb, id, a.label);
  mutex_unlock(&sbi->lock);
  a.out = id;
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return r;
}
static long fsfs_ioc_snap_delete(struct super_block *sb, __u64 *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 id;
  int r;
  if (copy_from_user(&id, arg, sizeof(id))) return -EFAULT;
  mutex_lock(&sbi->lock);
  r = fsfs_snap_delete(sb, id);
  fsfs_hist_gc(sb);
  mutex_unlock(&sbi->lock);
  return r;
}
static long fsfs_ioc_snap_restore(struct super_block *sb, __u64 *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 id;
  int r;
  if (copy_from_user(&id, arg, sizeof(id))) return -EFAULT;
  mutex_lock(&sbi->lock);
  r = fsfs_snap_restore(sb, id);
  mutex_unlock(&sbi->lock);
  return r;
}
static long fsfs_ioc_space(struct super_block *sb, struct fsfs_ioc_space *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_space a;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  mutex_lock(&sbi->lock);
  a.logical = fsfs_space_logical(sb);
  a.phys = fsfs_space_phys(sb);
  a.shared = fsfs_space_shared(sb);
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return 0;
}
static long fsfs_ioc_space_excl(struct super_block *sb, struct fsfs_ioc_space *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_space a;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  mutex_lock(&sbi->lock);
  a.exclusive = fsfs_space_exclusive(sb, a.ino);
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return 0;
}
static long fsfs_ioc_snap_est(struct super_block *sb, struct fsfs_ioc_space *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_space a;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  mutex_lock(&sbi->lock);
  a.est = fsfs_space_snap_delete_estimate(sb, a.id);
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return 0;
}

static long fsfs_ioc_pinned(struct super_block *sb, struct fsfs_ioc_text *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_text a;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  mutex_lock(&sbi->lock);
  fsfs_space_why_pinned(sb, a.a, a.buf, sizeof(a.buf));
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return 0;
}
static long fsfs_ioc_snap_diff(struct super_block *sb, struct fsfs_ioc_text *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_text a;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  mutex_lock(&sbi->lock);
  fsfs_snap_diff(sb, a.a, a.b, a.buf, sizeof(a.buf));
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return 0;
}
static long fsfs_ioc_dedup(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int r;
  mutex_lock(&sbi->lock);
  r = fsfs_reflink_dedup(sb);
  mutex_unlock(&sbi->lock);
  return r;
}
static long fsfs_ioc_scrub(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int r;
  mutex_lock(&sbi->lock);
  r = fsfs_scrub_all(sb);
  mutex_unlock(&sbi->lock);
  return r;
}
static long fsfs_ioc_scrub_bad(struct super_block *sb, __u64 *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 b;
  mutex_lock(&sbi->lock);
  b = sbi->scrub_bad;
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &b, sizeof(b))) return -EFAULT;
  return 0;
}
static long fsfs_ioc_hist_list(struct super_block *sb, struct fsfs_ioc_text *arg) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct fsfs_ioc_text a;
  if (copy_from_user(&a, arg, sizeof(a))) return -EFAULT;
  mutex_lock(&sbi->lock);
  fsfs_hist_list(sb, a.buf, sizeof(a.buf));
  mutex_unlock(&sbi->lock);
  if (copy_to_user(arg, &a, sizeof(a))) return -EFAULT;
  return 0;
}
long fsfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  struct inode *inode = file_inode(file);
  struct super_block *sb = inode->i_sb;
  switch (cmd) {
    case FSFS_IOC_SNAP_CREATE: return fsfs_ioc_snap_create(sb, (struct fsfs_ioc_snap *)arg);
    case FSFS_IOC_SNAP_DELETE: return fsfs_ioc_snap_delete(sb, (__u64 *)arg);
    case FSFS_IOC_SNAP_RESTORE: return fsfs_ioc_snap_restore(sb, (__u64 *)arg);
    case FSFS_IOC_SNAP_DIFF: return fsfs_ioc_snap_diff(sb, (struct fsfs_ioc_text *)arg);
    case FSFS_IOC_SPACE: return fsfs_ioc_space(sb, (struct fsfs_ioc_space *)arg);
    case FSFS_IOC_SPACE_EXCL: return fsfs_ioc_space_excl(sb, (struct fsfs_ioc_space *)arg);
    case FSFS_IOC_SNAP_EST: return fsfs_ioc_snap_est(sb, (struct fsfs_ioc_space *)arg);
    case FSFS_IOC_SPACE_PINNED: return fsfs_ioc_pinned(sb, (struct fsfs_ioc_text *)arg);
    case FSFS_IOC_DEDUP: return fsfs_ioc_dedup(sb);
    case FSFS_IOC_SCRUB: return fsfs_ioc_scrub(sb);
    case FSFS_IOC_SCRUB_BAD: return fsfs_ioc_scrub_bad(sb, (__u64 *)arg);
    case FSFS_IOC_HIST_LIST: return fsfs_ioc_hist_list(sb, (struct fsfs_ioc_text *)arg);
  }
  return -ENOTTY;
}
int fsfs_getattr(struct mnt_idmap *idmap, const struct path *path, struct kstat *stat, u32 request_mask, unsigned int flags) {
  struct inode *inode = d_inode(path->dentry);
  generic_fillattr(idmap, request_mask, inode, stat);
  stat->blksize = FSFS_BLOCK;
  return 0;
}
int fsfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *attr) {
  struct inode *inode = d_inode(dentry);
  struct super_block *sb = inode->i_sb;
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int r;
  r = setattr_prepare(idmap, dentry, attr);
  if (r) return r;
  if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size != inode->i_size) {
    mutex_lock(&sbi->lock);
    fsfs_inode_resize(sb, inode->i_ino, attr->ia_size);
    mutex_unlock(&sbi->lock);
    inode->i_size = sbi->inodes[inode->i_ino].size;
  }
  setattr_copy(idmap, inode, attr);
  mark_inode_dirty(inode);
  return 0;
}
#endif
