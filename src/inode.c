#include "fsfs.h"
#include <string.h>
#include <stdlib.h>
uint64_t fsfs_inode_create(fsfs_mount_t *m, uint32_t mode, int is_dir) {
  uint64_t i;
  fsfs_inode_t *x;
  if (!m) return 0;
  for (i = 1; i < FSFS_MAX_INODES; i++) {
    uint64_t cand = (m->super.next_ino + i) % FSFS_MAX_INODES;
    if (cand == 0) continue;
    if (m->inodes[cand].ino == 0 || m->inodes[cand].dead) {
      x = &m->inodes[cand];
      memset(x, 0, sizeof(*x));
      x->ino = cand;
      x->mode = mode;
      x->nlink = 1;
      x->uid = (uint32_t)getuid();
      x->gid = (uint32_t)getgid();
      x->size = 0;
      x->atime = x->mtime = x->ctime = (uint64_t)time(0);
      x->gen = m->cur_gen;
      x->seq = 1;
      x->comp_pol = FSFS_XMODE_LAT;
      x->xmode = FSFS_XMODE_LAT;
      x->is_dir = is_dir ? 1 : 0;
      x->crc_meta = fsfs_crc32c(x, 128);
      m->super.next_ino = cand + 1;
      if (m->super.next_ino >= FSFS_MAX_INODES) m->super.next_ino = 1;
      return cand;
    }
  }
  return 0;
}
int fsfs_inode_resize(fsfs_mount_t *m, uint64_t ino, uint64_t sz) {
  fsfs_inode_t *x;
  if (!m) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0) return -1;
  if (sz < x->size) {
    uint64_t nb = (sz + FSFS_BLOCK - 1) / FSFS_BLOCK;
    uint32_t k = 0;
    while (k < x->n_exts) {
      uint64_t idx = x->exts[k].l_off / FSFS_BLOCK;
      if (idx >= nb) {
        uint32_t j;
        fsfs_blk_decref(m, x->exts[k].p_blk);
        for (j = k; j + 1 < x->n_exts; j++) x->exts[j] = x->exts[j + 1];
        x->n_exts--;
      } else k++;
    }
  }
  x->size = sz;
  x->seq++;
  return 0;
}
int fsfs_inode_unlink(fsfs_mount_t *m, uint64_t ino) {
  fsfs_inode_t *x;
  if (!m) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0) return -1;
  if (x->nlink > 0) x->nlink--;
  if (x->nlink == 0) {
    fsfs_extent_free_inode(m, ino);
    x->dead = 1;
    x->size = 0;
  }
  return 0;
}
int fsfs_dir_add(fsfs_mount_t *m, uint64_t dino, char *name, uint64_t c, uint8_t t) {
  int r;
  if (!m || !name) return -1;
  r = fsfs_btree_insert(m, dino, name, c, t);
  if (r != 0) return r;
  if (c < FSFS_MAX_INODES && m->inodes[c].ino) m->inodes[c].nlink++;
  return 0;
}
int fsfs_dir_del(fsfs_mount_t *m, uint64_t dino, char *name) {
  uint64_t c = 0;
  uint8_t t = 0;
  int r;
  if (!m || !name) return -1;
  r = fsfs_btree_lookup(m, dino, name, &c, &t);
  if (r != 0) return r;
  r = fsfs_btree_remove(m, dino, name);
  if (r != 0) return r;
  if (c < FSFS_MAX_INODES && m->inodes[c].ino) fsfs_inode_unlink(m, c);
  return 0;
}
int fsfs_inode_read(fsfs_mount_t *m, uint64_t ino, uint64_t off, void *buf, size_t n) {
  fsfs_inode_t *x;
  uint8_t *p;
  size_t done = 0;
  size_t left;
  if (!m || !buf) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0 || x->dead) return -1;
  if (off >= x->size) return 0;
  if (off + n > x->size) n = (size_t)(x->size - off);
  if (x->inline_used && x->n_exts == 0) {
    memcpy(buf, x->inline_buf + off, n);
    return (int)n;
  }
  p = (uint8_t *)buf;
  left = n;
  while (left > 0) {
    uint64_t cur = off + done;
    uint64_t blkoff = (cur / FSFS_BLOCK) * FSFS_BLOCK;
    uint64_t inblk = cur % FSFS_BLOCK;
    size_t chunk = FSFS_BLOCK - (size_t)inblk;
    uint32_t k;
    int f = -1;
    uint8_t ob[FSFS_BLOCK];
    uint8_t blk[FSFS_BLOCK];
    uint32_t cc;
    if (chunk > left) chunk = left;
    for (k = 0; k < x->n_exts; k++) if (x->exts[k].l_off == blkoff) { f = (int)k; break; }
    if (f < 0) {
      memset(p + done, 0, chunk);
      done += chunk;
      left -= chunk;
      continue;
    }
    if (fsfs_blk_read(m, x->exts[f].p_blk, ob) != 0) return -1;
    if (x->exts[f].comp != FSFS_COMP_NONE) {
      int dl = fsfs_decompress_blk(ob, FSFS_BLOCK, blk, FSFS_BLOCK, x->exts[f].comp);
      if (dl < 0) return -1;
      if ((size_t)dl < FSFS_BLOCK) memset(blk + dl, 0, FSFS_BLOCK - dl);
    } else memcpy(blk, ob, FSFS_BLOCK);
    cc = fsfs_crc32c(blk, FSFS_BLOCK);
    if (cc != x->exts[f].crc && x->exts[f].len == FSFS_BLOCK) return -1;
    memcpy(p + done, blk + inblk, chunk);
    done += chunk;
    left -= chunk;
  }
  return (int)n;
}
int fsfs_inode_write_small(fsfs_mount_t *m, uint64_t ino, uint64_t off, void *buf, size_t n) {
  fsfs_inode_t *x = &m->inodes[ino];
  memcpy(x->inline_buf + off, buf, n);
  if (off + n > x->size) x->size = off + n;
  x->inline_used = 1;
  x->mtime = (uint64_t)time(0);
  x->seq++;
  fsfs_sha256(x->inline_buf, x->size > 256 ? 256 : x->size, x->sha_all);
  return 0;
}

