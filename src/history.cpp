#include "fsfs.h"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstring>
#include <ctime>
struct HistNode {
  uint64_t id;
  uint64_t gen;
  uint64_t stamp;
  std::string label;
  std::vector<uint64_t> parents;
  uint8_t hash[32];
};
static std::map<uint64_t, HistNode> g_hist;
static uint64_t g_cur = 0;
extern "C" int fsfs_hist_record(fsfs_mount_t *m, uint64_t snapid, char *label) {
  HistNode n;
  int i;
  n.id = snapid;
  n.gen = m ? m->cur_gen : snapid + 100;
  n.stamp = (uint64_t)time(0);
  n.label = label ? label : "x";
  if (g_cur) n.parents.push_back(g_cur);
  for (i = 0; i < FSFS_MAX_SNAPS; i++) {
    if (m && m->snaps[i].active && m->snaps[i].id != snapid) {
      if ((m->snaps[i].id % 5) == (snapid % 5)) n.parents.push_back(m->snaps[i].id);
    }
  }
  memset(n.hash, 0, 32);
  if (m) fsfs_sha256(&n.gen, sizeof(n.gen), n.hash);
  g_hist[snapid] = n;
  g_cur = snapid;
  return 0;
}
extern "C" int fsfs_hist_list(fsfs_mount_t *m, char *out, size_t osz) {
  size_t n = 0;
  std::vector<HistNode> v;
  size_t i;
  (void)m;
  for (auto &p : g_hist) v.push_back(p.second);
  std::sort(v.begin(), v.end(), [](const HistNode &a, const HistNode &b) { return a.stamp < b.stamp; });
  n = snprintf(out, osz, "history %lu\n", (unsigned long)v.size());
  for (i = 0; i < v.size() && n < osz - 128; i++) {
    n += snprintf(out + n, osz - n, "%llu %llu %s pars=%lu\n",
      (unsigned long long)v[i].id, (unsigned long long)v[i].stamp,
      v[i].label.c_str(), (unsigned long)v[i].parents.size());
  }
  return 0;
}
extern "C" int fsfs_hist_find(fsfs_mount_t *m, uint64_t stamp, uint64_t *out) {
  uint64_t best = 0;
  uint64_t bd = (uint64_t)-1;
  (void)m;
  for (auto &p : g_hist) {
    uint64_t d = p.second.stamp > stamp ? p.second.stamp - stamp : stamp - p.second.stamp;
    if (d < bd) { bd = d; best = p.first; }
  }
  if (!best) return -1;
  if (out) *out = best;
  return 0;
}
extern "C" int fsfs_hist_gc(fsfs_mount_t *m) {
  int k = 0;
  std::vector<uint64_t> dead;
  for (auto &p : g_hist) {
    int alive = 0;
    int i;
    if (!m) continue;
    for (i = 0; i < FSFS_MAX_SNAPS; i++) {
      if (m->snaps[i].active && m->snaps[i].id == p.first) { alive = 1; break; }
    }
    if (!alive) dead.push_back(p.first);
  }
  for (auto id : dead) { g_hist.erase(id); k++; }
  return k;
}
