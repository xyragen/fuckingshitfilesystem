#include "fsfs.h"
#include <string.h>
int fsfs_txn_begin(fsfs_mount_t *m, fsfs_txn_t *t) {
  if (!m || !t) return -1;
  memset(t, 0, sizeof(*t));
  t->id = ++m->super.txn_seq;
  t->state = FSFS_TXN_ACTIVE;
  t->stamp = (uint64_t)time(0);
  return 0;
}
int fsfs_txn_add_write(fsfs_txn_t *t, uint64_t ino, uint64_t off, void *d, size_t n) {
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
int fsfs_txn_add_create(fsfs_txn_t *t, uint64_t dino, char *name, uint32_t mode) {
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
int fsfs_txn_commit(fsfs_mount_t *m, fsfs_txn_t *t) {
  int i;
  int j;
  fsfs_inode_t bak[FSFS_TXN_MAX_OPS];
  uint64_t bina[FSFS_TXN_MAX_OPS];
  int nbak = 0;
  if (!m || !t) return -1;
  if (t->state != FSFS_TXN_ACTIVE) return -1;
  for (i = 0; i < t->n_ops; i++) {
    fsfs_jrec_t *r = &t->ops[i];
    uint32_t c = fsfs_crc32c(r->data, r->len);
    if (c != r->crc) { fsfs_txn_abort(m, t); return -1; }
    if (fsfs_journal_append(m, r) != 0) { fsfs_txn_abort(m, t); return -1; }
  }
  for (i = 0; i < t->n_ops; i++) {
    fsfs_jrec_t *r = &t->ops[i];
    if (r->op == 1) {
      uint64_t ino = r->a;
      if (ino < FSFS_MAX_INODES && m->inodes[ino].ino) {
        bina[nbak] = ino;
        bak[nbak++] = m->inodes[ino];
        if (fsfs_inode_write(m, ino, r->b, r->data, r->len) != 0) {
          for (j = 0; j < nbak - 1; j++) m->inodes[bina[j]] = bak[j];
          t->state = FSFS_TXN_ABORTED;
          return -1;
        }
      }
    } else if (r->op == 2) {
      uint64_t ni = fsfs_inode_create(m, (uint32_t)r->b, 0);
      if (ni == 0) {
        for (j = 0; j < nbak; j++) m->inodes[bina[j]] = bak[j];
        t->state = FSFS_TXN_ABORTED;
        return -1;
      }
      if (fsfs_dir_add(m, r->a, (char *)r->data, ni, 1) != 0) {
        for (j = 0; j < nbak; j++) m->inodes[bina[j]] = bak[j];
        m->inodes[ni].dead = 1;
        t->state = FSFS_TXN_ABORTED;
        return -1;
      }
    }
  }
  t->state = FSFS_TXN_COMMITTED;
  fsfs_image_sync(m);
  return 0;
}
int fsfs_txn_abort(fsfs_mount_t *m, fsfs_txn_t *t) {
  (void)m;
  if (!t) return -1;
  t->state = FSFS_TXN_ABORTED;
  t->n_ops = 0;
  return 0;
}
