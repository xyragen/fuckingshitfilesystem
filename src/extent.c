#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/time.h>
int fsfs_extent_alloc_for(struct super_block *sb, __u64 ino, __u64 loff, __u32 len, int comp) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __s64 b;
  int k;
  __u8 *raw;
  __u8 *out;
  __u8 hh[32];
  int algo;
  int rc = -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0) return -1;
  if (x->n_exts >= FSFS_MAX_EXTS) return -1;
  if (len > FSFS_BLOCK) len = FSFS_BLOCK;
  if (len == 0) len = FSFS_BLOCK;
  raw = kvmalloc(FSFS_BLOCK * 2, GFP_NOFS);
  if (!raw) return -ENOMEM;
  out = raw + FSFS_BLOCK;
  memset(raw, 0, FSFS_BLOCK);
  algo = comp;
  if (algo < 0) algo = fsfs_compress_pick(raw, len, x->comp_pol);
  b = fsfs_blk_alloc(sb, FSFS_ZONE_DATA);
  if (b < 0) goto out;
  memset(out, 0, FSFS_BLOCK);
  if (algo != FSFS_COMP_NONE) {
    int rr = fsfs_compress_blk(raw, len, out, FSFS_BLOCK, algo);
    if (rr > 0) {
      if (fsfs_blk_write(sb, (__u64)b, out) != 0) { fsfs_blk_free(sb, (__u64)b); goto out; }
    } else {
      algo = FSFS_COMP_NONE;
      if (fsfs_blk_write(sb, (__u64)b, raw) != 0) { fsfs_blk_free(sb, (__u64)b); goto out; }
    }
  } else {
    if (fsfs_blk_write(sb, (__u64)b, raw) != 0) { fsfs_blk_free(sb, (__u64)b); goto out; }
  }
  fsfs_sha256(raw, len, hh);
  k = x->n_exts;
  x->exts[k].l_off = loff;
  x->exts[k].p_blk = (__u64)b;
  x->exts[k].len = len;
  x->exts[k].flags = 0;
  x->exts[k].refs = 1;
  x->exts[k].crc = fsfs_crc32c(raw, len);
  x->exts[k].comp = (__u8)algo;
  x->exts[k].verified = 1;
  memcpy(x->exts[k].merkle, hh, 32);
  x->n_exts++;
  x->seq++;
  x->crc_meta = fsfs_crc32c(x, sizeof(fsfs_inode_t) - 512);
  rc = 0;
out:
  kvfree(raw);
  return rc;
}
int fsfs_extent_free_inode(struct super_block *sb, __u64 ino) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __u32 k;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0) return -1;
  for (k = 0; k < x->n_exts; k++) {
    fsfs_blk_decref(sb, x->exts[k].p_blk);
    x->exts[k].p_blk = 0;
    x->exts[k].refs = 0;
  }
  x->n_exts = 0;
  return 0;
}
int fsfs_extent_cow(struct super_block *sb, __u64 ino, __u64 loff) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __u32 k;
  int idx = -1;
  __s64 nb;
  __u8 *raw;
  __u8 *tmp;
  int rc = -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  for (k = 0; k < x->n_exts; k++) {
    if (x->exts[k].l_off == loff) { idx = (int)k; break; }
  }
  if (idx < 0) return -1;
  if (sbi->blk_ref[x->exts[idx].p_blk] <= 1 && x->exts[idx].refs <= 1) return 0;
  raw = kvmalloc(FSFS_BLOCK * 2, GFP_NOFS);
  if (!raw) return -ENOMEM;
  tmp = raw + FSFS_BLOCK;
  if (fsfs_blk_read(sb, x->exts[idx].p_blk, tmp) != 0) goto out;
  if (x->exts[idx].comp != FSFS_COMP_NONE) {
    int dl = fsfs_decompress_blk(tmp, FSFS_BLOCK, raw, FSFS_BLOCK, x->exts[idx].comp);
    if (dl < 0) goto out;
    memset(raw + dl, 0, FSFS_BLOCK - dl);
  } else {
    memcpy(raw, tmp, FSFS_BLOCK);
  }
  nb = fsfs_blk_alloc(sb, FSFS_ZONE_DATA);
  if (nb < 0) goto out;
  if (fsfs_blk_write(sb, (__u64)nb, raw) != 0) { fsfs_blk_free(sb, (__u64)nb); goto out; }
  fsfs_blk_decref(sb, x->exts[idx].p_blk);
  x->exts[idx].p_blk = (__u64)nb;
  x->exts[idx].refs = 1;
  x->exts[idx].comp = FSFS_COMP_NONE;
  x->exts[idx].crc = fsfs_crc32c(raw, x->exts[idx].len);
  fsfs_sha256(raw, x->exts[idx].len, x->exts[idx].merkle);
  x->seq++;
  rc = 0;
out:
  kvfree(raw);
  return rc;
}
#endif
