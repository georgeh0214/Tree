#include <iostream>
#include <algorithm>
#include <cstring>
#include <atomic>

#include "bitmap.h"

/*------------------------------------------------------------------------*/

class Inner;
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
        for (int i = 2; i < cnt; i++)
            this->ent[i].key.prefix = getPrefixWithOffset(this->ent[i].key.key, this->ent[i].key.length, prefix_offset);
        this->prefix_offset() = prefix_offset;
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
    Leaf* next[2]; // 16 byte

    Leaf() { next[0] = next[1] = nullptr; }
    Leaf(const Leaf& leaf);
    ~Leaf();

    int count() { return bitmap.count(); }
    Leaf* sibling() { return next[alt()]; }
    void insertEntry(key_type key, val_type val);
    int findKey(key_type key); // return position of key if found, -1 if not found
} __attribute__((aligned(64)));

static Inner* allocate_inner() { return new Inner; }

static Leaf* allocate_leaf() { return new Leaf; }

class tree
{
public:
    Node* root;
    int height; // leaf at level 0
    Leaf* first_leaf;

    tree()
    {
        printf("Inner size:%d \n", sizeof(Inner));
        printf("Leaf size:%d \n", sizeof(Leaf));
    #ifdef Binary_Search
        printf("Binary search.\n");
    #else
        printf("Linear search.\n");
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
        height = 0;
        root = new (allocate_leaf()) Leaf();
        first_leaf = (Leaf*)root;
    }

    ~tree()
    {
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

    // range scan with customized scan helper class
    void rangeScan(key_type start_key, ScanHelper& sh);

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

