#include <omp.h>
#include "tree_api.hpp"
#include "tree.h"
#include <thread>
#include <sys/types.h>

// #define DEBUG_MSG

class tree_wrapper : public tree_api
{
public:
  tree_wrapper();
  virtual ~tree_wrapper();

  virtual bool find(const char *key, size_t key_sz, char *value_out) override;
  virtual bool insert(const char *key, size_t key_sz, const char *value, size_t value_sz) override;
  virtual bool update(const char *key, size_t key_sz, const char *value, size_t value_sz) override;
  virtual bool remove(const char *key, size_t key_sz) override;
  virtual int scan(const char *key, size_t key_sz, int scan_sz, char *&values_out) override;

private:
  tree t_;
};

tree_wrapper::tree_wrapper()
{
}

tree_wrapper::~tree_wrapper()
{
}

bool tree_wrapper::find(const char *key, size_t key_sz, char *value_out)
{
  void* val;
  bool found = t_.lookup(*reinterpret_cast<key_type*>(const_cast<char*>(key)), val);
  if (found)
    std::memcpy(value_out, &val, 8);
  return found;
}

bool tree_wrapper::insert(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
  return t_.insert(*reinterpret_cast<key_type*>(const_cast<char*>(key)), 
    reinterpret_cast<val_type>(const_cast<char*>(value)));
}

bool tree_wrapper::update(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
//   thread_local ThreadHelper T{UPDATE};
//   bnode *p;
//   bleaf *lp;
//   int i, t, m, b, jj;
//   auto k = PBkeyToLB(key);
//   unsigned char key_hash = hashcode1B(k);
//   bool found = false;
  
//   Again4:
//     if (_xbegin() != _XBEGIN_STARTED)
//       goto Again4;
//     p = ((lbtree*)lbt)->tree_meta->tree_root;
//     for (i = ((lbtree*)lbt)->tree_meta->root_level; i > 0; i--)
//     {
//     #ifdef PREFETCH
//         NODE_PREF(p);
//     #endif
//         if (p->lock())
//         {
//             _xabort(1);
//             goto Again4;
//         }
//         // binary search to narrow down to at most 8 entries
//         b = 1;
//         t = p->num();
//         while (b + 7 <= t)
//         {
//             m = (b + t) >> 1;
//             if (k > p->k(m))
//                 b = m + 1;
//             else if (k < p->k(m))
//                 t = m - 1;
//             else
//             {
//                 p = p->ch(m);
//                 goto inner_done;
//             }
//         }
//         // sequential search (which is slightly faster now)
//         for (; b <= t; b++)
//             if (k < p->k(b))
//                 break;
//         p = p->ch(b - 1);

//     inner_done:;
//     }

//     lp = (bleaf *)p;
// #ifdef PREFETCH
//     LEAF_PREF(lp);
// #endif
//     if (lp->lock)
//     {
//         _xabort(2);
//         goto Again4;
//     }

//     __m128i key_16B = _mm_set1_epi8((char)key_hash);
//     __m128i fgpt_16B = _mm_load_si128((const __m128i *)lp);
//     __m128i cmp_res = _mm_cmpeq_epi8(key_16B, fgpt_16B);
//     unsigned int mask = (unsigned int)
//         _mm_movemask_epi8(cmp_res); // 1: same; 0: diff
//     mask = (mask >> 2) & ((unsigned int)(lp->bitmap));

//     while (mask)
//     {
//         jj = bitScan(mask) - 1; // next candidate
//         if (lp->k(jj) == k)
//         { // found
//             lp->lock = 1;
//             found = true;
//             break;
//         }
//         mask &= ~(0x1 << jj); // remove this bit
//     } // end while
//     _xend();
//     if (found) {
//       lp->ch(jj) = PBvalToLB(value);
//       #ifdef NVMPOOL_REAL
//         clwb(&(lp->ch(jj)));
//         sfence();
//       #endif
//       ((bleafMeta *)lp)->v.lock = 0;
//       // lp->lock = 0;
//     }
  // return found;
  return false;
}

bool tree_wrapper::remove(const char *key, size_t key_sz)
{
  // thread_local ThreadHelper t{REMOVE};
  // //FIXME
  // lbt->del(PBkeyToLB(key));
  // // Check whether the record is indeed removed.
  // // void *p;
  // // int pos;
  // // p = lbt->lookup(*reinterpret_cast<key_type*>(const_cast<char*>(key)), &pos);
  // // if (pos >= 0) {
  // //   return false;
  // // }
  return false;
}

int tree_wrapper::scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
{
//   constexpr size_t ONE_MB = 1ULL << 20;
//   static thread_local char results[ONE_MB];
//   // //FIXME
//   values_out = results;
//   int scanned = lbt->rangeScan(PBkeyToLB(key), scan_sz, results); // range_scan_by_size
// #ifdef DEBUG_MSG
//   if (scanned != 100)
//     printf("%d records scanned\n", scanned);
// #endif
//   return scanned;
  return 0;
}
