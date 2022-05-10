#include "iostream"

#define INNER_KEY_NUM 14
#define LEAF_KEY_NUM 15

typedef uint64_t key_type; // >= 8 bytes
typedef void* val_type; // >= 8 bytes

/*--------------------------------------------------------*/

#define INNER_MAX_IDX INNER_KEY_NUM - 1
#define LEAF_MAX_IDX LEAF_KEY_NUM - 1

struct Inner_Entry
{
    key_type key;
    Node* child;
};

struct Leaf_Entry
{
    key_type key;
    val_type val;
};

class Node
{
};

class Inner: public Node
{
public:
    Inner_Entry ent[INNER_KEY_NUM];

    int& lock() { return }
    int count() { return (int)ent[0].key; }

};

class Leaf: public Node
{
public:
    uint64_t version_lock;
    Leaf_Entry ent[LEAF_KEY_NUM];


};

typedef union bleafMeta
{
    unsigned long long word8B[2];
    struct
    {
        uint16_t bitmap : 14;
        uint16_t lock : 1;
        uint16_t alt : 1;
        unsigned char fgpt[LEAF_KEY_NUM]; /* fingerprints */
    } v;
} bleafMeta;

/**
 * bleaf: leaf node
 *
 * We guarantee that each leaf must have >=1 key.
 */
class bleaf
{
public:
    uint16_t bitmap : 14;
    uint16_t lock : 1;
    uint16_t alt : 1;
    unsigned char fgpt[LEAF_KEY_NUM]; /* fingerprints */
    IdxEntry ent[LEAF_KEY_NUM];
    bleaf *next[2];

public:
    key_type &k(int idx) { return ent[idx].k; }
    Pointer8B &ch(int idx) { return ent[idx].ch; }

    int num() { return countBit(bitmap); }
    bleaf *nextSibling() { return next[alt]; }

    bool isFull(void) { return (bitmap == 0x3fff); }

    void setBothWords(bleafMeta *m)
    {
        bleafMeta *my_meta = (bleafMeta *)this;
        my_meta->word8B[1] = m->word8B[1];
        my_meta->word8B[0] = m->word8B[0];
    }

    void setWord0(bleafMeta *m)
    {
        bleafMeta *my_meta = (bleafMeta *)this;
        my_meta->word8B[0] = m->word8B[0];
    }

    void movnt64(uint64_t *dest, uint64_t const src, bool front, bool back) {
        if (front) sfence();
        _mm_stream_si64((long long int *)dest, (long long int) src);
        if (back) sfence();
    }

    void setWord0_temporal(bleafMeta *m){
       bleafMeta * my_meta= (bleafMeta *)this;
       movnt64((uint64_t*)&my_meta->word8B[0], m->word8B[0], false, true);
    }

    void setBothWords_temporal(bleafMeta *m) {
       bleafMeta * my_meta= (bleafMeta *)this;
       my_meta->word8B[1]= m->word8B[1];
       movnt64((uint64_t*)&my_meta->word8B[0], m->word8B[0], false, true);
    }

}; // bleaf

/* ---------------------------------------------------------------------- */

class treeMeta
{
public:
    int root_level; // leaf: level 0, parent of leaf: level 1
    Pointer8B tree_root;
    bleaf **first_leaf; // on NVM

public:
    treeMeta(void *nvm_address, bool recover = false)
    {
        root_level = 0;
        tree_root = NULL;
        first_leaf = (bleaf **)nvm_address;

        if (!recover)
            setFirstLeaf(NULL);
    }

    void setFirstLeaf(bleaf *leaf)
    {
        *first_leaf = leaf;
        clwb(first_leaf);
        sfence();
    }

}; // treeMeta

/* ---------------------------------------------------------------------- */

class lbtree : public tree
{
public: // root and level
    treeMeta *tree_meta;

public:
    lbtree(void *nvm_address, bool recover = false)
    {
        tree_meta = new treeMeta(nvm_address, recover);
        if (!tree_meta)
        {
            perror("new");
            exit(1);
        }
    }

    ~lbtree()
    {
        delete tree_meta;
    }

private:
    int bulkloadSubtree(keyInput *input, int start_key, int num_key,
                        float bfill, int target_level,
                        Pointer8B pfirst[], int n_nodes[]);

    int bulkloadToptree(Pointer8B ptrs[], key_type keys[], int num_key,
                        float bfill, int cur_level, int target_level,
                        Pointer8B pfirst[], int n_nodes[]);

    void getMinMaxKey(bleaf *p, key_type &min_key, key_type &max_key);

    void getKeyPtrLevel(Pointer8B pnode, int pnode_level, key_type left_key,
                        int target_level, Pointer8B ptrs[], key_type keys[], int &num_nodes,
                        bool free_above_level_nodes);

    // sort pos[start] ... pos[end] (inclusively)
    void qsortBleaf(bleaf *p, int start, int end, int pos[]);

public:
    // bulkload a tree and return the root level
    // use multiple threads to do the bulkloading
    int bulkload(int keynum, keyInput *input, float bfill);

    void randomize(Pointer8B pnode, int level);
    void randomize()
    {
        srand48(12345678);
        randomize(tree_meta->tree_root, tree_meta->root_level);
    }

    // given a search key, perform the search operation
    // return the leaf node pointer and the position within leaf node
    void *lookup(key_type key, int *pos);

    void *get_recptr(void *p, int pos)
    {
        return ((bleaf *)p)->ch(pos);
    }

    // insert (key, ptr)
    void insert(key_type key, void *ptr);

    // delete key
    void del(key_type key);

    // // Range scan -- Author: Lu Baotong
    // int range_scan_by_size(const key_type& key,  uint32_t to_scan, char* result);
    // int range_scan_in_one_leaf(bleaf *lp, const key_type& key, uint32_t to_scan, std::pair<key_type, void*>* result);
    // int add_to_sorted_result(std::pair<key_type, void*>* result, std::pair<key_type, void*>* new_record, int total_size, int cur_idx);

    // Range Scan -- Author: George He
    int rangeScan(key_type key,  uint32_t scan_size, char* result);
    bleaf* lockSibling(bleaf* lp);
    
private:
    void print(Pointer8B pnode, int level);
    void check(Pointer8B pnode, int level, key_type &start, key_type &end, bleaf *&ptr);
    void checkFirstLeaf(void);

public:
    void print()
    {
        print(tree_meta->tree_root, tree_meta->root_level);
    }

    void check(key_type *start, key_type *end)
    {
        bleaf *ptr = NULL;
        check(tree_meta->tree_root, tree_meta->root_level, *start, *end, ptr);
        checkFirstLeaf();
    }

    int level() { return tree_meta->root_level; }

}; // lbtree

void initUseful();

#ifdef VAR_KEY
static int vkcmp(char* a, char* b) {
/*
    auto n = key_size_;
    while(n--)
        if( *a != *b )
            return *a - *b;
        else
            a++,b++;
    return 0;
*/
    return memcmp(a, b, key_size_);
}
#endif
/* ---------------------------------------------------------------------- */
#endif /* _LBTREE_H */
