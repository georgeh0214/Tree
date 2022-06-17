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
  bool found = t_.lookup(key, key_sz, val);

  if (found)
    std::memcpy(value_out, &val, 8);
  return found;
}

bool tree_wrapper::insert(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
  return t_.insert(key, key_sz, value);
}

bool tree_wrapper::update(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
  return t_.update(key, key_sz, value);
}

bool tree_wrapper::remove(const char *key, size_t key_sz)
{
  return false;
}

int tree_wrapper::scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
{
  // constexpr size_t ONE_MB = 1ULL << 20;
  // static thread_local char results[ONE_MB];
  // ScanHelper sh(scan_sz, results);
  // t_.rangeScan(*reinterpret_cast<key_type*>(const_cast<char*>(key)), sh);
  // std::sort((LeafEntry*)results, ((LeafEntry*)results) + sh.scanned, leafEntryCompareFunc);
  // return sh.scanned;
  return 0;
}
