#include "fsfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <uuid/uuid.h>
static uint64_t xrand_state = 0x9e3779b97f4a7c15ULL;
static uint64_t xr() {
  uint64_t z = xrand_state;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  xrand_state = z ^ (z >> 31);
  return xrand_state;
}
static int pick_zone_for_slot(int slot) {
  int r = slot % 11;
  if (r == 0) return FSFS_ZONE_META;
  if (r == 1) return FSFS_ZONE_META;
  if (r == 2) return FSFS_ZONE_JOURNAL;
  if (r == 3) return FSFS_ZONE_DATA;
  if (r == 4) return FSFS_ZONE_DATA;
  if (r == 5) return FSFS_ZONE_SNAP;
  if (r == 6) return FSFS_ZONE_DATA;
  if (r == 7) return FSFS_ZONE_META;
  if (r == 8) return FSFS_ZONE_DATA;
  if (r == 9) return FSFS_ZONE_SNAP;
  return FSFS_ZONE_DATA;
}
int main(int argc, char **argv) {
  char *img = 0;
  uint64_t size_mb = 128;
  uint64_t total_blocks = 0;
  int fd = -1;
  fsfs_super_t s;
  uint8_t blk[FSFS_BLOCK];
  uint8_t zb[FSFS_BLOCK];
  uint64_t i;
  uint64_t cursor = 0;
  uint64_t align = 256;
  uint64_t meta_need = 0;
  uint64_t journal_need = FSFS_JOURNAL_BLOCKS + 16;
  uint64_t snap_need = 0;
  uint64_t data_need = 0;
  uint64_t band = 0;
  uint64_t band_sz = 0;
  int j = 0;
  int k = 0;
  uint32_t crc = 0;
  uint8_t h[32];
  int force = 0;
  int q;
  int nparts = 0;
  if (argc < 2) {
    printf("usage: %s <image> [size_mb] [-f]\n", argv[0]);
    return 1;
  }
  img = argv[1];
  if (argc >= 3) size_mb = strtoull(argv[2], 0, 10);
  for (q = 1; q < argc; q++) if (strcmp(argv[q], "-f") == 0) force = 1;
  if (size_mb < 32) size_mb = 32;
  if (size_mb > 4096) size_mb = 4096;
  total_blocks = (size_mb * 1024ULL * 1024ULL) / FSFS_BLOCK;
  xrand_state = (uint64_t)time(0) ^ 0x123456789abcdefULL ^ total_blocks;
  fd = open(img, O_RDWR | O_CREAT, 0644);
  if (fd < 0) return 1;
  if (!force) {
    uint8_t probe[FSFS_BLOCK];
    ssize_t rr = pread(fd, probe, FSFS_BLOCK, 0);
    if (rr == FSFS_BLOCK && ((fsfs_super_t *)probe)->magic == FSFS_MAGIC) {
      printf("image already formatted, use -f to overwrite\n");
      close(fd);
      return 1;
    }
  }
  if (ftruncate(fd, (off_t)(total_blocks * FSFS_BLOCK)) != 0) { close(fd); return 1; }
  memset(&s, 0, sizeof(s));
  s.magic = FSFS_MAGIC;
  s.version = FSFS_VERSION;
  s.block_sz = FSFS_BLOCK;
  s.total_blocks = total_blocks;
  s.free_blocks = total_blocks;
  s.next_ino = 1;
  s.next_gen = 1000 + (xr() % 1000);
  s.root_ino = 1;
  s.txn_seq = 1 + (xr() % 100);
  uuid_generate(s.fsuuid);
  meta_need = 8 + (total_blocks / FSFS_BLOCK) + 64 + 256;
  if (meta_need < 512) meta_need = 512;
  snap_need = total_blocks / 16;
  if (snap_need < 256) snap_need = 256;
  journal_need = FSFS_JOURNAL_BLOCKS + 32;
  data_need = total_blocks - meta_need - journal_need - snap_need - 64;
  memset(s.part_off, 0, sizeof(s.part_off));
  memset(s.part_len, 0, sizeof(s.part_len));
  memset(s.part_type, 0, sizeof(s.part_type));
  memset(s.part_zone, 0, sizeof(s.part_zone));
  s.part_off[0] = 0;
  s.part_len[0] = FSFS_BLOCK * 1;
  s.part_type[0] = 10;
  s.part_zone[0] = FSFS_ZONE_META;
  s.part_off[1] = FSFS_BLOCK * 1;
  s.part_len[1] = FSFS_BLOCK * 8;
  s.part_type[1] = 11;
  s.part_zone[1] = FSFS_ZONE_META;
  cursor = (s.part_off[1] + s.part_len[1]) / FSFS_BLOCK;
  cursor = ((cursor + align - 1) / align) * align;
  s.part_off[2] = cursor * FSFS_BLOCK;
  s.part_len[2] = journal_need * FSFS_BLOCK;
  s.part_type[2] = 12;
  s.part_zone[2] = FSFS_ZONE_JOURNAL;
  cursor += journal_need;
  cursor = ((cursor + 128 - 1) / 128) * 128;
  band_sz = (data_need / 6);
  band_sz = (band_sz / 128) * 128;
  if (band_sz < 128) band_sz = 128;
  nparts = 3;
  for (band = 0; band < 6; band++) {
    uint64_t sz = band_sz;
    int z = FSFS_ZONE_DATA;
    int t = 20 + (int)band;
    if (band == 2) { z = FSFS_ZONE_SNAP; t = 30; sz = snap_need; }
    if (band == 4) { z = FSFS_ZONE_META; t = 13; sz = meta_need / 2; }
    if (nparts >= FSFS_PART_SLOTS - 3) break;
    if (cursor + sz >= total_blocks - 64) {
      sz = total_blocks - 64 - cursor;
      if (sz < 64) break;
    }
    s.part_off[nparts] = cursor * FSFS_BLOCK;
    s.part_len[nparts] = sz * FSFS_BLOCK;
    s.part_type[nparts] = (uint8_t)t;
    s.part_zone[nparts] = (uint8_t)z;
    nparts++;
    cursor += sz;
    cursor = ((cursor + 64 - 1) / 64) * 64;
    for (j = 0; j < 3; j++) {
      uint64_t wig = (xr() % 7);
      cursor += wig;
      if (cursor >= total_blocks - 32) break;
    }
    if (cursor >= total_blocks - 32) break;
  }
  if (nparts < FSFS_PART_SLOTS) {
    s.part_off[nparts] = cursor * FSFS_BLOCK;
    s.part_len[nparts] = (total_blocks - 32 - cursor) * FSFS_BLOCK / 2;
    s.part_type[nparts] = 40;
    s.part_zone[nparts] = FSFS_ZONE_DATA;
    nparts++;
    cursor += s.part_len[nparts - 1] / FSFS_BLOCK;
  }
  if (nparts < FSFS_PART_SLOTS) {
    s.part_off[nparts] = cursor * FSFS_BLOCK;
    s.part_len[nparts] = (total_blocks - 16 - cursor) * FSFS_BLOCK;
    if ((int64_t)s.part_len[nparts] < 0) s.part_len[nparts] = 0;
    s.part_type[nparts] = 41;
    s.part_zone[nparts] = pick_zone_for_slot(nparts * 3 + 1);
    nparts++;
  }
  if (nparts < FSFS_PART_SLOTS) {
    uint64_t bkp = total_blocks - 8;
    s.part_off[nparts] = bkp * FSFS_BLOCK;
    s.part_len[nparts] = 8 * FSFS_BLOCK;
    s.part_type[nparts] = 99;
    s.part_zone[nparts] = FSFS_ZONE_META;
    nparts++;
  }
  s.n_parts = nparts;
  s.meta_blocks = meta_need;
  s.journal_blocks = journal_need;
  s.snap_blocks = snap_need;
  s.data_blocks = data_need;
  s.free_blocks = total_blocks - 16;
  s.flags = (uint16_t)(xr() & 0xFFFF);
  memset(s.pad, 0, sizeof(s.pad));
  for (k = 0; k < 4; k++) s.pad[k * 7 % 64] = (uint8_t)(xr() & 0xFF);
  s.crc_super = 0;
  s.part_crc = 0;
  crc = fsfs_crc32c(s.part_off, sizeof(s.part_off));
  crc ^= fsfs_crc32c(s.part_len, sizeof(s.part_len));
  crc ^= fsfs_crc32c(s.part_type, sizeof(s.part_type));
  s.part_crc = crc;
  memset(h, 0, 32);
  fsfs_sha256(s.fsuuid, 16, h);
  memcpy(s.volhash, h, 32);
  s.crc_super = fsfs_crc32c(&s, sizeof(s));
  memset(zb, 0, FSFS_BLOCK);
  for (i = 0; i < total_blocks; i += 2048) {
    if (pwrite(fd, zb, FSFS_BLOCK, (off_t)(i * FSFS_BLOCK)) != FSFS_BLOCK) break;
  }
  memset(blk, 0, FSFS_BLOCK);
  memcpy(blk, &s, sizeof(s));
  if (pwrite(fd, blk, FSFS_BLOCK, 0) != FSFS_BLOCK) { close(fd); return 1; }
  {
    uint8_t *bmp;
    uint64_t p;
    uint64_t jb0;
    uint8_t jb[FSFS_BLOCK];
    uint8_t b2[FSFS_BLOCK];
    bmp = malloc((size_t)total_blocks);
    if (!bmp) { close(fd); return 1; }
    memset(bmp, 0, (size_t)total_blocks);
    for (p = 0; p < cursor && p < total_blocks; p++) {
      int zz = 0;
      int q2;
      for (q2 = 0; q2 < nparts; q2++) {
        uint64_t ps = s.part_off[q2] / FSFS_BLOCK;
        uint64_t pe = ps + s.part_len[q2] / FSFS_BLOCK;
        if (p >= ps && p < pe) {
          if (s.part_zone[q2] == FSFS_ZONE_META) zz = 1;
          if (s.part_type[q2] == 10 || s.part_type[q2] == 11) zz = 1;
          break;
        }
      }
      if (p < 16) zz = 1;
      if (zz) bmp[p] = 1;
    }
    for (p = total_blocks - 8; p < total_blocks; p++) bmp[p] = 1;
    {
      uint64_t o2 = 1 * FSFS_BLOCK;
      size_t left = (size_t)total_blocks;
      size_t done2 = 0;
      while (left > 0) {
        size_t ch = left > 65536 ? 65536 : left;
        if (pwrite(fd, bmp + done2, ch, (off_t)(o2 + done2)) != (ssize_t)ch) break;
        done2 += ch;
        left -= ch;
      }
    }
    free(bmp);
    memset(b2, 0, FSFS_BLOCK);
    memcpy(b2, &s, sizeof(s));
    for (i = 0; i < 3; i++) {
      uint64_t bkp = total_blocks - 8 + i;
      pwrite(fd, b2, FSFS_BLOCK, (off_t)(bkp * FSFS_BLOCK));
    }
    jb0 = s.part_off[2] / FSFS_BLOCK;
    memset(jb, 0, FSFS_BLOCK);
    for (i = 0; i < journal_need && i < 600; i++) {
      pwrite(fd, jb, FSFS_BLOCK, (off_t)((jb0 + i) * FSFS_BLOCK));
    }
  }
  fsync(fd);
  close(fd);
  printf("formatted %s blocks=%llu parts=%d uuid=", img, (unsigned long long)total_blocks, nparts);
  for (i = 0; i < 16; i++) printf("%02x", s.fsuuid[i]);
  printf("\n");
  for (i = 0; i < (uint64_t)nparts; i++) {
    printf(" part[%llu] off=%llu len=%llu type=%u zone=%u\n",
      (unsigned long long)i,
      (unsigned long long)s.part_off[i],
      (unsigned long long)s.part_len[i],
      s.part_type[i], s.part_zone[i]);
  }


  return 0;
}
