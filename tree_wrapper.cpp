#include "tree_wrapper.hpp"
#include <chrono>

size_t pool_size_ = ((size_t)(1024 * 1024 * 32) * 1024); // default 32 GB PM pool
const char *pool_path_;
TreeMeta meta;

extern "C" tree_api *create_tree(const tree_options_t &opt)
{
	auto path_ptr = new std::string(opt.pool_path);
 	if (*path_ptr != "")
    	pool_path_ = (*path_ptr).c_str();
  	else
    	pool_path_ = "./pool"; // default pool location

    if (opt.pool_size != 0)
    	pool_size_ = opt.pool_size;
    meta.key_len = opt.key_size;
    meta.value_len = opt.value_size;
	return new tree_wrapper();
}
