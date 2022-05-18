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

    inline void scanEntry(const LeafEntry& ent) // tell us what to do with each entry scanned
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
    void unlock() { versionLock.fetch_add(0b10); }
// leaf only
    bool alt() { return versionLock.load() & 0b1; }
    void unlockFlipAlt(bool alt) { alt? versionLock.fetch_add(0b1) : versionLock.fetch_add(0b11); }
};

class Inner : public Node
{
public:
    InnerEntry ent[INNER_KEY_NUM + 1]; // count stored in first key

    Inner() { count() = 0; }
    // ~Inner() { for (int i = 0; i <= count(); i++) { delete this->ent[i].child; } } ToDo: cannot delete void*
    uint64_t& count() { return *((uint64_t*)(ent)); }
    bool isFull() { return count() == INNER_KEY_NUM; }

    Node* findChildSetPos(key_type key, short* pos);
    Node* findChild(key_type key);


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
    #ifdef FINGERPRINT
        printf("Fingerprint enabled.\n");
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

};

static inline unsigned long long rdtsc(void)
{
   unsigned hi, lo;
   __asm__ __volatile__("rdtsc"
                        : "=a"(lo), "=d"(hi));
   return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

