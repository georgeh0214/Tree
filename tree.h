#include <iostream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <vector>

#include "bitmap.h"

/*------------------------------------------------------------------------*/

class Inner;
thread_local static Inner* anc_[MAX_HEIGHT];
thread_local static short pos_[MAX_HEIGHT];
thread_local static uint64_t versions_[MAX_HEIGHT];
thread_local static uint8_t key_hash_;

// thread_local static char split_key_[MAX_KEY_LENGTH];


class Node;
struct InnerEntry
{
    int offset;
    Node* child;
};

struct LeafEntry
{
#ifdef FINGERPRINT
    uint8_t fp;
#endif
    int offset;
    val_type val;
};

class ScanHelper // implement 
{
public:
    // LeafEntry* start;
    // LeafEntry* cur;
    // int scan_size;
    // int scanned;

    // ScanHelper(int c, char* r) // initializer
    // {
    //     scanned = 0;
    //     scan_size = c; 
    //     start = cur = (LeafEntry*)r; 
    // }

    // void reset() { scanned = 0; cur = start; } // called upon retry

    // bool stop() { return scanned >= scan_size; } // this is called once after scanning each leaf

    // inline void scanEntry(const LeafEntry& ent) // what to do with each entry scanned
    // {
    //     *cur = ent;
    //     cur ++;
    //     scanned ++;
    // }
};

// inline static bool leafEntryCompareFunc(LeafEntry& a, LeafEntry& b)
// {
//     return true; // ToDo
// }

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
    int cnt;
//    int meta_size;
    InnerEntry ent[]; // offset + ptr pairs

    Inner() 
    {
        count() = 0; 
//        updateMeta();
        ent[0].offset = INNER_SIZE;
    }
    // ~Inner() { for (int i = 0; i <= count(); i++) { delete this->ent[i].child; } } ToDo: cannot delete void*

    inline int& count() { return cnt; }

    inline char* getKey(int idx)
    {
        assert(idx >= 1 && idx <= cnt);
        return ((char*)this) + ent[idx].offset;
    }

    inline uint16_t getLen(int idx)
    {
        assert(idx >= 1 && idx <= cnt);
        return ent[idx - 1].offset - ent[idx].offset;
    }

    inline int freeSpace()
    {
	return ent[count()].offset - ((char*)(&(ent[count() + 1])) - ((char*)this)) - sizeof(InnerEntry);
        //return ent[count()].offset - meta_size - sizeof(InnerEntry);
    }

    inline int keySpace()
    {
        return INNER_SIZE - ent[count()].offset;
    }

//    inline void updateMeta()
//    {
//        meta_size = (char*)(&(ent[count() + 1])) - ((char*)this);
//    }

    inline bool isFull() { return freeSpace() < MAX_KEY_LENGTH; } // assume worst case during split

    int find(char* key, int len);
    int halfIndex();
    inline Node* findChildSetPos(char* key, int len, short* pos);
    inline Node* findChild(char* key, int len);
    // insert key, child entry at index, increment cnt, adjust meta_size
    inline void insertChild(short index, char* key, int len, Node* child); 
    inline void makeSpace(int index, int len); // shift keys from index i and beyond by len, entries from index i and beyond by 1
};// __attribute__((aligned(64)));

class Leaf : public Node
{
public:
//    int meta_size;
    Leaf* next[2]; // 16 byte
    LeafEntry ent[];
    
    Leaf() 
    {
        count() = 0;
        next[0] = next[1] = nullptr; 
//        updateMeta();
        ent[0].offset = LEAF_SIZE;
    }
    Leaf(const Leaf& leaf);
    ~Leaf();

    inline int& count() { return *(int*)(&(ent[0].val)); }

    inline char* getKey(int idx)
    {
        assert(idx >= 1 && idx <= count());
        return ((char*)this) + ent[idx].offset;
    }

    inline uint16_t getLen(int idx)
    {
        assert(idx >= 1 && idx <= count());
        return ent[idx - 1].offset - ent[idx].offset;
    }

    inline int freeSpace()
    {
	return ent[count()].offset - ((char*)(&(ent[count() + 1])) - ((char*)this)) - sizeof(LeafEntry);
        //return ent[count()].offset - meta_size - sizeof(LeafEntry);
    }

    inline int keySpace()
    {
        return LEAF_SIZE - ent[count()].offset;
    }

//    inline void updateMeta()
//    {
//        meta_size = (char*)(&(ent[count() + 1])) - ((char*)this);
//    }

    Leaf* sibling() { return next[alt()]; }
    inline void appendKey(char* key, int len, val_type val); // append key, entry, increment count, update meta
    inline void appendKeyEntry(char* key, int len, LeafEntry entry); // append key, entry with new offset, increment count, update meta
    int consolidate(std::vector<int>& vec, int len);
    int findKey(char* key, int len); // return position of key if found, -1 if not found
}; // __attribute__((aligned(64)));

static Inner* allocate_inner() 
{
    return (Inner*) new char[INNER_SIZE]; 
}

static Leaf* allocate_leaf() 
{ 
    return (Leaf*) new char[LEAF_SIZE];
}

// static char* allocate_key(int size)
// {
//     return new char[size];
// }

class tree
{
public:
    Node* root;
    int height; // leaf at level 0
    Leaf* first_leaf;

    tree()
    {
        printf("Using In-place String Key.\n");
        printf("Inner size:%d \n", INNER_SIZE);
        printf("Leaf size:%d \n", LEAF_SIZE);
    #ifdef Binary_Search
        printf("Binary search.\n");
    #else
        printf("Linear search.\n");
    #endif
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
    bool lookup(char* key, int len, val_type& val);

    // return true if insert successful
    bool insert(char* key, int len, val_type val);

    // return true if delete successful
    bool del(char* key, int len);

    // return true if entry with target key is found and val is set to new_val
    bool update(char* key, int len, val_type new_val);

    // range scan with customized scan helper class
    void rangeScan(char* start_key, int len, ScanHelper& sh);

private:
    Leaf* findLeaf(char* key, int len, uint64_t& version, bool lock);
    Leaf* findLeafAssumeSplit(char* key, int len);
    bool lockStack(Leaf* n);
};

static inline unsigned long long rdtsc(void)
{
   unsigned hi, lo;
   __asm__ __volatile__("rdtsc"
                        : "=a"(lo), "=d"(hi));
   return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

