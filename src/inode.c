#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/cred.h>
#include <linux/fs.h>
static __u64 fsfs_now2(void) {
  return (__u64)ktime_get_real_seconds();
}
__u64 fsfs_inode_create(struct super_block *sb, __u32 mode, int is_dir) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 i;
  fsfs_inode_t *x;
  for (i = 1; i < FSFS_MAX_INODES; i++) {
    __u64 cand = (sbi->super.next_ino + i) % FSFS_MAX_INODES;
    if (cand == 0) continue;
    if (sbi->inodes[cand].ino == 0 || sbi->inodes[cand].dead) {
      x = &sbi->inodes[cand];
      memset(x, 0, sizeof(*x));
      x->ino = cand;
      x->mode = mode;
      x->nlink = 1;
      x->uid = (__u32)from_kuid(&init_user_ns, current_uid());
      x->gid = (__u32)from_kgid(&init_user_ns, current_gid());
      x->size = 0;
      x->atime = x->mtime = x->ctime = fsfs_now2();
      x->gen = sbi->cur_gen;
      x->seq = 1;
      x->comp_pol = FSFS_XMODE_LAT;
      x->xmode = FSFS_XMODE_LAT;
      x->is_dir = is_dir ? 1 : 0;
      x->crc_meta = fsfs_crc32c(x, 128);
      sbi->super.next_ino = cand + 1;
      if (sbi->super.next_ino >= FSFS_MAX_INODES) sbi->super.next_ino = 1;
      return cand;
    }
  }
  return 0;
}
int fsfs_inode_resize(struct super_block *sb, __u64 ino, __u64 sz) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0) return -1;
  if (sz < x->size) {
    __u64 nb = (sz + FSFS_BLOCK - 1) / FSFS_BLOCK;
    __u32 k = 0;
    while (k < x->n_exts) {
      __u64 idx = x->exts[k].l_off / FSFS_BLOCK;
      if (idx >= nb) {
        __u32 j;
        fsfs_blk_decref(sb, x->exts[k].p_blk);
        for (j = k; j + 1 < x->n_exts; j++) x->exts[j] = x->exts[j + 1];
        x->n_exts--;
      } else k++;
    }
  }
  x->size = sz;
  x->seq++;
  return 0;
}
int fsfs_inode_unlink(struct super_block *sb, __u64 ino) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0) return -1;
  if (x->nlink > 0) x->nlink--;
  if (x->nlink == 0) {
    fsfs_extent_free_inode(sb, ino);
    x->dead = 1;
    x->size = 0;
  }
  return 0;
}
int fsfs_dir_add(struct super_block *sb, __u64 dino, char *name, __u64 c, __u8 t) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int r;
  if (!name) return -1;
  r = fsfs_btree_insert(sb, dino, name, c, t);
  if (r != 0) return r;
  if (c < FSFS_MAX_INODES && sbi->inodes[c].ino) sbi->inodes[c].nlink++;
  return 0;
}
int fsfs_dir_del(struct super_block *sb, __u64 dino, char *name) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 c = 0;
  __u8 t = 0;
  int r;
  if (!name) return -1;
  r = fsfs_btree_lookup(sb, dino, name, &c, &t);
  if (r != 0) return r;
  r = fsfs_btree_remove(sb, dino, name);
  if (r != 0) return r;
  if (c < FSFS_MAX_INODES && sbi->inodes[c].ino) fsfs_inode_unlink(sb, c);
  return 0;
}
int fsfs_inode_read(struct super_block *sb, __u64 ino, __u64 off, void *buf, size_t n) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __u8 *p;
  __u8 *ob;
  __u8 *dd;
  size_t done = 0;
  size_t left;
  int rc = -1;
  if (!buf) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0 || x->dead) return -1;
  if (off >= x->size) return 0;
  if (off + n > x->size) n = (size_t)(x->size - off);
  if (x->inline_used && x->n_exts == 0) {
    memcpy(buf, x->inline_buf + off, n);
    return (int)n;
  }
  ob = kvmalloc(FSFS_BLOCK * 2, GFP_NOFS);
  if (!ob) return -ENOMEM;
  dd = ob + FSFS_BLOCK;
  p = (__u8 *)buf;
  left = n;
  while (left > 0) {
    __u64 cur = off + done;
    __u64 blkoff = (cur / FSFS_BLOCK) * FSFS_BLOCK;
    __u64 inblk = cur % FSFS_BLOCK;
    size_t chunk = FSFS_BLOCK - (size_t)inblk;
    __u32 k;
    int f = -1;
    if (chunk > left) chunk = left;
    for (k = 0; k < x->n_exts; k++) if (x->exts[k].l_off == blkoff) { f = (int)k; break; }
    if (f < 0) { memset(p + done, 0, chunk); }
    else {
      if (fsfs_blk_read(sb, x->exts[f].p_blk, ob) != 0) goto out;
      if (x->exts[f].comp != FSFS_COMP_NONE) {
        int dl = fsfs_decompress_blk(ob, FSFS_BLOCK, dd, FSFS_BLOCK, x->exts[f].comp);
        if (dl < 0) goto out;
        memset(dd + dl, 0, FSFS_BLOCK - dl);
        memcpy(p + done, dd + inblk, chunk);
      } else memcpy(p + done, ob + inblk, chunk);
    }
    done += chunk;
    left -= chunk;
  }
  rc = (int)n;
