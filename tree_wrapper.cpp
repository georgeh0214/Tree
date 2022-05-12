#include "tree_wrapper.hpp"
#include <chrono>

size_t key_size_ = 0;
size_t pool_size_ = ((size_t)(1024 * 1024 * 32) * 1024);
const char *pool_path_;

extern "C" tree_api *create_tree(const tree_options_t &opt)
{
  return new tree_wrapper();
}
