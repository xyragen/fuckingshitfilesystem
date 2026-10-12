#include "fsfs.h"
#include <string.h>
#include <stdlib.h>
int fsfs_extent_alloc_for(fsfs_mount_t *m, uint64_t ino, uint64_t loff, uint32_t len, int comp) {
  fsfs_inode_t *x;
  int64_t b;
  int k;
  uint8_t raw[FSFS_BLOCK];
  uint8_t out[FSFS_BLOCK];
  uint8_t hh[32];
  int algo;
  if (!m) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0) return -1;
  if (x->n_exts >= FSFS_MAX_EXTS) return -1;
  if (len > FSFS_BLOCK) len = FSFS_BLOCK;
  if (len == 0) len = FSFS_BLOCK;
  memset(raw, 0, FSFS_BLOCK);
  algo = comp;
  if (algo < 0) algo = fsfs_compress_pick(raw, len, x->comp_pol);
  b = fsfs_blk_alloc(m, FSFS_ZONE_DATA);
  if (b < 0) return -1;
  memset(out, 0, FSFS_BLOCK);
  if (algo != FSFS_COMP_NONE) {
    int rr = fsfs_compress_blk(raw, len, out, FSFS_BLOCK, algo);
    if (rr > 0) {
      if (fsfs_blk_write(m, (uint64_t)b, out) != 0) { fsfs_blk_free(m, (uint64_t)b); return -1; }
    } else {
      algo = FSFS_COMP_NONE;
      if (fsfs_blk_write(m, (uint64_t)b, raw) != 0) { fsfs_blk_free(m, (uint64_t)b); return -1; }
    }
  } else {
    if (fsfs_blk_write(m, (uint64_t)b, raw) != 0) { fsfs_blk_free(m, (uint64_t)b); return -1; }
  }
  fsfs_sha256(raw, len, hh);
  k = x->n_exts;
  x->exts[k].l_off = loff;
  x->exts[k].p_blk = (uint64_t)b;
  x->exts[k].len = len;
  x->exts[k].flags = 0;
  x->exts[k].refs = 1;
  x->exts[k].crc = fsfs_crc32c(raw, len);
  x->exts[k].comp = (uint8_t)algo;
  x->exts[k].verified = 1;
  memcpy(x->exts[k].merkle, hh, 32);
  x->n_exts++;
  x->seq++;
  x->crc_meta = fsfs_crc32c(x, sizeof(fsfs_inode_t) - 512);
  return 0;
}
int fsfs_extent_free_inode(fsfs_mount_t *m, uint64_t ino) {
  fsfs_inode_t *x;
  uint32_t k;
  if (!m) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0) return -1;
  for (k = 0; k < x->n_exts; k++) {
    fsfs_blk_decref(m, x->exts[k].p_blk);
    x->exts[k].p_blk = 0;
    x->exts[k].refs = 0;
  }
  x->n_exts = 0;
  return 0;
}
int fsfs_extent_cow(fsfs_mount_t *m, uint64_t ino, uint64_t loff) {
  fsfs_inode_t *x;
  uint32_t k;
  int idx = -1;
  int64_t nb;
  uint8_t raw[FSFS_BLOCK];
  uint8_t tmp[FSFS_BLOCK];
  if (!m) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  for (k = 0; k < x->n_exts; k++) {
    if (x->exts[k].l_off == loff) { idx = (int)k; break; }
  }
  if (idx < 0) return -1;
  if (m->blk_ref[x->exts[idx].p_blk] <= 1 && x->exts[idx].refs <= 1) return 0;
  if (fsfs_blk_read(m, x->exts[idx].p_blk, tmp) != 0) return -1;
  if (x->exts[idx].comp != FSFS_COMP_NONE) {
    if (fsfs_decompress_blk(tmp, FSFS_BLOCK, raw, FSFS_BLOCK, x->exts[idx].comp) < 0) {
      memcpy(raw, tmp, FSFS_BLOCK);
    }
  } else {
    memcpy(raw, tmp, FSFS_BLOCK);
  }
  nb = fsfs_blk_alloc(m, FSFS_ZONE_DATA);
  if (nb < 0) return -1;
  if (fsfs_blk_write(m, (uint64_t)nb, raw) != 0) { fsfs_blk_free(m, (uint64_t)nb); return -1; }
  fsfs_blk_decref(m, x->exts[idx].p_blk);
  x->exts[idx].p_blk = (uint64_t)nb;
  x->exts[idx].refs = 1;
  x->exts[idx].crc = fsfs_crc32c(raw, x->exts[idx].len);
  fsfs_sha256(raw, x->exts[idx].len, x->exts[idx].merkle);
  x->seq++;
  return 0;
}
