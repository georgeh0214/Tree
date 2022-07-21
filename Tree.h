#include <libpmemobj.h> // PMDK
#include <immintrin.h> // SIMD

#include "bitmap.h"

struct TreeMeta {
    PMEMobjpool * pop; // should be nullptr if running entirely in DRAM
    uint32_t inner_size; // size of inner node
    uint32_t leaf_size; // size of leaf node
    uint16_t inner_key_num; // xor max # of keys in each inner
    uint16_t leaf_key_num; // xor max # of keys in each leaf <= 64
    uint32_t key_len; // fixed key len
    uint32_t value_len; // fixed value len
    bool enable_fp; // whether to store fps in leaf
    bool enable_simd; // whether to use simd to compare fps
    bool binary_search; // linear/binary search inner node

    TreeMeta()
    {
        std::memset(this, 0, sizeof(TreeMeta));
    }
};

union Ptr {
    void* vp; // volatile ptr
    PMEMoid pp; // persistent ptr
};

struct Node;
thread_local static Node* anc_[MAX_HEIGHT];
thread_local static short pos_[MAX_HEIGHT];
thread_local static uint64_t versions_[MAX_HEIGHT];
thread_local static uint8_t key_hash_;

// struct Node;
// struct InnerEntry
// {
//     key_type key;
//     Node* child;
// };

// struct LeafEntry
// {
//     key_type key;
//     val_type val;
// };

// struct ScanHelper // implement 
// {
// public:
//     LeafEntry* start;
//     LeafEntry* cur;
//     int scan_size;
//     int scanned;

//     ScanHelper(int c, char* r) // initializer
//     {
//         scanned = 0;
//         scan_size = c; 
//         start = cur = (LeafEntry*)r; 
//     }

//     void reset() { scanned = 0; cur = start; } // called upon retry

//     bool stop() { return scanned >= scan_size; } // this is called once after scanning each leaf

//     inline void scanEntry(const LeafEntry& ent) // what to do with each entry scanned
//     {
//         *cur = ent;
//         cur ++;
//         scanned ++;
//     }
// };

// inline static bool leafEntryCompareFunc(LeafEntry& a, LeafEntry& b)
// {
// #if defined(PM) && defined(STRING_KEY)
//     return compare(pmemobj_direct(a.key), pmemobj_direct(b.key), a.len, b.len) < 0;
// #elif defined(LONG_KEY)
//     return a.key < b.key;
// #else
//     return a.key < b.key;
// #endif
// }

struct Node
{
public:
    std::atomic<uint64_t> versionLock{0b100};
// general
    bool isLocked(uint64_t& lock) { lock = versionLock.load(); return lock & 0b10; }
    bool checkVersion(uint64_t version) { return version == versionLock.load(); }
    bool lock() {
        uint64_t version = versionLock.load();
        if (version & 0b10)
            return false;
        return versionLock.compare_exchange_strong(version, version | 0b10);
    }
    bool upgradeToWriter(uint64_t version) {
        return versionLock.compare_exchange_strong(version, version | 0b10);
    }
    bool upgradeToWriter(uint64_t version, Node* n) {
        if (versionLock.compare_exchange_strong(version, version | 0b10))
            return true;
        n->unlock();
        return false;
    }
    void unlock() { versionLock.fetch_add(0b10); } // ToDo: is incrementing version necessary on retry?
// leaf only
    bool alt() { return versionLock.load() & 0b1; }
    void unlockFlipAlt(bool alt) { alt? versionLock.fetch_add(0b1) : versionLock.fetch_add(0b11); }
};

// struct Inner : public Node
// {
// public:
//     uint32_t count;
//     InnerEntry ent[INNER_KEY_NUM + 1]; // count stored in first key

//     Inner() 
//     { 
//         count() = 0;
//     }
//     // ~Inner() { for (int i = 0; i <= count(); i++) { delete this->ent[i].child; } } ToDo: cannot delete void*
//     int& count() { return *((int*)(ent)); }

//     bool isFull() { return count() == INNER_KEY_NUM; }

//     int find(key_type key);
//     Node* findChildSetPos(key_type key, short* pos);
//     Node* findChild(key_type key);
//     inline void insertChild(short index, key_type key, Node* child); // insert key, child at index, does not increment count

