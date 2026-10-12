#define FUSE_USE_VERSION 35
#include "fsfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse3/fuse.h>
static fsfs_mount_t g_m;
static int resolve(char *path, uint64_t *out) {
  char tmp[4096];
  char *tok;
  char *sv = 0;
  uint64_t cur = 1;
  if (!path || !out) return -1;
  if (strcmp(path, "/") == 0) { *out = 1; return 0; }
  strncpy(tmp, path, 4095);
  tok = strtok_r(tmp, "/", &sv);
  while (tok) {
    uint64_t ch = 0;
    if (fsfs_btree_lookup(&g_m, cur, tok, &ch, 0) != 0) return -1;
    cur = ch;
    tok = strtok_r(0, "/", &sv);
  }
  *out = cur;
  return 0;
}
static int x_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
  uint64_t ino = 0;
  fsfs_inode_t *x;
  (void)fi;
  memset(st, 0, sizeof(*st));
  if (resolve((char *)path, &ino) != 0) return -ENOENT;
  x = &g_m.inodes[ino];
  st->st_ino = ino;
  st->st_mode = x->mode;
  if (x->is_dir) st->st_mode |= S_IFDIR;
  else st->st_mode |= S_IFREG;
  st->st_nlink = x->nlink;
  st->st_uid = x->uid;
  st->st_gid = x->gid;
  st->st_size = x->size;
  st->st_atime = x->atime;
  st->st_mtime = x->mtime;
  st->st_ctime = x->ctime;
  st->st_blocks = (x->size + 511) / 512;
  return 0;
}
static int x_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi, enum fuse_readdir_flags fl) {
  uint64_t ino = 0;
  fsfs_dirent_t ents[32];
  int got = 0;
  int i;
  (void)off; (void)fi; (void)fl;
  if (resolve((char *)path, &ino) != 0) return -ENOENT;
  filler(buf, ".", 0, 0, (enum fuse_fill_dir_flags)0);
  filler(buf, "..", 0, 0, (enum fuse_fill_dir_flags)0);
  if (fsfs_btree_list(&g_m, ino, ents, 32, &got) != 0) return -ENOENT;
  for (i = 0; i < got; i++) filler(buf, ents[i].name, 0, 0, (enum fuse_fill_dir_flags)0);
  return 0;
}
static int x_open(const char *path, struct fuse_file_info *fi) {
  uint64_t ino = 0;
  if (resolve((char *)path, &ino) != 0) return -ENOENT;
  fi->fh = ino;
  return 0;
}
static int x_read(const char *path, char *buf, size_t sz, off_t off, struct fuse_file_info *fi) {
  uint64_t ino = 0;
  int r;
  if (fi) ino = fi->fh;
  else if (resolve((char *)path, &ino) != 0) return -ENOENT;
  r = fsfs_inode_read(&g_m, ino, off, buf, sz);
  if (r < 0) return -EIO;
  return r;
}
static int x_write(const char *path, const char *buf, size_t sz, off_t off, struct fuse_file_info *fi) {
  uint64_t ino = 0;
  if (fi) ino = fi->fh;
  else if (resolve((char *)path, &ino) != 0) return -ENOENT;
  if (fsfs_inode_write(&g_m, ino, off, (void *)buf, sz) != 0) return -EIO;
  fsfs_image_sync(&g_m);
  return sz;
}
static int x_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  char tmp[4096];
  char *sl;
  char *nm;
  uint64_t dino = 0;
  uint64_t ni = 0;
  strncpy(tmp, path, 4095);
  sl = strrchr(tmp, '/');
  if (!sl) return -ENOENT;
  *sl = 0;
  nm = sl + 1;
  if (strlen(tmp) == 0) dino = 1;
  else if (resolve(tmp, &dino) != 0) return -ENOENT;
  ni = fsfs_inode_create(&g_m, mode, 0);
  if (!ni) return -ENOSPC;
  if (fsfs_dir_add(&g_m, dino, nm, ni, 1) != 0) return -EIO;
  fi->fh = ni;
  fsfs_image_sync(&g_m);
  return 0;
}
static int x_mkdir(const char *path, mode_t mode) {
  char tmp[4096];
  char *sl;
  char *nm;
  uint64_t dino = 0;
  uint64_t ni = 0;
  strncpy(tmp, path, 4095);
  sl = strrchr(tmp, '/');
  if (!sl) return -ENOENT;
  *sl = 0;
  nm = sl + 1;
  if (strlen(tmp) == 0) dino = 1;
  else if (resolve(tmp, &dino) != 0) return -ENOENT;
  ni = fsfs_inode_create(&g_m, mode | S_IFDIR, 1);
  if (!ni) return -ENOSPC;
  if (fsfs_dir_add(&g_m, dino, nm, ni, 2) != 0) return -EIO;
  fsfs_image_sync(&g_m);
  return 0;
}
static int x_unlink(const char *path) {
  char tmp[4096];
  char *sl;
  char *nm;
  uint64_t dino = 0;
  strncpy(tmp, path, 4095);
  sl = strrchr(tmp, '/');
  if (!sl) return -ENOENT;
  *sl = 0;
  nm = sl + 1;
  if (strlen(tmp) == 0) dino = 1;
  else if (resolve(tmp, &dino) != 0) return -ENOENT;
  if (fsfs_dir_del(&g_m, dino, nm) != 0) return -ENOENT;
  fsfs_image_sync(&g_m);
  return 0;
}
static int x_truncate(const char *path, off_t sz, struct fuse_file_info *fi) {
  uint64_t ino = 0;
  (void)fi;
  if (resolve((char *)path, &ino) != 0) return -ENOENT;
  if (fsfs_inode_resize(&g_m, ino, sz) != 0) return -EIO;
  fsfs_image_sync(&g_m);
  return 0;
}
static struct fuse_operations x_ops;
static void init_ops() {
  memset(&x_ops, 0, sizeof(x_ops));
  x_ops.getattr = x_getattr;
  x_ops.readdir = x_readdir;
  x_ops.mkdir = x_mkdir;
  x_ops.unlink = x_unlink;
  x_ops.rmdir = x_unlink;
  x_ops.truncate = x_truncate;
  x_ops.open = x_open;
  x_ops.read = x_read;
  x_ops.write = x_write;
  x_ops.create = x_create;
}
int main(int argc, char **argv) {
  char *img = 0;
  char *mnt = 0;
  int i;
  int fargc = 0;
  char *fargv[64];
  fargv[fargc++] = argv[0];
  for (i = 1; i < argc; i++) {
    if (img == 0 && argv[i][0] != '-') img = argv[i];
    else if (mnt == 0 && argv[i][0] != '-') mnt = argv[i];
    else { fargv[fargc++] = argv[i]; }
  }
  if (!img || !mnt) { printf("usage: %s <device> <mountpoint> [fuse opts]\n", argv[0]); return 1; }
  init_ops();
  memset(&g_m, 0, sizeof(g_m));
  if (fsfs_mount_open(img, &g_m) != 0) {
    printf("mount open failed\n");
    return 1;
  }
  if (g_m.inodes[1].ino == 0) {
    fsfs_inode_t *r;
    g_m.inodes[1].ino = 1;
    r = &g_m.inodes[1];
    r->mode = 0755;
    r->nlink = 2;
    r->is_dir = 1;
    r->size = 0;
    g_m.super.next_ino = 2;
    g_m.super.root_ino = 1;
    fsfs_image_sync(&g_m);
  }
  fargv[fargc++] = mnt;
  fargv[fargc] = 0;
  return fuse_main(fargc, fargv, &x_ops, 0);
}
