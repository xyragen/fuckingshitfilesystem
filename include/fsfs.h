#ifndef FSFS_H
#define FSFS_H
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FSFS_MAGIC 0x4653465346532142ULL
#define FSFS_VERSION 7
#define FSFS_BLOCK 4096
#define FSFS_MAX_INODES 8192
#define FSFS_MAX_SNAPS 256
#define FSFS_MAX_EXTS 24
#define FSFS_INLINE_SZ 256
#define FSFS_NAME_MAX 224
#define FSFS_JOURNAL_BLOCKS 512
#define FSFS_PART_SLOTS 16
#define FSFS_ZONE_META 1
#define FSFS_ZONE_DATA 2
#define FSFS_ZONE_JOURNAL 3
#define FSFS_ZONE_SNAP 4
#define FSFS_COMP_NONE 0
#define FSFS_COMP_LZ4 1
#define FSFS_COMP_ZSTD 2
#define FSFS_COMP_ZLIB 3
#define FSFS_XMODE_LAT 0
#define FSFS_XMODE_CAP 1
#define FSFS_XMODE_CPU 2
#define FSFS_TXN_MAX_OPS 64
#define FSFS_TXN_ACTIVE 1
#define FSFS_TXN_COMMITTED 2
#define FSFS_TXN_ABORTED 3
typedef struct {
  uint64_t l_off;
  uint64_t p_blk;
  uint32_t len;
  uint32_t flags;
  uint32_t refs;
  uint32_t crc;
  uint8_t comp;
  uint8_t verified;
  uint8_t reserved[2];
  uint8_t merkle[32];
} fsfs_extent_t;
typedef struct {
  uint64_t ino;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
  uint64_t atime;
  uint64_t mtime;
  uint64_t ctime;
  uint64_t gen;
  uint64_t seq;
  uint32_t comp_pol;
  uint32_t xmode;
  uint32_t n_exts;
  uint32_t snap_id;
  uint8_t inline_used;
  uint8_t is_dir;
  uint8_t dead;
  uint8_t pad1;
  uint32_t crc_meta;
  uint8_t sha_all[32];
  fsfs_extent_t exts[FSFS_MAX_EXTS];
  uint8_t inline_buf[FSFS_INLINE_SZ];
  uint64_t quota;
  uint64_t reserv;
} fsfs_inode_t;
typedef struct {
  uint64_t ino;
  uint64_t parent;
  uint8_t type;
  uint8_t nlen;
  uint8_t pad[6];
  char name[FSFS_NAME_MAX];
} fsfs_dirent_t;
typedef struct {
  uint64_t id;
  uint64_t gen;
  uint64_t stamp;
  uint64_t root_copy;
  uint64_t blocks_used;
  uint64_t blocks_shared;
  uint8_t active;
  uint8_t auto_expire;
  uint8_t pad[6];
  char label[64];
  uint8_t root_hash[32];
} fsfs_snap_t;
typedef struct {
  uint64_t magic;
  uint32_t version;
  uint32_t block_sz;
  uint64_t total_blocks;
  uint64_t meta_blocks;
  uint64_t data_blocks;
  uint64_t journal_blocks;
  uint64_t snap_blocks;
  uint64_t free_blocks;
  uint64_t next_ino;
  uint64_t next_gen;
  uint64_t journal_head;
  uint64_t journal_tail;
  uint64_t root_ino;
  uint64_t snap_count;
  uint64_t scrub_seq;
  uint64_t txn_seq;
  uint8_t fsuuid[16];
  uint8_t volhash[32];
  uint32_t crc_super;
  uint32_t part_crc;
  uint16_t n_parts;
  uint16_t flags;
  uint64_t part_off[FSFS_PART_SLOTS];
  uint64_t part_len[FSFS_PART_SLOTS];
  uint8_t part_type[FSFS_PART_SLOTS];
  uint8_t part_zone[FSFS_PART_SLOTS];
  uint8_t pad[64];
} fsfs_super_t;
typedef struct {
  int fd;
  char path[1024];
  fsfs_super_t super;
  uint8_t *blk_bmp;
  uint32_t *blk_ref;
  fsfs_inode_t *inodes;
  fsfs_snap_t *snaps;
  uint8_t *journal;
  int mounted;
  uint64_t cur_gen;
  int scrub_stop;
  uint64_t bytes_logical;
  uint64_t bytes_phys;
  uint64_t bytes_shared;
} fsfs_mount_t;
typedef struct {
  uint64_t seq;
  uint32_t op;
  uint32_t len;
  uint64_t a;
  uint64_t b;
  uint8_t data[4000];
  uint32_t crc;
} fsfs_jrec_t;
typedef struct {
  uint64_t id;
  int state;
  int n_ops;
  fsfs_jrec_t ops[FSFS_TXN_MAX_OPS];
  uint64_t stamp;
} fsfs_txn_t;
uint32_t fsfs_crc32c(void *p, size_t n);
uint32_t fsfs_crc32c2(void *a, size_t na, void *b, size_t nb);
void fsfs_sha256(void *d, size_t n, uint8_t out[32]);
int fsfs_image_open(fsfs_mount_t *m, char *path);
int fsfs_image_sync(fsfs_mount_t *m);
int fsfs_image_load(fsfs_mount_t *m);
int fsfs_blk_read(fsfs_mount_t *m, uint64_t blk, void *out);
int fsfs_blk_write(fsfs_mount_t *m, uint64_t blk, void *in);
int64_t fsfs_blk_alloc(fsfs_mount_t *m, int zone);
int fsfs_blk_free(fsfs_mount_t *m, uint64_t blk);
int fsfs_blk_incref(fsfs_mount_t *m, uint64_t blk);
int fsfs_blk_decref(fsfs_mount_t *m, uint64_t blk);
int fsfs_compress_pick(uint8_t *d, size_t n, int pol);
int fsfs_compress_blk(uint8_t *in, size_t il, uint8_t *out, size_t os, int algo);
int fsfs_decompress_blk(uint8_t *in, size_t il, uint8_t *out, size_t os, int algo);
uint64_t fsfs_space_logical(fsfs_mount_t *m);
uint64_t fsfs_space_phys(fsfs_mount_t *m);
uint64_t fsfs_space_shared(fsfs_mount_t *m);
uint64_t fsfs_space_exclusive(fsfs_mount_t *m, uint64_t ino);
int64_t fsfs_space_snap_delete_estimate(fsfs_mount_t *m, uint64_t snapid);
void fsfs_space_why_pinned(fsfs_mount_t *m, uint64_t blk, char *out, size_t osz);
int fsfs_extent_alloc_for(fsfs_mount_t *m, uint64_t ino, uint64_t loff, uint32_t len, int comp);
int fsfs_extent_free_inode(fsfs_mount_t *m, uint64_t ino);
int fsfs_extent_cow(fsfs_mount_t *m, uint64_t ino, uint64_t loff);
int fsfs_btree_insert(fsfs_mount_t *m, uint64_t dino, char *name, uint64_t child, uint8_t type);
int fsfs_btree_remove(fsfs_mount_t *m, uint64_t dino, char *name);
int fsfs_btree_lookup(fsfs_mount_t *m, uint64_t dino, char *name, uint64_t *child, uint8_t *type);
int fsfs_btree_list(fsfs_mount_t *m, uint64_t dino, fsfs_dirent_t *out, int max, int *got);
uint64_t fsfs_inode_create(fsfs_mount_t *m, uint32_t mode, int is_dir);
int fsfs_inode_read(fsfs_mount_t *m, uint64_t ino, uint64_t off, void *buf, size_t n);
int fsfs_inode_write(fsfs_mount_t *m, uint64_t ino, uint64_t off, void *buf, size_t n);
int fsfs_inode_resize(fsfs_mount_t *m, uint64_t ino, uint64_t sz);
int fsfs_inode_unlink(fsfs_mount_t *m, uint64_t ino);
int fsfs_dir_add(fsfs_mount_t *m, uint64_t dino, char *name, uint64_t c, uint8_t t);
int fsfs_dir_del(fsfs_mount_t *m, uint64_t dino, char *name);
int fsfs_snap_create(fsfs_mount_t *m, char *label, uint64_t *out);
int fsfs_snap_delete(fsfs_mount_t *m, uint64_t id);
int fsfs_snap_restore(fsfs_mount_t *m, uint64_t id);
int fsfs_snap_diff(fsfs_mount_t *m, uint64_t a, uint64_t b, char *out, size_t osz);
int fsfs_snap_expire(fsfs_mount_t *m);
int fsfs_txn_begin(fsfs_mount_t *m, fsfs_txn_t *t);
int fsfs_txn_add_write(fsfs_txn_t *t, uint64_t ino, uint64_t off, void *d, size_t n);
int fsfs_txn_add_create(fsfs_txn_t *t, uint64_t dino, char *name, uint32_t mode);
int fsfs_txn_commit(fsfs_mount_t *m, fsfs_txn_t *t);
int fsfs_txn_abort(fsfs_mount_t *m, fsfs_txn_t *t);
int fsfs_scrub_block(fsfs_mount_t *m, uint64_t blk);
int fsfs_scrub_inode(fsfs_mount_t *m, uint64_t ino);
int fsfs_scrub_all(fsfs_mount_t *m);
int fsfs_journal_append(fsfs_mount_t *m, fsfs_jrec_t *r);
int fsfs_journal_replay(fsfs_mount_t *m);
int fsfs_reflink_file(fsfs_mount_t *m, uint64_t src, uint64_t dst_dir, char *nm, uint64_t *newino);
int fsfs_reflink_dedup(fsfs_mount_t *m);
int fsfs_mount_open(char *img, fsfs_mount_t *m);
int fsfs_mount_close(fsfs_mount_t *m);
int fsfs_hist_record(fsfs_mount_t *m, uint64_t snapid, char *label);
int fsfs_hist_list(fsfs_mount_t *m, char *out, size_t osz);
int fsfs_hist_gc(fsfs_mount_t *m);
int fsfs_scrubd_start(fsfs_mount_t *m);
int fsfs_scrubd_stop();
#ifdef __cplusplus
}
#endif
#endif