// } __attribute__((aligned(64)));

// struct Leaf : public Node
// {
// public:
//     Bitmap bitmap; // 8 byte
// #ifdef FINGERPRINT
//     uint8_t fp[LEAF_KEY_NUM];
// #endif

//     LeafEntry ent[LEAF_KEY_NUM];

// #ifdef PM
//     PMEMoid next[2]; // 32 byte
// #else
//     Leaf* next[2]; // 16 byte
// #endif




//     Leaf() 
//     { 
//     #ifdef PM
//         next[0] = next[1] = OID_NULL; 
//     #else
//         next[0] = next[1] = nullptr; 
//     #endif
//     }
//     Leaf(const Leaf& leaf);
//     ~Leaf();

//     int count() { return bitmap.count(); }

//     Leaf* sibling() 
//     { 
//     #ifdef PM
//         return (Leaf*) pmemobj_direct(next[alt()]);
//     #else
//         return next[alt()]; 
//     #endif
//     }

//     void insertEntry(key_type key, val_type val);
//     int findKey(key_type key); // return position of key if found, -1 if not found
// } __attribute__((aligned(256)));


// static void prefetchInner(void* addr)
// {
// #define INNER_LINE_NUM sizeof(Inner) / 64 
//     for (int i = 0; i < INNER_LINE_NUM; i++, addr += 64)
//         __asm__ __volatile__("prefetcht0 %0" \
//                       :               \
//                       : "m"(*((char *)addr)));
// }

// static void prefetchLeaf(void* addr)
// {
// #define LEAF_LINE_NUM sizeof(Leaf) / 64
//     for (int i = 0; i < INNER_LINE_NUM; i++, addr += 64)
//         __asm__ __volatile__("prefetcht0 %0" \
//                       :               \
//                       : "m"(*((char *)addr)));
// }

class Tree
{
public:
    Node* root;
    uint64_t offset; // for bitmap
    uint16_t height; // leaf at level 0
    uint16_t class_id; // to allocate PM blocks by 256B alignment
    
    TreeMeta meta; // 27B
    Ptr first_leaf;

    Tree() {}

    ~Tree()
    {
        printf("Tree height: %d \n", height);
        // delete (Leaf*)root; ToDo: reclaim memory
    }

    // return true and set val if lookup successful, 
    bool lookup(const char* key, char* val);

    // return true if insert successful
    bool insert(const char* key, char* val);

    // return true if delete successful
    bool del(const char* key);

    // return true if entry with target key is found and val is set to new_val
    bool update(const char* key, char* new_val);

    // range scan with customized scan helper
    // void rangeScan(const char* start_key, ScanHelper& sh);

