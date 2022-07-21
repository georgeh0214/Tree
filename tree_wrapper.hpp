#include <omp.h>
#include "tree_api.hpp"
#include "Tree.h"
#include <thread>
#include <sys/types.h>

static bool PM = false;

extern size_t pool_size_;
extern const char *pool_path_;
extern TreeMeta meta;

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
    Tree* t_;
};

tree_wrapper::tree_wrapper()
{
    if (PM)
    {
        meta.inner_size = 640;
        meta.leaf_key_num = 64;
        meta.enable_fp = true;
        meta.enable_simd = true;
        struct stat buffer;
        if (stat(pool_path_, &buffer) == 0) // file exists
        {
            printf("Remove pool before creating new index!\n");
            exit(1);
        }
        else
        {
            printf("Creating PMEM pool of size: %llu    in location: %s \n", pool_size_, pool_path_);
            POBJ_LAYOUT_BEGIN(Tree);
            POBJ_LAYOUT_END(Tree);
            meta.pop = pmemobj_create(pool_path_, POBJ_LAYOUT_NAME(Tree), pool_size_, 0666);
            if (!meta.pop)
            {
                printf("pmemobj_create\n");
                exit(1);
            }
            t_ = (Tree*)pmemobj_direct(pmemobj_root(meta.pop, sizeof(Tree))); // expand root before calling init
            assert(t_ && "pmemobj_root");
        }
    }
    else
    {
        meta.inner_size = 640;
        meta.leaf_key_num = 64;
        meta.enable_fp = true;
        meta.enable_simd = true;
        meta.pop = nullptr;
        t_ = new Tree();
        assert(t_ && "new");
    }
    t_->init(meta, false);
}

tree_wrapper::~tree_wrapper()
{
    if (meta.pop)
        pmemobj_close(meta.pop);
}

bool tree_wrapper::find(const char *key, size_t key_sz, char *value_out)
{
    bool found = t_->lookup(key, value_out);
    return found;
}

bool tree_wrapper::insert(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
    return t_->insert(key, value);
}

bool tree_wrapper::update(const char *key, size_t key_sz, const char *value, size_t value_sz)
{
    return t_->update(key, value);
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
//   ScanHelper sh(scan_sz, results);
// #ifdef STRING_KEY
//   key_type k(reinterpret_cast<char*>(const_cast<char*>(key)), key_sz);
//   t_.rangeScan(k, sh);
// #elif defined(LONG_KEY)
//   t_.rangeScan(LongKey(key), sh);
// #else
//   t_.rangeScan(*reinterpret_cast<key_type*>(const_cast<char*>(key)), sh);
// #endif
//   std::sort((LeafEntry*)results, ((LeafEntry*)results) + sh.scanned, leafEntryCompareFunc);
//   return sh.scanned;
  return 0;
}
