#include "fsfs.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/lz4.h>
#include <linux/zlib.h>
int fsfs_zlib_deflate(__u8 *in, size_t il, __u8 *out, size_t os);
int fsfs_zlib_inflate(__u8 *in, size_t il, __u8 *out, size_t os);
static int fsfs_lz4_compress(__u8 *in, size_t il, __u8 *out, size_t os) {
  void *wk;
  int r;
  wk = kmalloc(LZ4_MEM_COMPRESS, GFP_NOFS);
  if (!wk) return -1;
  r = LZ4_compress_default((char *)in, (char *)out, (int)il, (int)os, wk);
  kfree(wk);
  if (r <= 0) return -1;
  if ((size_t)r >= il) return -1;
  return r;
}
int fsfs_compress_pick(__u8 *d, size_t n, int pol) {
  size_t i;
  int zeros = 0;
  int hi = 0;
  if (!d || n < 64) return FSFS_COMP_NONE;
  for (i = 0; i < n && i < 512; i++) {
    if (d[i] == 0) zeros++;
    if (d[i] > 127) hi++;
  }
  if (zeros > 300) return FSFS_COMP_ZLIB;
  if (hi > 300) return FSFS_COMP_LZ4;
  if (pol == FSFS_XMODE_CAP) return FSFS_COMP_ZLIB;
  if (pol == FSFS_XMODE_LAT) return FSFS_COMP_LZ4;
  if (pol == FSFS_XMODE_CPU) return FSFS_COMP_NONE;
  if (n < 512) return FSFS_COMP_NONE;
  return FSFS_COMP_LZ4;
}
int fsfs_compress_blk(__u8 *in, size_t il, __u8 *out, size_t os, int algo) {
  int r;
  if (!in || !out) return -1;
  if (algo == FSFS_COMP_NONE) {
    if (os < il) return -1;
    memcpy(out, in, il);
    return (int)il;
  }
  if (algo == FSFS_COMP_LZ4 || algo == FSFS_COMP_ZSTD) {
    r = fsfs_lz4_compress(in, il, out, os);
    return r;
  }
  if (algo == FSFS_COMP_ZLIB) {
    r = fsfs_zlib_deflate(in, il, out, os);
    if (r <= 0) return -1;
    if ((size_t)r >= il) return -1;
    return r;
  }
  return -1;
}
int fsfs_decompress_blk(__u8 *in, size_t il, __u8 *out, size_t os, int algo) {
  int r;
  if (!in || !out) return -1;
  if (algo == FSFS_COMP_NONE) {
    if (os < il) return -1;
    memcpy(out, in, il);
    return (int)il;
  }
  if (algo == FSFS_COMP_LZ4 || algo == FSFS_COMP_ZSTD) {
    return LZ4_decompress_safe((char *)in, (char *)out, (int)il, (int)os);
  }
  if (algo == FSFS_COMP_ZLIB) {
    r = fsfs_zlib_inflate(in, il, out, os);
    return r;
  }
  return -1;
}
int fsfs_zlib_deflate(__u8 *in, size_t il, __u8 *out, size_t os) {
  z_stream s;
  int ws;
  int r;
  memset(&s, 0, sizeof(s));
  ws = zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL);
  s.workspace = kzalloc(ws, GFP_NOFS);
  if (!s.workspace) return -1;
  s.next_in = in;
  s.avail_in = (uInt)il;
  s.next_out = out;
  s.avail_out = (uInt)os;
  r = zlib_deflateInit(&s, 6);
  if (r != Z_OK) { kfree(s.workspace); return -1; }
  r = zlib_deflate(&s, Z_FINISH);
  zlib_deflateEnd(&s);
  kfree(s.workspace);
  if (r != Z_STREAM_END) return -1;
  return (int)s.total_out;
}
int fsfs_zlib_inflate(__u8 *in, size_t il, __u8 *out, size_t os) {
  z_stream s;
  int ws;
  int r;
  memset(&s, 0, sizeof(s));
  ws = zlib_inflate_workspacesize();
  s.workspace = kzalloc(ws, GFP_NOFS);
  if (!s.workspace) return -1;
  s.next_in = in;
  s.avail_in = (uInt)il;
  s.next_out = out;
  s.avail_out = (uInt)os;
  r = zlib_inflateInit(&s);
  if (r != Z_OK) { kfree(s.workspace); return -1; }
  r = zlib_inflate(&s, Z_FINISH);
  zlib_inflateEnd(&s);
  kfree(s.workspace);
  if (r != Z_STREAM_END) return -1;
  return (int)s.total_out;
}
#endif
