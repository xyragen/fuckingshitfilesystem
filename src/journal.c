#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
int fsfs_journal_append(struct super_block *sb, fsfs_jrec_t *r) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 pos;
  __u8 blk[FSFS_BLOCK];
  if (!r) return -1;
  r->seq = ++sbi->super.txn_seq;
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
int fsfs_journal_replay(struct super_block *sb) {
  struct fsfs_sb_info *sbi = FSFS_SB(sb);
  __u64 i;
  int n = 0;
  for (i = sbi->super.journal_tail; i < sbi->super.journal_head; i++) {
    fsfs_jrec_t *r;
    __u32 c;
    __u64 pos = i % FSFS_JOURNAL_BLOCKS;
    if (!sbi->journal) break;
    r = (fsfs_jrec_t *)(sbi->journal + pos * FSFS_BLOCK);
    if (r->op != 1 && r->op != 2) continue;
    if (r->len > 4000) continue;
    c = fsfs_crc32c(r->data, r->len);
    if (c != r->crc) continue;
    if (r->op == 1) {
      if (r->a < FSFS_MAX_INODES && sbi->inodes[r->a].ino) {
        fsfs_inode_write(sb, r->a, r->b, r->data, r->len);
        n++;
      }
    } else if (r->op == 2) {
      __u64 ni = fsfs_inode_create(sb, (__u32)r->b, 0);
      if (ni) { fsfs_dir_add(sb, r->a, (char *)r->data, ni, 1); n++; }
    }
    if (n > 512) break;
  }
  return n;
}
#endif
