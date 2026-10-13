#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
int fsfs_scrub_block(struct super_block *sb, __u64 blk) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u8 b[FSFS_BLOCK];
  __u32 c;
  int i;
  int found = 0;
  __u32 want = 0;
  if (blk >= sbi->super.total_blocks) return -1;
  if (!sbi->blk_bmp[blk]) return 0;
  if (fsfs_blk_read(sb, blk, b) != 0) return -1;
  c = fsfs_crc32c(b, FSFS_BLOCK);
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &sbi->inodes[i];
    __u32 k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (x->exts[k].p_blk == blk) {
        __u8 dd[FSFS_BLOCK];
        found = 1;
        if (x->exts[k].comp != FSFS_COMP_NONE) {
          int dl = fsfs_decompress_blk(b, FSFS_BLOCK, dd, FSFS_BLOCK, x->exts[k].comp);
          if (dl < 0) return -1;
          want = fsfs_crc32c(dd, FSFS_BLOCK);
        } else {
          want = x->exts[k].crc;
          if (c != want) return -1;
          x->exts[k].verified = 1;
        }
        break;
      }
    }
    if (found) break;
  }
  if (!found) {
    if (c == 0xFFFFFFFFU) return -1;
  }
  sbi->super.scrub_seq++;
  return 0;
}
int fsfs_scrub_inode(struct super_block *sb, __u64 ino) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __u32 k;
  __u8 h[32];
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0) return -1;
  for (k = 0; k < x->n_exts; k++) {
    if (fsfs_scrub_block(sb, x->exts[k].p_blk) != 0) {
      if (sbi->blk_ref[x->exts[k].p_blk] > 1) {
        fsfs_extent_cow(sb, ino, x->exts[k].l_off);
      } else return -1;
    }
  }
  if (x->inline_used) {
    fsfs_sha256(x->inline_buf, x->size > 256 ? 256 : x->size, h);
    if (x->size > 0 && memcmp(h, x->sha_all, 4) != 0)
      return -1;
  }
  return 0;
}
int fsfs_scrub_all(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 b;
  int bad = 0;
  for (b = 0; b < sbi->super.total_blocks; b++) {
    if (sbi->scrub_stop) break;
    if (!sbi->blk_bmp[b]) continue;
    if (fsfs_scrub_block(sb, b) != 0) { bad++; sbi->scrub_bad++; }
    if (bad > 64) break;
  }
  return bad == 0 ? 0 : -1;
}

#endif
