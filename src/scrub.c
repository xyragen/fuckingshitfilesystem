#include "fsfs.h"
#include <string.h>
int fsfs_scrub_block(fsfs_mount_t *m, uint64_t blk) {
  uint8_t b[FSFS_BLOCK];
  uint32_t c;
  int i;
  int found = 0;
  uint32_t want = 0;
  if (!m) return -1;
  if (blk >= m->super.total_blocks) return -1;
  if (!m->blk_bmp[blk]) return 0;
  if (fsfs_blk_read(m, blk, b) != 0) return -1;
  c = fsfs_crc32c(b, FSFS_BLOCK);
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    uint32_t k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (x->exts[k].p_blk == blk) {
        uint8_t dd[FSFS_BLOCK];
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
  m->super.scrub_seq++;
  return 0;
}
int fsfs_scrub_inode(fsfs_mount_t *m, uint64_t ino) {
  fsfs_inode_t *x;
  uint32_t k;
  uint8_t h[32];
  if (!m) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0) return -1;
  for (k = 0; k < x->n_exts; k++) {
    if (fsfs_scrub_block(m, x->exts[k].p_blk) != 0) {
      if (m->blk_ref[x->exts[k].p_blk] > 1) {
        fsfs_extent_cow(m, ino, x->exts[k].l_off);
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
int fsfs_scrub_all(fsfs_mount_t *m) {
  uint64_t b;
  int bad = 0;
  if (!m) return -1;
  for (b = 0; b < m->super.total_blocks; b++) {
    if (m->scrub_stop) break;
    if (!m->blk_bmp[b]) continue;
    if (fsfs_scrub_block(m, b) != 0) bad++;
    if (bad > 64) break;
  }
  return bad == 0 ? 0 : -1;
}
