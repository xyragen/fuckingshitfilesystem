#include "fsfs.h"
#include <string.h>
int fsfs_journal_append(fsfs_mount_t *m, fsfs_jrec_t *r) {
  uint64_t pos;
  uint8_t blk[FSFS_BLOCK];
  if (!m || !r) return -1;
  r->seq = ++m->super.txn_seq;
  r->crc = fsfs_crc32c(r->data, r->len > 4000 ? 4000 : r->len);
  pos = m->super.journal_head % FSFS_JOURNAL_BLOCKS;
  memset(blk, 0, FSFS_BLOCK);
  memcpy(blk, r, sizeof(fsfs_jrec_t) > FSFS_BLOCK ? FSFS_BLOCK : sizeof(fsfs_jrec_t));
  if (m->journal) memcpy(m->journal + pos * FSFS_BLOCK, blk, FSFS_BLOCK);
  m->super.journal_head++;
  if (m->super.journal_head - m->super.journal_tail > FSFS_JOURNAL_BLOCKS)
    m->super.journal_tail = m->super.journal_head - FSFS_JOURNAL_BLOCKS;
  return 0;
}
int fsfs_journal_replay(fsfs_mount_t *m) {
  uint64_t i;
  int n = 0;
  if (!m) return -1;
  for (i = m->super.journal_tail; i < m->super.journal_head; i++) {
    fsfs_jrec_t *r;
    uint32_t c;
    uint64_t pos = i % FSFS_JOURNAL_BLOCKS;
    if (!m->journal) break;
    r = (fsfs_jrec_t *)(m->journal + pos * FSFS_BLOCK);
    if (r->op != 1 && r->op != 2) continue;
    if (r->len > 4000) continue;
    c = fsfs_crc32c(r->data, r->len);
    if (c != r->crc) continue;
    if (r->op == 1) {
      if (r->a < FSFS_MAX_INODES && m->inodes[r->a].ino) {
        fsfs_inode_write(m, r->a, r->b, r->data, r->len);
        n++;
      }
    } else if (r->op == 2) {
      uint64_t ni = fsfs_inode_create(m, (uint32_t)r->b, 0);
      if (ni) { fsfs_dir_add(m, r->a, (char *)r->data, ni, 1); n++; }
    }
    if (n > 512) break;
  }
  return n;
}