int fsfs_inode_write(fsfs_mount_t *m, uint64_t ino, uint64_t off, void *buf, size_t n) {
  fsfs_inode_t *x;
  uint8_t *p;
  size_t left;
  size_t done = 0;
  if (!m || !buf) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &m->inodes[ino];
  if (x->ino == 0 || x->dead) return -1;
  if (x->is_dir) return -1;
  if (off + n <= FSFS_INLINE_SZ && x->n_exts == 0) return fsfs_inode_write_small(m, ino, off, buf, n);
  if (x->inline_used && x->n_exts == 0) {
    uint8_t tmp[FSFS_INLINE_SZ];
    size_t osz = (size_t)x->size;
    memcpy(tmp, x->inline_buf, osz);
    memset(x->inline_buf, 0, FSFS_INLINE_SZ);
    x->inline_used = 0;
    x->size = 0;
    if (fsfs_extent_alloc_for(m, ino, 0, FSFS_BLOCK, -1) != 0) return -1;
    if (osz > 0) {
      uint32_t kk;
      int ff = -1;
      uint8_t blk[FSFS_BLOCK];
      for (kk = 0; kk < x->n_exts; kk++) if (x->exts[kk].l_off == 0) ff = kk;
      if (ff >= 0) {
        memset(blk, 0, FSFS_BLOCK);
        memcpy(blk, tmp, osz);
        fsfs_blk_write(m, x->exts[ff].p_blk, blk);
        x->exts[ff].crc = fsfs_crc32c(blk, FSFS_BLOCK);
      }
      x->size = osz;
    }
  }
  p = (uint8_t *)buf;
  left = n;
  while (left > 0) {
    uint64_t cur = off + done;
    uint64_t blkoff = (cur / FSFS_BLOCK) * FSFS_BLOCK;
    uint64_t inblk = cur % FSFS_BLOCK;
    size_t chunk = FSFS_BLOCK - (size_t)inblk;
    uint32_t k;
    int f = -1;
    uint8_t blk[FSFS_BLOCK];
    uint8_t ob[FSFS_BLOCK];
    if (chunk > left) chunk = left;
    for (k = 0; k < x->n_exts; k++) if (x->exts[k].l_off == blkoff) { f = (int)k; break; }
    if (f < 0) {
      if (fsfs_extent_alloc_for(m, ino, blkoff, FSFS_BLOCK, -1) != 0) return -1;
      for (k = 0; k < x->n_exts; k++) if (x->exts[k].l_off == blkoff) { f = (int)k; break; }
      if (f < 0) return -1;
    } else {
      if (fsfs_extent_cow(m, ino, blkoff) != 0) return -1;
    }
    if (fsfs_blk_read(m, x->exts[f].p_blk, ob) != 0) memset(ob, 0, FSFS_BLOCK);
    if (x->exts[f].comp != FSFS_COMP_NONE) {
      uint8_t dd[FSFS_BLOCK];
      int dl = fsfs_decompress_blk(ob, FSFS_BLOCK, dd, FSFS_BLOCK, x->exts[f].comp);
      if (dl > 0) { memset(blk, 0, FSFS_BLOCK); memcpy(blk, dd, dl < FSFS_BLOCK ? dl : FSFS_BLOCK); }
      else memcpy(blk, ob, FSFS_BLOCK);
    } else memcpy(blk, ob, FSFS_BLOCK);
    memcpy(blk + inblk, p + done, chunk);
    if (x->exts[f].comp != FSFS_COMP_NONE) {
      uint8_t cb[FSFS_BLOCK];
      int cl = fsfs_compress_blk(blk, FSFS_BLOCK, cb, FSFS_BLOCK, x->exts[f].comp);
      if (cl > 0) { memset(ob, 0, FSFS_BLOCK); memcpy(ob, cb, cl); fsfs_blk_write(m, x->exts[f].p_blk, ob); }
      else { x->exts[f].comp = FSFS_COMP_NONE; fsfs_blk_write(m, x->exts[f].p_blk, blk); }
    } else fsfs_blk_write(m, x->exts[f].p_blk, blk);
    x->exts[f].crc = fsfs_crc32c(blk, FSFS_BLOCK);
    fsfs_sha256(blk, FSFS_BLOCK, x->exts[f].merkle);
    done += chunk;
    left -= chunk;
  }
  if (off + n > x->size) x->size = off + n;
  x->mtime = (uint64_t)time(0);
  x->seq++;
  return 0;
}

