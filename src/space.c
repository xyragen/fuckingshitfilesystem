#include "fsfs.h"
#include <string.h>
#include <stdio.h>
uint64_t fsfs_space_logical(fsfs_mount_t *m) {
  uint64_t t = 0;
  int i;
  if (!m) return 0;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    if (x->ino == 0) continue;
    if (x->dead) continue;
    t += x->size;
  }
  m->bytes_logical = t;
  return t;
}
uint64_t fsfs_space_phys(fsfs_mount_t *m) {
  uint64_t t = 0;
  uint64_t i;
  if (!m) return 0;
  for (i = 0; i < m->super.total_blocks; i++) {
    if (m->blk_bmp[i]) t++;
  }
  t = t * FSFS_BLOCK;
  m->bytes_phys = t;
  return t;
}
uint64_t fsfs_space_shared(fsfs_mount_t *m) {
  uint64_t t = 0;
  uint64_t i;
  if (!m) return 0;
  for (i = 0; i < m->super.total_blocks; i++) {
    if (m->blk_bmp[i] && m->blk_ref[i] > 1) {
      t += FSFS_BLOCK;
    }
  }
  m->bytes_shared = t;
  return t;
}
uint64_t fsfs_space_exclusive(fsfs_mount_t *m, uint64_t ino) {
  fsfs_inode_t *x;
  uint64_t t = 0;
  uint32_t k;
  if (!m) return 0;
  if (ino >= FSFS_MAX_INODES) return 0;
  x = &m->inodes[ino];
  if (x->ino == 0) return 0;
  if (x->inline_used) t += x->size > FSFS_INLINE_SZ ? FSFS_INLINE_SZ : x->size;
  for (k = 0; k < x->n_exts; k++) {
    uint64_t b = x->exts[k].p_blk;
    if (b >= m->super.total_blocks) continue;
    if (m->blk_ref[b] == 1) t += x->exts[k].len;
    else if (m->blk_ref[b] == 0) continue;
  }
  return t;
}
int64_t fsfs_space_snap_delete_estimate(fsfs_mount_t *m, uint64_t snapid) {
  int i;
  int found = -1;
  uint64_t est = 0;
  uint64_t j;
  if (!m) return -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (m->snaps[i].active && m->snaps[i].id == snapid) { found = i; break; }
  }
  if (found < 0) return -1;
  for (j = 0; j < m->super.total_blocks; j++) {
    if (m->blk_bmp[j] && m->blk_ref[j] > 1)
      est += FSFS_BLOCK / 2;
  }
  est += m->snaps[found].blocks_used * 128;
  if (est > m->bytes_phys && m->bytes_phys) est = m->bytes_phys / 4;
  return (int64_t)est;
}
void fsfs_space_why_pinned(fsfs_mount_t *m, uint64_t blk, char *out, size_t osz) {
  int i;
  int n = 0;
  if (!m || !out || osz == 0) return;
  out[0] = 0;
  if (blk >= m->super.total_blocks) {
    snprintf(out, osz, "bad block %llu", (unsigned long long)blk);
    return;
  }
  if (m->blk_bmp[blk] == 0) {
    snprintf(out, osz, "block %llu free", (unsigned long long)blk);
    return;
  }
  n = snprintf(out, osz, "block %llu refs=%u ", (unsigned long long)blk, m->blk_ref[blk]);
  for (i = 0; i < FSFS_MAX_INODES && (size_t)n < osz - 64; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    uint32_t k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (x->exts[k].p_blk == blk) {
        n += snprintf(out + n, osz - n, "ino%llu ", (unsigned long long)x->ino);
        break;
      }
    }
  }
  for (i = 0; i < FSFS_MAX_SNAPS && (size_t)n < osz - 32; i++) {
    if (m->snaps[i].active && m->snaps[i].root_copy == blk) {
      n += snprintf(out + n, osz - n, "snap%llu ", (unsigned long long)m->snaps[i].id);
    }
  }
  if (m->blk_ref[blk] > 1) {
    snprintf(out + n, osz - n, "shared");
  } else {
    snprintf(out + n, osz - n, "exclusive");
  }
}
