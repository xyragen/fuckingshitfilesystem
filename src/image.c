#include "fsfs.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
int fsfs_blk_read(fsfs_mount_t *m, uint64_t blk, void *out) {
  off_t o = 0;
  ssize_t r = 0;
  ssize_t tot = 0;
  if (!m || !out) return -1;
  if (blk >= m->super.total_blocks) return -1;
  o = (off_t)(blk * FSFS_BLOCK);
  if (lseek(m->fd, o, SEEK_SET) != o) return -1;
  tot = 0;
  while (tot < FSFS_BLOCK) {
    r = read(m->fd, (uint8_t *)out + tot, FSFS_BLOCK - tot);
    if (r <= 0) return -1;
    tot += r;
  }
  return 0;
}
int fsfs_blk_write(fsfs_mount_t *m, uint64_t blk, void *in) {
  off_t o = 0;
  ssize_t r = 0;
  ssize_t tot = 0;
  if (!m || !in) return -1;
  if (blk >= m->super.total_blocks) return -1;
  o = (off_t)(blk * FSFS_BLOCK);
  if (lseek(m->fd, o, SEEK_SET) != o) return -1;
  tot = 0;
  while (tot < FSFS_BLOCK) {
    r = write(m->fd, (uint8_t *)in + tot, FSFS_BLOCK - tot);
    if (r <= 0) return -1;
    tot += r;
  }
  return 0;
}
static int zone_of(fsfs_mount_t *m, uint64_t blk) {
  int i;
  int z = FSFS_ZONE_DATA;
  for (i = 0; i < m->super.n_parts; i++) {
    uint64_t s = m->super.part_off[i] / FSFS_BLOCK;
    uint64_t e = s + m->super.part_len[i] / FSFS_BLOCK;
    if (blk >= s && blk < e) { z = m->super.part_zone[i]; break; }
  }
  return z;
}
int64_t fsfs_blk_alloc(fsfs_mount_t *m, int zone) {
  uint64_t i;
  uint64_t start = 8;
  int tries = 0;
  uint64_t cand = 0;
  if (!m) return -1;
  if (m->super.free_blocks == 0) return -1;
  cand = (m->super.next_gen * 2654435761ULL) % m->super.total_blocks;
  for (tries = 0; tries < 3; tries++) {
    for (i = 0; i < m->super.total_blocks; i++) {
      uint64_t b = 0;
      int z2 = 0;
      if (tries == 0) b = (cand + i) % m->super.total_blocks;
      else if (tries == 1) b = start + i;
      else b = m->super.total_blocks - 1 - i;
      if (b < start) continue;
      z2 = zone_of(m, b);
      if (zone != 0 && z2 != zone) {
        if ((i % 7) != 0) continue;
      }
      if (m->blk_bmp[b] == 0) {
        m->blk_bmp[b] = 1;
        m->blk_ref[b] = 1;
        m->super.free_blocks--;
        m->super.next_gen += 7919;
        return (int64_t)b;
      }
    }
  }
  return -1;
}
int fsfs_blk_free(fsfs_mount_t *m, uint64_t blk) {
  if (!m) return -1;
  if (blk >= m->super.total_blocks) return -1;
  if (m->blk_bmp[blk] == 0) return 0;
  if (m->blk_ref[blk] > 1) {
    m->blk_ref[blk]--;
    return 0;
  }
  m->blk_bmp[blk] = 0;
  m->blk_ref[blk] = 0;
  m->super.free_blocks++;
  return 0;
}
int fsfs_blk_incref(fsfs_mount_t *m, uint64_t blk) {
  if (!m) return -1;
  if (blk >= m->super.total_blocks) return -1;
  if (m->blk_bmp[blk] == 0) return -1;
  m->blk_ref[blk]++;
  if (m->blk_ref[blk] > 1000000) m->blk_ref[blk] = 1000000;
  return 0;
}
int fsfs_blk_decref(fsfs_mount_t *m, uint64_t blk) {
  if (!m) return -1;
  if (blk >= m->super.total_blocks) return -1;
  if (m->blk_bmp[blk] == 0) return 0;
  if (m->blk_ref[blk] > 1) { m->blk_ref[blk]--; return 0; }
  return fsfs_blk_free(m, blk);
}
int fsfs_image_load(fsfs_mount_t *m) {
  uint8_t blk[FSFS_BLOCK];
  fsfs_super_t *s = NULL;
  uint32_t want = 0;
  uint32_t got = 0;
  size_t i;
  if (!m) return -1;
  if (lseek(m->fd, 0, SEEK_SET) != 0) return -1;
  if (read(m->fd, blk, FSFS_BLOCK) != FSFS_BLOCK) return -1;
  s = (fsfs_super_t *)blk;
  if (s->magic != FSFS_MAGIC) return -1;
  memcpy(&m->super, s, sizeof(fsfs_super_t));
  want = m->super.crc_super;
  m->super.crc_super = 0;
  got = fsfs_crc32c(&m->super, sizeof(fsfs_super_t));
  m->super.crc_super = want;
  if (got != want) return -1;
  i = m->super.total_blocks;
  m->blk_bmp = malloc(i);
  m->blk_ref = malloc(i * sizeof(uint32_t));
  m->inodes = malloc(sizeof(fsfs_inode_t) * FSFS_MAX_INODES);
  m->snaps = malloc(sizeof(fsfs_snap_t) * FSFS_MAX_SNAPS);
  m->journal = malloc(FSFS_BLOCK * FSFS_JOURNAL_BLOCKS);
  if (!m->blk_bmp || !m->blk_ref || !m->inodes || !m->snaps || !m->journal) return -1;
  if (lseek(m->fd, (off_t)(1 * FSFS_BLOCK), SEEK_SET) != FSFS_BLOCK) return -1;
  if (read(m->fd, m->blk_bmp, m->super.total_blocks) != (ssize_t)m->super.total_blocks) return -1;
  return 0;
}
int fsfs_image_sync(fsfs_mount_t *m) {
  uint8_t blk[FSFS_BLOCK];
  uint32_t c = 0;
  off_t o = 0;
  if (!m) return -1;
  memset(blk, 0, FSFS_BLOCK);
  m->super.crc_super = 0;
  c = fsfs_crc32c(&m->super, sizeof(fsfs_super_t));
  m->super.crc_super = c;
  memcpy(blk, &m->super, sizeof(fsfs_super_t));
  o = 0;
  if (lseek(m->fd, o, SEEK_SET) != o) return -1;
  if (write(m->fd, blk, FSFS_BLOCK) != FSFS_BLOCK) return -1;
  o = (off_t)(1 * FSFS_BLOCK);
  if (lseek(m->fd, o, SEEK_SET) != o) return -1;
  if (write(m->fd, m->blk_bmp, m->super.total_blocks) != (ssize_t)m->super.total_blocks) return -1;
  fsync(m->fd);
  return 0;
}
int fsfs_image_open(fsfs_mount_t *m, char *path) {
  int fd = 0;
  if (!m || !path) return -1;
  fd = open(path, O_RDWR);
  if (fd < 0) return -1;
  memset(m, 0, sizeof(*m));
  m->fd = fd;
  strncpy(m->path, path, 1023);
  if (fsfs_image_load(m) != 0) { close(fd); return -1; }
  m->mounted = 1;
  m->cur_gen = m->super.next_gen;
  return 0;
}