out:
  kvfree(ob);
  return rc;
}
static int fsfs_inode_write_small(struct super_block *sb, __u64 ino, __u64 off, void *buf, size_t n) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x = &sbi->inodes[ino];
  memcpy(x->inline_buf + off, buf, n);
  if (off + n > x->size) x->size = off + n;
  x->inline_used = 1;
  x->mtime = fsfs_now2();
  x->seq++;
  fsfs_sha256(x->inline_buf, x->size > 256 ? 256 : x->size, x->sha_all);
  return 0;
}
int fsfs_inode_write(struct super_block *sb, __u64 ino, __u64 off, void *buf, size_t n) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *x;
  __u8 *p;
  __u8 *scratch;
  __u8 *blk;
  __u8 *ob;
  __u8 *dd;
  __u8 *cb;
  size_t left;
  size_t done = 0;
  int rc = -1;
  if (!buf) return -1;
  if (ino >= FSFS_MAX_INODES) return -1;
  x = &sbi->inodes[ino];
  if (x->ino == 0 || x->dead) return -1;
  if (x->is_dir) return -1;
  if (off + n <= FSFS_INLINE_SZ && x->n_exts == 0) return fsfs_inode_write_small(sb, ino, off, buf, n);
  scratch = kvmalloc(FSFS_BLOCK * 4, GFP_NOFS);
  if (!scratch) return -ENOMEM;
  blk = scratch;
  ob = scratch + FSFS_BLOCK;
  dd = scratch + FSFS_BLOCK * 2;
  cb = scratch + FSFS_BLOCK * 3;
  if (x->inline_used && x->n_exts == 0) {
    __u8 tmp[FSFS_INLINE_SZ];
    size_t osz = (size_t)x->size;
    memcpy(tmp, x->inline_buf, osz);
    memset(x->inline_buf, 0, FSFS_INLINE_SZ);
    x->inline_used = 0;
    x->size = 0;
    if (fsfs_extent_alloc_for(sb, ino, 0, FSFS_BLOCK, -1) != 0) goto out;
    if (osz > 0) {
      __u32 kk;
      int ff = -1;
      for (kk = 0; kk < x->n_exts; kk++) if (x->exts[kk].l_off == 0) ff = kk;
      if (ff >= 0) {
        memset(blk, 0, FSFS_BLOCK);
        memcpy(blk, tmp, osz);
        if (fsfs_blk_write(sb, x->exts[ff].p_blk, blk) != 0) goto out;
        x->exts[ff].crc = fsfs_crc32c(blk, FSFS_BLOCK);
      }
      x->size = osz;
    }
  }
  p = (__u8 *)buf;
  left = n;
  while (left > 0) {
    __u64 cur = off + done;
    __u64 blkoff = (cur / FSFS_BLOCK) * FSFS_BLOCK;
    __u64 inblk = cur % FSFS_BLOCK;
    size_t chunk = FSFS_BLOCK - (size_t)inblk;
    __u32 k;
    int f = -1;
    if (chunk > left) chunk = left;
    for (k = 0; k < x->n_exts; k++) if (x->exts[k].l_off == blkoff) { f = (int)k; break; }
    if (f < 0) {
      if (fsfs_extent_alloc_for(sb, ino, blkoff, FSFS_BLOCK, -1) != 0) goto out;
      for (k = 0; k < x->n_exts; k++) if (x->exts[k].l_off == blkoff) { f = (int)k; break; }
      if (f < 0) goto out;
    } else {
      if (fsfs_extent_cow(sb, ino, blkoff) != 0) goto out;
    }
    if (fsfs_blk_read(sb, x->exts[f].p_blk, ob) != 0) goto out;
    if (x->exts[f].comp != FSFS_COMP_NONE) {
      int dl = fsfs_decompress_blk(ob, FSFS_BLOCK, dd, FSFS_BLOCK, x->exts[f].comp);
      if (dl < 0) goto out;
      memset(dd + dl, 0, FSFS_BLOCK - dl);
      memset(blk, 0, FSFS_BLOCK);
      memcpy(blk, dd, dl < FSFS_BLOCK ? dl : FSFS_BLOCK);
    } else memcpy(blk, ob, FSFS_BLOCK);
    memcpy(blk + inblk, p + done, chunk);
    if (x->exts[f].comp != FSFS_COMP_NONE) {
      int cl = fsfs_compress_blk(blk, FSFS_BLOCK, cb, FSFS_BLOCK, x->exts[f].comp);
      if (cl > 0) { memset(ob, 0, FSFS_BLOCK); memcpy(ob, cb, cl); if (fsfs_blk_write(sb, x->exts[f].p_blk, ob) != 0) goto out; }
      else { x->exts[f].comp = FSFS_COMP_NONE; if (fsfs_blk_write(sb, x->exts[f].p_blk, blk) != 0) goto out; }
    } else if (fsfs_blk_write(sb, x->exts[f].p_blk, blk) != 0) goto out;
    x->exts[f].crc = fsfs_crc32c(blk, FSFS_BLOCK);
    fsfs_sha256(blk, FSFS_BLOCK, x->exts[f].merkle);
    done += chunk;
    left -= chunk;
  }
  if (off + n > x->size) x->size = off + n;
  x->mtime = fsfs_now2();
  x->seq++;
  rc = 0;
out:
  kvfree(scratch);
  return rc;
}
#endif

