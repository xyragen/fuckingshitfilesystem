#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/time.h>
static __u64 fsfs_now3(void) {
  return (__u64)ktime_get_real_seconds();
}
int fsfs_snap_create(struct super_block *sb, char *label, __u64 *out) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int slot = -1;
  __u64 id;
  __u8 buf[FSFS_BLOCK];
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (!sbi->snaps[i].active && slot < 0) slot = i;
  }
  if (slot < 0) {
    fsfs_snap_expire(sb);
    for (i = 0; i < FSFS_MAX_SNAPS; i++) if (!sbi->snaps[i].active && slot < 0) slot = i;
    if (slot < 0) return -1;
  }
  id = ++sbi->super.txn_seq;
  if (id == 0) id = 1;
  sbi->snaps[slot].id = id;
  sbi->snaps[slot].gen = sbi->cur_gen++;
  sbi->super.next_gen = sbi->cur_gen;
  sbi->snaps[slot].stamp = fsfs_now3();
  sbi->snaps[slot].root_copy = sbi->super.root_ino;
  sbi->snaps[slot].active = 1;
  sbi->snaps[slot].auto_expire = 1;
  if (label) {
    size_t ll = strlen(label);
    if (ll > 63) ll = 63;
    memcpy(sbi->snaps[slot].label, label, ll);
    sbi->snaps[slot].label[ll] = 0;
  }
  else snprintf(sbi->snaps[slot].label, 64, "snap%llu", id);
  sbi->snaps[slot].blocks_used = 0;
  sbi->snaps[slot].blocks_shared = 0;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    __u32 k;
    if (x->ino == 0 || x->dead) continue;
    for (k = 0; k < x->n_exts; k++) {
      fsfs_blk_incref(sb, x->exts[k].p_blk);
      x->exts[k].refs++;
      sbi->snaps[slot].blocks_shared++;
    }
  }
  memset(buf, 0, FSFS_BLOCK);
  memcpy(buf, sbi->inodes, sizeof(fsfs_inode_t) * 4);
  fsfs_sha256(buf, FSFS_BLOCK, sbi->snaps[slot].root_hash);
  sbi->super.snap_count++;
  if (out) *out = id;
  return 0;
}
int fsfs_snap_delete(struct super_block *sb, __u64 id) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int f = -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) if (sbi->snaps[i].active && sbi->snaps[i].id == id) f = i;
  if (f < 0) return -1;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    __u32 k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (sbi->blk_ref[x->exts[k].p_blk] > 1) {
        fsfs_blk_decref(sb, x->exts[k].p_blk);
        if (x->exts[k].refs > 0) x->exts[k].refs--;
      }
    }
  }
  memset(&sbi->snaps[f], 0, sizeof(fsfs_snap_t));
  if (sbi->super.snap_count > 0) sbi->super.snap_count--;
  return 0;
}
int fsfs_snap_restore(struct super_block *sb, __u64 id) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int f = -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) if (sbi->snaps[i].active && sbi->snaps[i].id == id) f = i;
  if (f < 0) return -1;
  sbi->cur_gen++;
  sbi->super.next_gen = sbi->cur_gen;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    __u32 k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (x->exts[k].refs > 1 || sbi->blk_ref[x->exts[k].p_blk] > 1) {
        fsfs_extent_cow(sb, x->ino, x->exts[k].l_off);
      }
    }
    x->gen = sbi->cur_gen;
  }
  return 0;
}
int fsfs_snap_diff(struct super_block *sb, __u64 a, __u64 b, char *out, size_t osz) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int ia = -1;
  int ib = -1;
  int i;
  int n = 0;
  if (!out) return -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (sbi->snaps[i].active && sbi->snaps[i].id == a) ia = i;
    if (sbi->snaps[i].active && sbi->snaps[i].id == b) ib = i;
  }
  if (ia < 0 || ib < 0) return -1;
  n = snprintf(out, osz, "diff %llu vs %llu\n", a, b);
  for (i = 0; i < FSFS_MAX_INODES && (size_t)n < osz - 128; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    if (x->ino == 0) continue;
    if ((x->gen >= sbi->snaps[ia].gen && x->gen <= sbi->snaps[ib].gen) || x->seq > 2) {
      n += snprintf(out + n, osz - n, "ino %llu seq %llu size %llu\n", x->ino, x->seq, x->size);
    }
  }
  return 0;
}
int fsfs_snap_expire(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int oldest = -1;
  __u64 omin = 0;
  int killed = 0;
  if (sbi->super.free_blocks * 4 > sbi->super.total_blocks) return 0;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (!sbi->snaps[i].active) continue;
    if (!sbi->snaps[i].auto_expire) continue;
    if (oldest < 0 || sbi->snaps[i].stamp < omin) { oldest = i; omin = sbi->snaps[i].stamp; }
  }
  if (oldest >= 0) {
    __u64 id = sbi->snaps[oldest].id;
    fsfs_snap_delete(sb, id);
    killed = 1;
  }
  return killed;
}
#endif
