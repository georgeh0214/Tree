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
#ifdef STRING_KEY
  key_type k(reinterpret_cast<char*>(const_cast<char*>(key)), key_sz);
  bool found = t_.lookup(k, val);
#else
  bool found = t_.lookup(*reinterpret_cast<key_type*>(const_cast<char*>(key)), val);
#endif
  if (found)
    std::memcpy(value_out, &val, 8);
  return found;
}

bool tree_wrapper::insert(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
#ifdef STRING_KEY
  key_type k(reinterpret_cast<char*>(const_cast<char*>(key)), key_sz);
  return t_.insert(k, reinterpret_cast<val_type>(const_cast<char*>(value)));
#else
  return t_.insert(*reinterpret_cast<key_type*>(const_cast<char*>(key)), 
    reinterpret_cast<val_type>(const_cast<char*>(value)));
#endif
}

bool tree_wrapper::update(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
#ifdef STRING_KEY
  key_type k(reinterpret_cast<char*>(const_cast<char*>(key)), key_sz);
  return t_.update(k, reinterpret_cast<val_type>(const_cast<char*>(value)));
#else
  return t_.update(*reinterpret_cast<key_type*>(const_cast<char*>(key)), *reinterpret_cast<val_type*>(const_cast<char*>(value)));
#endif
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
  constexpr size_t ONE_MB = 1ULL << 20;
  static thread_local char results[ONE_MB];
  ScanHelper sh(scan_sz, results);
#ifdef STRING_KEY
  key_type k(reinterpret_cast<char*>(const_cast<char*>(key)), key_sz);
  t_.rangeScan(k, sh);
#else
  t_.rangeScan(*reinterpret_cast<key_type*>(const_cast<char*>(key)), sh);
#endif
  std::sort((LeafEntry*)results, ((LeafEntry*)results) + sh.scanned, leafEntryCompareFunc);
  return sh.scanned;
}