    // Use given meta m to initialize Tree struct, assume Tree is already mapped to an entry in pop.root if running in PM
    void init(TreeMeta& m, bool recovery)
    {
        root = nullptr;
        height = 0;
        offset = 0;
        class_id = 0;
        if (m.pop)
            first_leaf.pp = OID_NULL;
        else
            first_leaf.vp = nullptr;

        assert(!m.inner_size != !m.inner_key_num && "Only one field of inner params should be valid");
        assert(!m.leaf_size != !m.leaf_key_num && "Only one field of leaf params should be valid");
        assert(m.key_len && "Invalid key length");

        printf("Tree struct size: %d \n", sizeof(Tree));

        auto inner_ent_size = m.key_len + sizeof(Node*);
        auto leaf_ent_size = m.key_len + m.value_len;

        // set inner size or key_num
        if (m.inner_size)
            m.inner_key_num = (m.inner_size - sizeof(Node) - sizeof(uint32_t)) / inner_ent_size - 1; // - lock - cnt, reserve 1 entry for extra child ptr
        else
            m.inner_size = (m.inner_key_num + 1) * inner_ent_size + sizeof(Node) + sizeof(uint32_t);
        printf("Inner size: %d \n", m.inner_size);
        printf("Inner key num: %d \n", m.inner_key_num);

        // set leaf size or key_num
        auto ptr_size = m.pop? sizeof(PMEMoid) : sizeof(void*); // size of leaf next ptr
        if (m.leaf_size)
        {
            if (m.enable_fp)
                m.leaf_key_num = (m.leaf_size - sizeof(Node) - sizeof(Bitmap) - ptr_size * 2) / (leaf_ent_size + sizeof(key_hash_));
            else
                m.leaf_key_num = (m.leaf_size - sizeof(Node) - sizeof(Bitmap) - ptr_size * 2) / leaf_ent_size;
        }
        else
        {
            if (m.enable_fp)
                m.leaf_size = m.leaf_key_num * (leaf_ent_size + sizeof(key_hash_)) + sizeof(Node) + sizeof(Bitmap) + ptr_size * 2;
            else
                m.leaf_size = m.leaf_key_num * leaf_ent_size + sizeof(Node) + sizeof(Bitmap) + ptr_size * 2;
        }
        if (m.leaf_key_num > 64)
        {
            printf("Current bitmap implementation supports at most 64 entries in leaf node, the calculated setting is: %d \n", m.leaf_key_num);
            exit(1);
        }
        uint32_t leaf_size_align = m.leaf_size;
        leaf_size_align += m.leaf_size % 256 == 0? 0 : (256 - m.leaf_size % 256);
        printf("Leaf size without alignment: %d     with alignment: %d      differences: %d \n", m.leaf_size, leaf_size_align, leaf_size_align - m.leaf_size);
        printf("Leaf key num: %d \n", m.leaf_key_num);
        m.leaf_size = leaf_size_align;


        meta = m;
        offset = (uint64_t)(-1) >> (64 - m.leaf_key_num); // bitmap offset
        if (m.pop) // PM. Parse existing Tree metas 
        {
            size_t root_size = pmemobj_root_size(m.pop);
            assert(root_size > 0 && "Should expand root before calling init function");
            int tree_cnt = root_size / sizeof(Tree);
            printf("Current pop contains at most %d Trees\n", tree_cnt);

            Tree* pop_root = (Tree*)pmemobj_direct(pmemobj_root(m.pop, root_size)); // will not change root size
            for (int i = 0; i < tree_cnt; i++) // go through existing TreeMeta entries to see if creating alloc class is necessary
            {
                if (pop_root[i].class_id != 0 && pop_root[i].meta.leaf_size == m.leaf_size) // found another existing PM Tree with same leaf size
                {
                    class_id = pop_root[i].class_id;
                    printf("Reuse existing allocation class id: %d \n", class_id);
                    break;
                }
            }
            if (class_id == 0) // did not find any Tree with same leaf size
            {
                pobj_alloc_class_desc arg;
                arg.unit_size = m.leaf_size;
                arg.alignment = 256;
                arg.units_per_block = 4096;
                arg.header_type = POBJ_HEADER_NONE;
                if (pmemobj_ctl_set(m.pop, "heap.alloc_class.new.desc", &arg) != 0)
                {
                    printf("Allocation class initialization failed!\n");
                    exit(1);
                }
                class_id = arg.class_id;
                printf("Created new allocation class id: %d \n", class_id);
            }
            root = new (alloc_leaf(&first_leaf.pp)) Node();
            clwb(root, meta.leaf_size);
            clwb(this, sizeof(Tree));
            sfence();
        }
        else // DRAM
        {
            root = new (alloc_leaf()) Node();
            first_leaf.vp = root;
        }

        printf("Key length is: %d \n", m.key_len);
        printf("Value length is: %d \n", m.value_len);
        printf("pop is %p \n", m.pop);
        if (m.enable_fp)
            printf("Fingerprint enabled\n");
        if (m.enable_simd)
            printf("SIMD enabled \n");
        if (m.binary_search)
            printf("Binary search inner.\n");
        else
            printf("Linear search inner.\n");

        printf("initialization Complete.\n");
    }

private:
    // general
    inline Node* alloc_inner()
    {
        char* inner = new char[meta.inner_size]; 
        assert(inner && "alloc_inner: new");
        return (Node*)inner;
    }

    inline Node* alloc_leaf(PMEMoid* ptr) // PM
    {
        assert(meta.pop && "alloc_leaf: invalid pool");
        thread_local pobj_action act;
        *ptr = pmemobj_xreserve(meta.pop, &act, meta.leaf_size, 0, POBJ_CLASS_ID(class_id));
        assert(pmemobj_direct(*ptr) && "pmemobj_xreserve");
        return (Node*)pmemobj_direct(*ptr);
    }

