#include "bitmap.h"

/*------------------------------------------------------------------------*/

class Inner;
class Leaf;

#ifdef PM
    #include <libpmemobj.h>
    POBJ_LAYOUT_BEGIN(Tree);
    POBJ_LAYOUT_TOID(Tree, Leaf);
    POBJ_LAYOUT_END(Tree);

    extern PMEMobjpool * pop_;
    extern uint64_t class_id;
#endif

#ifdef SIMD
    #include <immintrin.h>
#endif

thread_local static Inner* anc_[MAX_HEIGHT];
thread_local static short pos_[MAX_HEIGHT];
thread_local static uint64_t versions_[MAX_HEIGHT];
thread_local static uint8_t key_hash_;

class Node;
struct InnerEntry
{
    key_type key;
    Node* child;
};

struct LeafEntry
{
    key_type key;
    val_type val;
};

class ScanHelper // implement 
{
public:
    LeafEntry* start;
    LeafEntry* cur;
    int scan_size;
    int scanned;

    ScanHelper(int c, char* r) // initializer
    {
        scanned = 0;
        scan_size = c; 
        start = cur = (LeafEntry*)r; 
    }

    void reset() { scanned = 0; cur = start; } // called upon retry

    bool stop() { return scanned >= scan_size; } // this is called once after scanning each leaf

    inline void scanEntry(const LeafEntry& ent) // what to do with each entry scanned
    {
        *cur = ent;
        cur ++;
        scanned ++;
    }
};

inline static bool leafEntryCompareFunc(LeafEntry& a, LeafEntry& b)
{
    return a.key < b.key; 
}

class Node
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

class Inner : public Node
{
public:
    InnerEntry ent[INNER_KEY_NUM + 1]; // count stored in first key

    Inner() 
    { 
        count() = 0;
    #ifdef ADAPTIVE_PREFIX
        prefix_offset() = 0;
    #endif
    }
    // ~Inner() { for (int i = 0; i <= count(); i++) { delete this->ent[i].child; } } ToDo: cannot delete void*
    int& count() { return *((int*)(ent)); }
#ifdef ADAPTIVE_PREFIX
    int& prefix_offset() { return ((int*)(ent))[1]; }
    void adjustPrefix(short index) // for inner that contains inserted key
    {
        int cnt = count(), i;
        if (index == 1 || index == cnt)
        {
            uint16_t prefix_offset = this->prefix_offset();
            int len = std::min(this->ent[1].key.length, this->ent[cnt].key.length), j;
            for (i = 0; i < len; i++)
                if (this->ent[1].key.key[i] != this->ent[cnt].key.key[i])
                    break;
            if (i != prefix_offset)
            {
                this->prefix_offset() = i;
                for (j = 1; j <= cnt; j++)
                    this->ent[j].key.prefix = getPrefixWithOffset(this->ent[j].key.key, this->ent[j].key.length, i);
            }
        }
        else
            adjustPrefix();
    }
    void adjustPrefix() // for inner that only contains original keys
    {
        uint16_t prefix_offset = this->prefix_offset();
        int cnt = this->count();
        while (this->ent[1].key.prefix == this->ent[cnt].key.prefix) 
        {
            prefix_offset += 6;
            this->ent[1].key.prefix = getPrefixWithOffset(this->ent[1].key.key, this->ent[1].key.length, prefix_offset);
            this->ent[cnt].key.prefix = getPrefixWithOffset(this->ent[cnt].key.key, this->ent[cnt].key.length, prefix_offset);
        }
        if (prefix_offset != this->prefix_offset())
        {
            for (int i = 2; i < cnt; i++)
                this->ent[i].key.prefix = getPrefixWithOffset(this->ent[i].key.key, this->ent[i].key.length, prefix_offset);
            this->prefix_offset() = prefix_offset;
        }
    }
#endif
    bool isFull() { return count() == INNER_KEY_NUM; }

    int find(key_type key);
    Node* findChildSetPos(key_type key, short* pos);
    Node* findChild(key_type key);
    inline void insertChild(short index, key_type key, Node* child); // insert key, child at index, does not increment count

} __attribute__((aligned(64)));

class Leaf : public Node
{
public:
    Bitmap bitmap; // 8 byte
#ifdef FINGERPRINT
    uint8_t fp[LEAF_KEY_NUM];
#endif
    LeafEntry ent[LEAF_KEY_NUM];
#ifdef PM
    PMEMoid next[2]; // 32 byte
#else
    Leaf* next[2]; // 16 byte
#endif

    Leaf() 
    { 
    #ifdef PM
        next[0] = next[1] = OID_NULL; 
    #else
        next[0] = next[1] = nullptr; 
    #endif
    }
    Leaf(const Leaf& leaf);
    ~Leaf();

    int count() { return bitmap.count(); }

    Leaf* sibling() 
    { 
    #ifdef PM
        return (Leaf*) pmemobj_direct(next[alt()]);
    #else
        return next[alt()]; 
    #endif
    }

