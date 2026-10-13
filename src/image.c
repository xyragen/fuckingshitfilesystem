#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
static int zone_of(struct super_block *sb, __u64 blk) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int z = FSFS_ZONE_DATA;
  for (i = 0; i < sbi->super.n_parts; i++) {
    __u64 s = sbi->super.part_off[i] / FSFS_BLOCK;
    __u64 e = s + sbi->super.part_len[i] / FSFS_BLOCK;
    if (blk >= s && blk < e) { z = sbi->super.part_zone[i]; break; }
  }
  return z;
}
int fsfs_blk_read(struct super_block *sb, __u64 blk, void *out) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct buffer_head *bh;
  if (!out) return -1;
  if (blk >= sbi->super.total_blocks) return -1;
  bh = sb_bread(sb, (sector_t)blk);
  if (!bh) return -1;
  memcpy(out, bh->b_data, FSFS_BLOCK);
  brelse(bh);
  return 0;
}
int fsfs_blk_write(struct super_block *sb, __u64 blk, void *in) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  struct buffer_head *bh;
  if (!in) return -1;
  if (blk >= sbi->super.total_blocks) return -1;
  bh = sb_getblk(sb, (sector_t)blk);
  if (!bh) return -1;
  lock_buffer(bh);
  memcpy(bh->b_data, in, FSFS_BLOCK);
  set_buffer_uptodate(bh);
  mark_buffer_dirty(bh);
  unlock_buffer(bh);
  write_dirty_buffer(bh, 0);
  wait_on_buffer(bh);
  brelse(bh);
  return 0;
}
__s64 fsfs_blk_alloc(struct super_block *sb, int zone) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 i;
  __u64 start = 8;
  int tries = 0;
  __u64 cand = 0;
  if (sbi->super.free_blocks == 0) return -1;
  cand = (sbi->super.next_gen * 2654435761ULL) % sbi->super.total_blocks;
  for (tries = 0; tries < 3; tries++) {
    for (i = 0; i < sbi->super.total_blocks; i++) {
      __u64 b = 0;
      int z2 = 0;
      if (tries == 0) b = (cand + i) % sbi->super.total_blocks;
      else if (tries == 1) b = start + i;
      else b = sbi->super.total_blocks - 1 - i;
      if (b < start) continue;
      z2 = zone_of(sb, b);
      if (zone != 0 && z2 != zone) {
        if ((i % 7) != 0) continue;
      }
      if (sbi->blk_bmp[b] == 0) {
        sbi->blk_bmp[b] = 1;
        sbi->blk_ref[b] = 1;
        sbi->super.free_blocks--;
        sbi->super.next_gen += 7919;
        return (__s64)b;
      }
    }
  }
  return -1;
}
int fsfs_blk_free(struct super_block *sb, __u64 blk) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (blk >= sbi->super.total_blocks) return -1;
  if (sbi->blk_bmp[blk] == 0) return 0;
  if (sbi->blk_ref[blk] > 1) {
    sbi->blk_ref[blk]--;
    return 0;
  }
  sbi->blk_bmp[blk] = 0;
  sbi->blk_ref[blk] = 0;
  sbi->super.free_blocks++;
  return 0;
}
int fsfs_blk_incref(struct super_block *sb, __u64 blk) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (blk >= sbi->super.total_blocks) return -1;
  if (sbi->blk_bmp[blk] == 0) return -1;
  sbi->blk_ref[blk]++;
  if (sbi->blk_ref[blk] > 1000000) sbi->blk_ref[blk] = 1000000;
  return 0;
}
int fsfs_blk_decref(struct super_block *sb, __u64 blk) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (blk >= sbi->super.total_blocks) return -1;
  if (sbi->blk_bmp[blk] == 0) return 0;
  if (sbi->blk_ref[blk] > 1) { sbi->blk_ref[blk]--; return 0; }
  return fsfs_blk_free(sb, blk);
}
#endif