    inline Node* alloc_leaf() // DRAM
    {
        char* leaf = new char[meta.leaf_size];
        assert(leaf && "alloc_leaf: new");
        return (Node*)leaf;
    }

    inline void clwb(void* addr, uint32_t len)
    {
        if (meta.pop)
        {
        #ifdef NEW_PERSIST
            for (uint32_t i = 0; i < len; i += 64, addr += 64)
                asm volatile("clwb %0"
                       :
                       : "m"(*((char *)addr)));
        #else
            pmemobj_flush(meta.pop, addr, len);
        #endif
        }
    }

    inline void sfence()
    {
        if (meta.pop)
        {
        #ifdef NEW_PERSIST
            asm volatile("sfence");
        #else
            pmemobj_drain(meta.pop);
        #endif
        }
    }

    inline void initOp(const char* key)
    {
        if (meta.enable_fp)
            key_hash_ = getOneByteHash(key, meta.key_len);
    }

    inline int compare(const char* k1, const char* k2);

    // Inner
    inline uint32_t& getInnerCount(Node* inner)
    {
        return *(uint32_t*)((char*)inner + sizeof(Node));
    }

    inline bool isInnerFull(Node* inner)
    {
        return getInnerCount(inner) == meta.inner_key_num;
    }

    inline char* getInnerKey(Node* inner, int idx)
    {
        return ((char*)inner + sizeof(Node) + sizeof(uint32_t) + idx * (meta.key_len + sizeof(Node*)));
    }

    inline Node* getInnerChild(Node* inner, int idx)
    {
        return *(Node**)((char*)inner + sizeof(Node) + sizeof(uint32_t) + meta.key_len + idx * (meta.key_len + sizeof(Node*)));
    }

    inline void insertInnerEntry(Node* inner, char* new_key, Node* new_child, int idx)
    {
        char* insert_pos = getInnerKey(inner, idx);
        std::memcpy(insert_pos, new_key, meta.key_len);
        *(Node**)(insert_pos + meta.key_len) = new_child;
    }

    Node* findInnerChild(Node* inner, const char* key, short* pos);


    // Leaf
    inline Bitmap* getLeafBitmap(Node* leaf)
    {
        return (Bitmap*)((char*)leaf + sizeof(Node));
    }

    inline uint32_t getLeafCount(Node* leaf)
    {
        return getLeafBitmap(leaf)->count();
    }

    inline uint8_t& getLeafFP(Node* leaf, int idx)
    {
        return *(uint8_t*)((char*)leaf + sizeof(Node) + sizeof(Bitmap) + sizeof(uint8_t) * idx);
    }

    inline char* getLeafKey(Node* leaf, int idx)
    {
        if (meta.enable_fp)
            return ((char*)leaf + sizeof(Node) + sizeof(Bitmap) + meta.leaf_key_num * sizeof(uint8_t) + idx * (meta.key_len + meta.value_len));
        else
            return ((char*)leaf + sizeof(Node) + sizeof(Bitmap) + idx * (meta.key_len + meta.value_len));
    }
    // here assume next ptrs are stored at the end of allocated leaf block, so may have gaps between kv entries and next ptrs due to 256B alignment
    inline void* getNextLeafPtrAddr(Node* leaf, int offset) 
    {
        return ((char*)leaf + meta.leaf_size - offset);
    }

    char* searchLeaf(Node* leaf, const char* key); // return value's address if key is found

    void insertLeafEntry(Node* leaf, const char* key, char* val);


    //Tree
    // return a leaf node that may contain key and set its version, the returned node is locked if lock = true
    Node* findLeaf(const char* key, uint64_t& version, bool lock);
    // return nullptr if key is found in reached leaf node, otherwise lock and return the leaf. Will also keep all nodes traversed & versions in stacks
    Node* findLeafAssumeSplit(const char* key);

    bool lockStack(Node* n);


    
    

    
} __attribute__((aligned(64)));

static inline unsigned long long rdtsc(void)
{
   unsigned hi, lo;
   __asm__ __volatile__("rdtsc"
                        : "=a"(lo), "=d"(hi));
   return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

