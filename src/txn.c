#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/time.h>
int fsfs_txn_begin(struct super_block *sb, fsfs_txn_t *t) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (!t) return -1;
  memset(t, 0, sizeof(*t));
  t->id = ++sbi->super.txn_seq;
  t->state = FSFS_TXN_ACTIVE;
  t->stamp = (__u64)ktime_get_real_seconds();
  return 0;
}
int fsfs_txn_add_write(fsfs_txn_t *t, __u64 ino, __u64 off, void *d, size_t n) {
  fsfs_jrec_t *r;
  if (!t || !d) return -1;
  if (t->state != FSFS_TXN_ACTIVE) return -1;
  if (t->n_ops >= FSFS_TXN_MAX_OPS) return -1;
  if (n > 3800) return -1;
  r = &t->ops[t->n_ops++];
  r->seq = t->n_ops;
  r->op = 1;
  r->len = n;
  r->a = ino;
  r->b = off;
  memcpy(r->data, d, n);
  r->crc = fsfs_crc32c(r->data, n);
  return 0;
}
int fsfs_txn_add_create(fsfs_txn_t *t, __u64 dino, char *name, __u32 mode) {
  fsfs_jrec_t *r;
  size_t nl;
  if (!t || !name) return -1;
  if (t->state != FSFS_TXN_ACTIVE) return -1;
  if (t->n_ops >= FSFS_TXN_MAX_OPS) return -1;
  nl = strlen(name);
  if (nl > 200) return -1;
  r = &t->ops[t->n_ops++];
  r->seq = t->n_ops;
  r->op = 2;
  r->a = dino;
  r->b = mode;
  memcpy(r->data, name, nl + 1);
  r->len = nl + 1;
  r->crc = fsfs_crc32c(r->data, r->len);
  return 0;
}
int fsfs_txn_commit(struct super_block *sb, fsfs_txn_t *t) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  int i;
  int j;
  fsfs_inode_t bak[FSFS_TXN_MAX_OPS];
  __u64 bina[FSFS_TXN_MAX_OPS];
  int nbak = 0;
  if (!t) return -1;
  if (t->state != FSFS_TXN_ACTIVE) return -1;
  for (i = 0; i < t->n_ops; i++) {
    fsfs_jrec_t *r = &t->ops[i];
    __u32 c = fsfs_crc32c(r->data, r->len);
    if (c != r->crc) { fsfs_txn_abort(sb, t); return -1; }
    if (fsfs_journal_append(sb, r) != 0) { fsfs_txn_abort(sb, t); return -1; }
  }
  for (i = 0; i < t->n_ops; i++) {
    fsfs_jrec_t *r = &t->ops[i];
    if (r->op == 1) {
      __u64 ino = r->a;
      if (ino < FSFS_MAX_INODES && sbi->inodes[ino].ino) {
        bina[nbak] = ino;
        bak[nbak++] = sbi->inodes[ino];
        if (fsfs_inode_write(sb, ino, r->b, r->data, r->len) != 0) {
          for (j = 0; j < nbak - 1; j++) sbi->inodes[bina[j]] = bak[j];
          t->state = FSFS_TXN_ABORTED;
          return -1;
        }
      }
    } else if (r->op == 2) {
      __u64 ni = fsfs_inode_create(sb, (__u32)r->b, 0);
      if (ni == 0) {
        for (j = 0; j < nbak; j++) sbi->inodes[bina[j]] = bak[j];
        t->state = FSFS_TXN_ABORTED;
        return -1;
      }
      if (fsfs_dir_add(sb, r->a, (char *)r->data, ni, 1) != 0) {
        for (j = 0; j < nbak; j++) sbi->inodes[bina[j]] = bak[j];
        sbi->inodes[ni].dead = 1;
        t->state = FSFS_TXN_ABORTED;
        return -1;
      }
    }
  }
  t->state = FSFS_TXN_COMMITTED;
  return 0;
}
int fsfs_txn_abort(struct super_block *sb, fsfs_txn_t *t) {
  if (!t) return -1;
  t->state = FSFS_TXN_ABORTED;
  t->n_ops = 0;
  return 0;
}
#endif
