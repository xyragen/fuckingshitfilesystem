#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/time.h>
__u64 fsfs_space_logical(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 t = 0;
  int i;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    if (x->ino == 0) continue;
    if (x->dead) continue;
    t += x->size;
  }
  sbi->bytes_logical = t;
  return t;
}
__u64 fsfs_space_phys(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 t = 0;
  __u64 i;
  for (i = 0; i < sbi->super.total_blocks; i++) {
    if (sbi->blk_bmp[i]) t++;
  }
  t = t * FSFS_BLOCK;
  sbi->bytes_phys = t;
  return t;
}
__u64 fsfs_space_shared(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 t = 0;
  __u64 i;
  for (i = 0; i < sbi->super.total_blocks; i++) {
    if (sbi->blk_bmp[i] && sbi->blk_ref[i] > 1) {
      t += FSFS_BLOCK;
    }
  }
  sbi->bytes_shared = t;
  return t;
}
__u64 fsfs_space_exclusive(struct super_block *sb, __u64 ino) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __u64 t = 0;
  __u32 k;
  if (ino >= FSFS_MAX_INODES) return 0;
  x = &sbi->inodes[ino];
  if (x->ino == 0) return 0;
  if (x->inline_used) t += x->size > FSFS_INLINE_SZ ? FSFS_INLINE_SZ : x->size;
  for (k = 0; k < x->n_exts; k++) {
    __u64 b = x->exts[k].p_blk;
    if (b >= sbi->super.total_blocks) continue;
    if (sbi->blk_ref[b] == 1) t += x->exts[k].len;
    else if (sbi->blk_ref[b] == 0) continue;
  }
  return t;
}
__s64 fsfs_space_snap_delete_estimate(struct super_block *sb, __u64 snapid) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int found = -1;
  __u64 est = 0;
  __u64 j;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (sbi->snaps[i].active && sbi->snaps[i].id == snapid) { found = i; break; }
  }
  if (found < 0) return -1;
  for (j = 0; j < sbi->super.total_blocks; j++) {
    if (sbi->blk_bmp[j] && sbi->blk_ref[j] > 1)
      est += FSFS_BLOCK / 2;
  }
  est += sbi->snaps[found].blocks_used * 128;
  if (est > sbi->bytes_phys && sbi->bytes_phys) est = sbi->bytes_phys / 4;
  return (__s64)est;
}
void fsfs_space_why_pinned(struct super_block *sb, __u64 blk, char *out, size_t osz) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int n = 0;
  if (!out || osz == 0) return;
  out[0] = 0;
  if (blk >= sbi->super.total_blocks) {
    snprintf(out, osz, "bad block %llu", blk);
    return;
  }
  if (sbi->blk_bmp[blk] == 0) {
    snprintf(out, osz, "block %llu free", blk);
    return;
  }
  n = snprintf(out, osz, "block %llu refs=%u ", blk, sbi->blk_ref[blk]);
  for (i = 0; i < FSFS_MAX_INODES && (size_t)n < osz - 64; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    __u32 k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (x->exts[k].p_blk == blk) {
        n += snprintf(out + n, osz - n, "ino%llu ", x->ino);
        break;
      }
    }
  }
  for (i = 0; i < FSFS_MAX_SNAPS && (size_t)n < osz - 32; i++) {
    if (sbi->snaps[i].active && sbi->snaps[i].root_copy == blk) {
      n += snprintf(out + n, osz - n, "snap%llu ", sbi->snaps[i].id);
    }
  }
  if (sbi->blk_ref[blk] > 1) {
    snprintf(out + n, osz - n, "shared");
  } else {
    snprintf(out + n, osz - n, "exclusive");
  }
}
#endif
