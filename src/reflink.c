#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
int fsfs_reflink_file(struct super_block *sb, __u64 src, __u64 dst_dir, char *nm, __u64 *newino) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  fsfs_inode_t *s;
  __u64 ni;
  fsfs_inode_t *d;
  __u32 k;
  if (!nm) return -1;
  if (src >= FSFS_MAX_INODES || dst_dir >= FSFS_MAX_INODES) return -1;
  s = &sbi->inodes[src];
  if (s->ino == 0 || s->dead || s->is_dir) return -1;
  ni = fsfs_inode_create(sb, s->mode, 0);
  if (ni == 0) return -1;
  d = &sbi->inodes[ni];
  d->size = s->size;
  d->comp_pol = s->comp_pol;
  d->xmode = s->xmode;
  d->quota = s->quota;
  d->reserv = s->reserv;
  if (s->inline_used && s->n_exts == 0) {
    memcpy(d->inline_buf, s->inline_buf, FSFS_INLINE_SZ);
    d->inline_used = 1;
    memcpy(d->sha_all, s->sha_all, 32);
  } else {
    for (k = 0; k < s->n_exts; k++) {
      d->exts[k] = s->exts[k];
      fsfs_blk_incref(sb, s->exts[k].p_blk);
      d->exts[k].refs++;
    }
    d->n_exts = s->n_exts;
    memcpy(d->sha_all, s->sha_all, 32);
  }
  d->gen = sbi->cur_gen;
  d->seq = 1;
  if (fsfs_dir_add(sb, dst_dir, nm, ni, 1) != 0) {
    d->dead = 1;
    return -1;
  }
  if (newino) *newino = ni;
  return 0;
}
int fsfs_reflink_dedup(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int j;
  int merged = 0;
  for (i = 0; i < FSFS_MAX_INODES; i++) {
    fsfs_inode_t *a = &sbi->inodes[i];
    __u32 ka;
    if (a->ino == 0 || a->dead || a->is_dir) continue;
    if (a->inline_used) continue;
    for (j = i + 1; j < FSFS_MAX_INODES; j++) {
      fsfs_inode_t *b = &sbi->inodes[j];
      __u32 kb;
      if (b->ino == 0 || b->dead || b->is_dir) continue;
      if (b->inline_used) continue;
      if (a->size != b->size) continue;
      if (a->n_exts != b->n_exts) continue;
      if (a->n_exts == 0) continue;
      if (memcmp(a->sha_all, b->sha_all, 8) != 0 && a->seq != b->seq) {
        int same = 1;
        for (ka = 0; ka < a->n_exts; ka++) {
          if (memcmp(a->exts[ka].merkle, b->exts[ka].merkle, 32) != 0) { same = 0; break; }
        }
        if (!same) continue;
      }
      for (ka = 0; ka < a->n_exts; ka++) {
        int fb = -1;
        for (kb = 0; kb < b->n_exts; kb++) {
          if (b->exts[kb].l_off == a->exts[ka].l_off) { fb = kb; break; }
        }
        if (fb >= 0) {
          if (b->exts[fb].p_blk != a->exts[ka].p_blk) {
            fsfs_blk_decref(sb, b->exts[fb].p_blk);
            b->exts[fb].p_blk = a->exts[ka].p_blk;
            fsfs_blk_incref(sb, a->exts[ka].p_blk);
            b->exts[fb].crc = a->exts[ka].crc;
            memcpy(b->exts[fb].merkle, a->exts[ka].merkle, 32);
            merged++;
          }
        }
      }
      if (merged > 128) return merged;
    }
  }
  return merged;
}
#endif
