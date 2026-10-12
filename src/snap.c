#include "fsfs.h"
#include <string.h>
#include <stdio.h>
int fsfs_snap_create(fsfs_mount_t *m, char *label, uint64_t *out) {
  int i;
  int slot = -1;
  uint64_t id;
  uint8_t buf[FSFS_BLOCK];
  if (!m) return -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (!m->snaps[i].active && slot < 0) slot = i;
  }
  if (slot < 0) {
    fsfs_snap_expire(m);
    for (i = 0; i < FSFS_MAX_SNAPS; i++) if (!m->snaps[i].active && slot < 0) slot = i;
    if (slot < 0) return -1;
  }
  id = ++m->super.txn_seq;
  if (id == 0) id = 1;
  m->snaps[slot].id = id;
  m->snaps[slot].gen = m->cur_gen++;
  m->super.next_gen = m->cur_gen;
  m->snaps[slot].stamp = (uint64_t)time(0);
  m->snaps[slot].root_copy = m->super.root_ino;
  m->snaps[slot].active = 1;
  m->snaps[slot].auto_expire = 1;
  if (label) strncpy(m->snaps[slot].label, label, 63);
  else snprintf(m->snaps[slot].label, 64, "snap%llu", (unsigned long long)id);
  m->snaps[slot].blocks_used = 0;
  m->snaps[slot].blocks_shared = 0;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    uint32_t k;
    if (x->ino == 0 || x->dead) continue;
    for (k = 0; k < x->n_exts; k++) {
      fsfs_blk_incref(m, x->exts[k].p_blk);
      x->exts[k].refs++;
      m->snaps[slot].blocks_shared++;
    }
  }
  memset(buf, 0, FSFS_BLOCK);
  memcpy(buf, m->inodes, sizeof(fsfs_inode_t) * 4);
  fsfs_sha256(buf, FSFS_BLOCK, m->snaps[slot].root_hash);
  m->super.snap_count++;
  if (out) *out = id;
  fsfs_image_sync(m);
  return 0;
}
int fsfs_snap_delete(fsfs_mount_t *m, uint64_t id) {
  int i;
  int f = -1;
  if (!m) return -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) if (m->snaps[i].active && m->snaps[i].id == id) f = i;
  if (f < 0) return -1;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    uint32_t k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (m->blk_ref[x->exts[k].p_blk] > 1) {
        fsfs_blk_decref(m, x->exts[k].p_blk);
        if (x->exts[k].refs > 0) x->exts[k].refs--;
      }
    }
  }
  memset(&m->snaps[f], 0, sizeof(fsfs_snap_t));
  if (m->super.snap_count > 0) m->super.snap_count--;
  fsfs_image_sync(m);
  return 0;
}
int fsfs_snap_restore(fsfs_mount_t *m, uint64_t id) {
  int i;
  int f = -1;
  if (!m) return -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) if (m->snaps[i].active && m->snaps[i].id == id) f = i;
  if (f < 0) return -1;
  m->cur_gen++;
  m->super.next_gen = m->cur_gen;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    uint32_t k;
    if (x->ino == 0) continue;
    for (k = 0; k < x->n_exts; k++) {
      if (x->exts[k].refs > 1 || m->blk_ref[x->exts[k].p_blk] > 1) {
        fsfs_extent_cow(m, x->ino, x->exts[k].l_off);
      }
    }
    x->gen = m->cur_gen;
  }
  fsfs_image_sync(m);
  return 0;
}
int fsfs_snap_diff(fsfs_mount_t *m, uint64_t a, uint64_t b, char *out, size_t osz) {
  int ia = -1;
  int ib = -1;
  int i;
  int n = 0;
  if (!m || !out) return -1;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (m->snaps[i].active && m->snaps[i].id == a) ia = i;
    if (m->snaps[i].active && m->snaps[i].id == b) ib = i;
  }
  if (ia < 0 || ib < 0) return -1;
  n = snprintf(out, osz, "diff %llu vs %llu\n", (unsigned long long)a, (unsigned long long)b);
  for (i = 0; i < FSFS_MAX_INODES && (size_t)n < osz - 128; i++) {
    fsfs_inode_t *x = &m->inodes[i];
    if (x->ino == 0) continue;
    if ((x->gen >= m->snaps[ia].gen && x->gen <= m->snaps[ib].gen) || x->seq > 2) {
      n += snprintf(out + n, osz - n, "ino %llu seq %llu size %llu\n",
        (unsigned long long)x->ino, (unsigned long long)x->seq, (unsigned long long)x->size);
    }
  }
  return 0;
}
int fsfs_snap_expire(fsfs_mount_t *m) {
  int i;
  int oldest = -1;
  uint64_t omin = 0;
  int killed = 0;
  if (!m) return -1;
  if (m->super.free_blocks * 4 > m->super.total_blocks) return 0;
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (!m->snaps[i].active) continue;
    if (!m->snaps[i].auto_expire) continue;
    if (oldest < 0 || m->snaps[i].stamp < omin) { oldest = i; omin = m->snaps[i].stamp; }
  }
  if (oldest >= 0) {
    uint64_t id = m->snaps[oldest].id;
    fsfs_snap_delete(m, id);
    killed = 1;
  }
  return killed;
}