    void insertEntry(key_type key, val_type val);
    int findKey(key_type key); // return position of key if found, -1 if not found
} __attribute__((aligned(256)));

static Inner* allocate_inner() { return new Inner; }

#ifdef PM
    static Leaf* allocate_leaf(PMEMoid* oid) 
    {
    #ifdef ALIGNED_ALLOC
        thread_local pobj_action act;
        // auto x = POBJ_XRESERVE_NEW(pop, dummy, &act, POBJ_CLASS_ID(class_id));
        *oid =  pmemobj_xreserve(pop_, &act, sizeof(Leaf), 0, class_id);
        // D_RW(x)->arr[0] = NULL;
        // D_RW(x)->arr[31] = NULL;
        // (((unsigned long long)(D_RW(x)->arr)) & (~(unsigned long long)(64 - 1)));
        // (((unsigned long long)(D_RW(x)->arr+32)) & (~(unsigned long long)(64 - 1)));
        if (((unsigned long long)pmemobj_direct(*oid)) % 256 != 0)
        {
            printf("leaf(%p): not aligned at 256B\n", pmemobj_direct(*oid));
            exit(1);
        }
    #else
        if (pmemobj_alloc(pop_, oid, sizeof(Leaf), 0, NULL, NULL) != 0)
        {
            printf("pmemobj_alloc\n");
            exit(1);
        }
    #endif
        return (Leaf*) pmemobj_direct(*oid);
    }
#else
    static Leaf* allocate_leaf() { return new Leaf; }
#endif

static void clwb(void* addr, uint32_t len)
{
#ifdef PM
    pmemobj_flush(pop_, addr, len);
#endif
}

static void sfence()
{
#ifdef PM
    pmemobj_drain(pop_);
#endif
}

class tree
{
public:
    Node* root;
    int height; // leaf at level 0

    tree()
    {
        printf("Inner size:%d \n", sizeof(Inner));
        printf("Leaf size:%d \n", sizeof(Leaf));
    #ifdef Binary_Search
        printf("Binary search inner.\n");
    #else
        printf("Linear search inner.\n");
    #endif
    #ifdef FINGERPRINT
        printf("Fingerprint enabled.\n");
    #endif
    #ifdef STRING_KEY
        printf("Using String Key.\n");
    #endif
    #ifdef PREFIX
        printf("Prefix enabled.\n");
    #endif
    #ifdef ADAPTIVE_PREFIX
        printf("Adaptive prefix enabled.\n");
    #endif
    }

    ~tree()
    {
        printf("Tree height: %d \n", height);
    #ifdef PM
        pmemobj_close(pop_);
    #endif
        // delete (Leaf*)root; ToDo: 
    }

    // return true and set val if lookup successful, 
    bool lookup(key_type key, val_type& val);

    // return true if insert successful
    bool insert(key_type key, val_type val);

    // return true if delete successful
    bool del(key_type key);

    // return true if entry with target key is found and val is set to new_val
    bool update(key_type key, val_type new_val);

    // range scan with customized scan helper
    void rangeScan(key_type start_key, ScanHelper& sh);

    void init(const char* pool_path, uint64_t pool_size)
    {
        height = 0;
    #ifdef PM
        struct stat buffer;
        if (stat(pool_path, &buffer) == 0)
        {
            printf("Recovery not implemented!\n");
            exit(1);
        }
        else
        {
            printf("Creating PMEM pool of size: %llu \n", pool_size);
            pop_ = pmemobj_create(pool_path, POBJ_LAYOUT_NAME(Tree), pool_size, 0666);
            if (!pop_)
            {
                printf("pmemobj_create\n");
                exit(1);
            }
            #ifdef ALIGNED_ALLOC
                pobj_alloc_class_desc arg;
                arg.unit_size = sizeof(Leaf);
                arg.alignment = 256;
                arg.units_per_block = 16;
                arg.header_type = POBJ_HEADER_NONE;
                if (pmemobj_ctl_set(pop_, "heap.alloc_class.new.desc", &arg) != 0)
                {
                    printf("Allocation class initialization failed!\n");
                    exit(1);
                }
                class_id = arg.class_id;
            #endif
            PMEMoid p = pmemobj_root(pop_, sizeof(Leaf));
            root = new ((Leaf*)pmemobj_direct(p)) Leaf();
            clwb(root, sizeof(Leaf));
            sfence();
        }
    #else
        root = new (allocate_leaf()) Leaf();
    #endif
    }

private:
    Leaf* findLeaf(key_type key, uint64_t& version, bool lock);
    Leaf* findLeafAssumeSplit(key_type key);
    bool lockStack(Leaf* n);
    inline void initOp(key_type& key);
    inline void resetPrefix(key_type& key);
};

static inline unsigned long long rdtsc(void)
{
   unsigned hi, lo;
   __asm__ __volatile__("rdtsc"
                        : "=a"(lo), "=d"(hi));
   return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

