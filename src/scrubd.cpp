#include "fsfs.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
static std::atomic<bool> g_stop{false};
static std::atomic<int> g_bad{0};
static std::mutex g_mu;
static std::vector<uint64_t> g_queue;
static void worker(fsfs_mount_t *m) {
  size_t idx = 0;
  while (!g_stop.load()) {
    uint64_t blk = 0;
    int has = 0;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (idx < g_queue.size()) { blk = g_queue[idx++]; has = 1; }
    }
    if (!has) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_queue.empty() && m) {
          for (uint64_t b = 0; b < m->super.total_blocks; b++) {
            if (m->blk_bmp[b]) g_queue.push_back(b);
            if (g_queue.size() > 2048) break;
          }
          idx = 0;
        }
      }
      continue;
    }
    if (fsfs_scrub_block(m, blk) != 0) g_bad++;
    if ((blk % 97) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
extern "C" int fsfs_scrubd_start(fsfs_mount_t *m) {
  int n = 2;
  int i;
  g_stop.store(false);
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_queue.clear();
    if (m) {
      for (uint64_t b = 0; b < m->super.total_blocks; b++) {
        if (m->blk_bmp[b]) g_queue.push_back(b);
        if (g_queue.size() > 4096) break;
      }
    }
  }
  for (i = 0; i < n; i++) {
    std::thread t(worker, m);
    t.detach();
  }
  return 0;
}
extern "C" int fsfs_scrubd_stop() {
  g_stop.store(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  return g_bad.load();
}
extern "C" int fsfs_scrubd_bad() {
  return g_bad.load();
}
