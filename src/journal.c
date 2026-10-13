#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
int fsfs_journal_append(struct super_block *sb, fsfs_jrec_t *r) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 pos;
  __u8 blk[FSFS_BLOCK];
  if (!r) return -1;
  r->seq = sbi->super.journal_head;
  r->crc = fsfs_crc32c(r->data, r->len > 4000 ? 4000 : r->len);
  pos = sbi->super.journal_head % FSFS_JOURNAL_BLOCKS;
  memset(blk, 0, FSFS_BLOCK);
  memcpy(blk, r, sizeof(fsfs_jrec_t) > FSFS_BLOCK ? FSFS_BLOCK : sizeof(fsfs_jrec_t));
  if (sbi->journal) memcpy(sbi->journal + pos * FSFS_BLOCK, blk, FSFS_BLOCK);
  sbi->super.journal_head++;
  if (sbi->super.journal_head - sbi->super.journal_tail > FSFS_JOURNAL_BLOCKS)
    sbi->super.journal_tail = sbi->super.journal_head - FSFS_JOURNAL_BLOCKS;
  return 0;
}
static int fsfs_jrec_valid(fsfs_jrec_t *r) {
  __u32 c;
  if (r->op != FSFS_JOP_WRITE && r->op != FSFS_JOP_CREATE && r->op != FSFS_JOP_COMMIT)
    return 0;
  if (r->len > 4000) return 0;
  c = fsfs_crc32c(r->data, r->len);
  return c == r->crc;
}
static int fsfs_apply_one(struct super_block *sb, fsfs_jrec_t *r) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  if (r->op == FSFS_JOP_WRITE) {
    if (r->a < FSFS_MAX_INODES && sbi->inodes[r->a].ino) {
      if (fsfs_inode_write(sb, r->a, r->b, r->data, r->len) != 0) return -1;
    }
  } else if (r->op == FSFS_JOP_CREATE) {
    __u64 ni;
    if (fsfs_btree_lookup(sb, r->a, (char *)r->data, NULL, NULL) == 0) return 0;
    ni = fsfs_inode_create(sb, (__u32)r->b, 0);
    if (!ni) return -1;
    if (fsfs_dir_add(sb, r->a, (char *)r->data, ni, 1) != 0) {
      sbi->inodes[ni].dead = 1;
      return -1;
    }
  }
  return 0;
}
int fsfs_journal_replay(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 i;
  __u64 committed[FSFS_JOURNAL_BLOCKS];
  int ncommit = 0;
  int n = 0;
  if (!sbi->journal) return 0;
  for (i = sbi->super.journal_tail; i < sbi->super.journal_head; i++) {
    fsfs_jrec_t *r = (fsfs_jrec_t *)(sbi->journal + (i % FSFS_JOURNAL_BLOCKS) * FSFS_BLOCK);
    if (r->op == 0) continue;
    if (!fsfs_jrec_valid(r)) continue;
    if (r->op == FSFS_JOP_COMMIT) {
      if (ncommit < FSFS_JOURNAL_BLOCKS) committed[ncommit++] = r->txn;
    }
  }
  for (i = sbi->super.journal_tail; i < sbi->super.journal_head; i++) {
    fsfs_jrec_t *r = (fsfs_jrec_t *)(sbi->journal + (i % FSFS_JOURNAL_BLOCKS) * FSFS_BLOCK);
    int k;
    int ok = 0;
    if (r->op == 0) continue;
    if (r->op != FSFS_JOP_WRITE && r->op != FSFS_JOP_CREATE) continue;
    if (!fsfs_jrec_valid(r)) continue;
    for (k = 0; k < ncommit; k++) {
      if (committed[k] == r->txn) { ok = 1; break; }
    }
    if (!ok) continue;
    if (fsfs_apply_one(sb, r) == 0) n++;
    if (n > 4096) break;
  }
  return n;
}
#endif
