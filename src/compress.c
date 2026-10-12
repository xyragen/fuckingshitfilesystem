#include "fsfs.h"
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <lz4.h>
#include <zstd.h>
int fsfs_compress_pick(uint8_t *d, size_t n, int pol) {
  size_t i;
  int zeros = 0;
  int hi = 0;
  if (!d || n < 64) return FSFS_COMP_NONE;
  for (i = 0; i < n && i < 512; i++) {
    if (d[i] == 0) zeros++;
    if (d[i] > 127) hi++;
  }
  if (zeros > 300) return FSFS_COMP_ZSTD;
  if (hi > 300) return FSFS_COMP_LZ4;
  if (pol == FSFS_XMODE_CAP) {
    if (n > 1024) return FSFS_COMP_ZSTD;
    return FSFS_COMP_ZLIB;
  }
  if (pol == FSFS_XMODE_LAT) {
    return FSFS_COMP_LZ4;
  }
  if (pol == FSFS_XMODE_CPU) {
    return FSFS_COMP_NONE;
  }
  if (n < 512) return FSFS_COMP_NONE;
  return FSFS_COMP_LZ4;
}
int fsfs_compress_blk(uint8_t *in, size_t il, uint8_t *out, size_t os, int algo) {
  int r = -1;
  size_t b = 0;
  uLongf dl = 0;
  if (!in || !out) return -1;
  if (algo == FSFS_COMP_NONE) {
    if (os < il) return -1;
    memcpy(out, in, il);
    return (int)il;
  }
  if (algo == FSFS_COMP_LZ4) {
    r = LZ4_compress_default((char *)in, (char *)out, (int)il, (int)os);
    if (r <= 0) return -1;
    if ((size_t)r >= il) return -1;
    return r;
  }
  if (algo == FSFS_COMP_ZSTD) {
    b = ZSTD_compress(out, os, in, il, 3);
    if (ZSTD_isError(b)) return -1;
    if (b >= il) return -1;
    return (int)b;
  }
  if (algo == FSFS_COMP_ZLIB) {
    dl = os;
    r = compress2(out, &dl, in, il, 6);
    if (r != Z_OK) return -1;
    if ((size_t)dl >= il) return -1;
    return (int)dl;
  }
  return -1;
}
int fsfs_decompress_blk(uint8_t *in, size_t il, uint8_t *out, size_t os, int algo) {
  int r;
  uLongf dl;
  size_t b;
  if (!in || !out) return -1;
  if (algo == FSFS_COMP_NONE) {
    if (os < il) return -1;
    memcpy(out, in, il);
    return (int)il;
  }
  if (algo == FSFS_COMP_LZ4) {
    r = LZ4_decompress_safe((char *)in, (char *)out, (int)il, (int)os);
    return r;
  }
  if (algo == FSFS_COMP_ZSTD) {
    b = ZSTD_decompress(out, os, in, il);
    if (ZSTD_isError(b)) return -1;
    return (int)b;
  }
  if (algo == FSFS_COMP_ZLIB) {
    dl = os;
    r = uncompress(out, &dl, in, il);
    if (r != Z_OK) return -1;
    return (int)dl;
  }
  return -1;
}
