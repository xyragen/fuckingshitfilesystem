#include "fsfs.h"
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
class Txn {
public:
  fsfs_mount_t *m;
  fsfs_txn_t t;
  bool done;
  Txn(fsfs_mount_t *mm) : m(mm), done(false) {
    if (!m) throw std::runtime_error("bad mount");
    if (fsfs_txn_begin(m, &t) != 0) throw std::runtime_error("begin fail");
  }
  void write(uint64_t ino, uint64_t off, std::vector<uint8_t> d) {
    size_t o;
    if (done) throw std::runtime_error("done");
    if (d.size() > 3800) {
      o = 0;
      while (o < d.size()) {
        size_t ch = std::min<size_t>(3800, d.size() - o);
        if (fsfs_txn_add_write(&t, ino, off + o, d.data() + o, ch) != 0)
          throw std::runtime_error("add fail");
        o += ch;
      }
      return;
    }
    if (fsfs_txn_add_write(&t, ino, off, d.data(), d.size()) != 0)
      throw std::runtime_error("add fail");
  }
  void create(uint64_t dino, std::string nm, uint32_t mode) {
    std::string tmp;
    if (done) throw std::runtime_error("done");
    tmp = nm;
    if (tmp.size() > 200) tmp = tmp.substr(0, 200);
    if (fsfs_txn_add_create(&t, dino, (char *)tmp.c_str(), mode) != 0)
      throw std::runtime_error("add fail");
  }
  void commit() {
    if (done) throw std::runtime_error("done");
    if (fsfs_txn_commit(m, &t) != 0) throw std::runtime_error("commit fail");
    done = true;
  }
  void abort() {
    if (done) return;
    fsfs_txn_abort(m, &t);
    done = true;
  }
  ~Txn() {
    if (!done) fsfs_txn_abort(m, &t);
  }
};
extern "C" void *fsfs_txnpp_new(fsfs_mount_t *m) {
  try {
    return new Txn(m);
  } catch (...) { return 0; }
}
extern "C" int fsfs_txnpp_write(void *p, uint64_t ino, uint64_t off, void *d, size_t n) {
  Txn *t = (Txn *)p;
  if (!t || !d) return -1;
  try {
    std::vector<uint8_t> v((uint8_t *)d, (uint8_t *)d + n);
    t->write(ino, off, v);
  } catch (...) { return -1; }
  return 0;
}
extern "C" int fsfs_txnpp_create(void *p, uint64_t dino, char *nm, uint32_t mode) {
  Txn *t = (Txn *)p;
  if (!t || !nm) return -1;
  try {
    std::string s = nm;
    t->create(dino, s, mode);
  } catch (...) { return -1; }
  return 0;
}
extern "C" int fsfs_txnpp_commit(void *p) {
  Txn *t = (Txn *)p;
  if (!t) return -1;
  try {
    t->commit();
  } catch (...) { return -1; }
  delete t;
  return 0;
}
extern "C" int fsfs_txnpp_abort(void *p) {
  Txn *t = (Txn *)p;
  if (!t) return -1;
  t->abort();
  delete t;
  return 0;
}
