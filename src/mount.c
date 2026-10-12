#include "fsfs.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
int fsfs_mount_open(char *img, fsfs_mount_t *m) {
  int r;
  if (!img || !m) return -1;
  memset(m, 0, sizeof(*m));
  m->fd = open(img, O_RDWR);
  if (m->fd < 0) return -1;
  strncpy(m->path, img, 1023);
  r = fsfs_image_load(m);
  if (r != 0) { close(m->fd); return -1; }
  if (!m->inodes) {
    m->inodes = malloc(sizeof(fsfs_inode_t) * FSFS_MAX_INODES);
    if (!m->inodes) { close(m->fd); return -1; }
    memset(m->inodes, 0, sizeof(fsfs_inode_t) * FSFS_MAX_INODES);
  }
  if (!m->snaps) {
    m->snaps = malloc(sizeof(fsfs_snap_t) * FSFS_MAX_SNAPS);
    if (!m->snaps) { close(m->fd); return -1; }
    memset(m->snaps, 0, sizeof(fsfs_snap_t) * FSFS_MAX_SNAPS);
  }
  if (!m->journal) {
    m->journal = malloc(FSFS_BLOCK * FSFS_JOURNAL_BLOCKS);
    if (m->journal) memset(m->journal, 0, FSFS_BLOCK * FSFS_JOURNAL_BLOCKS);
  }
  if (!m->blk_ref) {
    m->blk_ref = malloc(m->super.total_blocks * sizeof(uint32_t));
    if (m->blk_ref) {
      uint64_t i;
      for (i = 0; i < m->super.total_blocks; i++) m->blk_ref[i] = m->blk_bmp[i] ? 1 : 0;
    }
  }
  m->mounted = 1;
  m->cur_gen = m->super.next_gen + 1;
  fsfs_journal_replay(m);
  return 0;
}
int fsfs_mount_close(fsfs_mount_t *m) {
  if (!m) return -1;
  if (!m->mounted) return 0;
  fsfs_image_sync(m);
  if (m->blk_bmp) free(m->blk_bmp);
  if (m->blk_ref) free(m->blk_ref);
  if (m->inodes) free(m->inodes);
  if (m->snaps) free(m->snaps);
  if (m->journal) free(m->journal);
  close(m->fd);
  m->mounted = 0;
  return 0;
}
