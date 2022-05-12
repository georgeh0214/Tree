#include <iostream>
#include <algorithm>
#include <string.h>

#include "bitmap.h"

// #define PM
// #define Binary_Search

#define INNER_KEY_NUM 14
#define LEAF_KEY_NUM 14 // <= 64 for now, recommand 14/30/46/62
#define MAX_HEIGHT 32 // should be enough

typedef uint64_t key_type; // >= 8 bytes
typedef void* val_type;

inline static uint8_t getOneByteHash(key_type key)
{
    uint8_t oneByteHashKey = std::_Hash_bytes(&key, sizeof(key_type), 1) & 0xff;
    return oneByteHashKey;
}

inline static bool leafEntryCompareFunc(LeafEntry& a, LeafEntry& b)
{
    return a.key < b.key; 
}

static const void* allocate_leaf() { return new Leaf; }

static const void* allocate_inner() { return new Inner; }

/*------------------------------------------------------------------------*/

static const uint64_t offset = (uint64_t)(-1) >> (64 - LEAF_KEY_NUM);
static const uint64_t insert_locked = 1 << 3;
static const uint64_t split_locked = 1 << 2;
static const uint64_t update_locked = 1 << 1;
static const uint64_t delete_locked = 1;


struct InnerEntry
{
    key_type key;
    void* child;
};

struct InnerMeta
{
    int lock;
    int count;
};

struct LeafEntry
{
    key_type key;
    val_type val;
};



class Inner
{
public:
    InnerEntry ent[INNER_KEY_NUM + 1]; // InnerMeta stored in first key

    Inner() { count() = 0; lock() = 0; }
    ~Inner() { for (size_t i = 0; i <= count(); i++) { delete this->ent[i].child; } }
    int& lock() { return (InnerMeta*)(this)->lock; }
    int& count() { return (InnerMeta*)(this)->count; }
    void* findChildSetPos(key_type key, short* pos);
    void* findChild(key_type key);

};


class Leaf
{
public:
    uint64_t version_lock : 59; // 8 byte --> 59 bits version number, 1 bit alt, 1 bit insert/split/update/delete lock
    uint64_t alt : 1;
    uint64_t inset_lock : 1;
    uint64_t split_lock : 1;
    uint64_t update_lock : 1;
    uint64_t delete_lock : 1;
    Bitmap bitmap; // 8 byte
    LeafEntry ent[LEAF_KEY_NUM];
    Leaf* next[2]; // 16 byte

    Leaf();
    Leaf(const Leaf& leaf);
    ~Leaf();
    inline bool isInsertLocked() { return version_lock & insert_locked; }
    inline bool isSplitLocked() { return version_lock & split_locked; }
    inline bool isUpdateLocked() { return version_lock & update_locked; }
    inline bool isDeleteLocked() { return version_lock & delete_locked; }

    inline void setInsertLock() { version_lock |= insert_locked; }
    inline void releaseInsertLock() { version_lock ^= insert_locked; }

    void insertEntry(key_type key, val_type val);
};


class tree
{
public:
    void* root;
    int height; // leaf at level 0
    Leaf* first_leaf;

    tree()
    {
        height = 0;
        root = nullptr;
        first_leaf = nullptr;
        printf("Size of Inner: %d\n", sizeof(Inner));
        printf("Size of Leaf: %d\n", sizeof(Leaf));
        printf("_XBEGIN_STARTED: %d\n", _XBEGIN_STARTED);
        printf("_XABORT_EXPLICIT: %d\n", _XABORT_EXPLICIT);
        printf("_XABORT_RETRY: %d\n", _XABORT_RETRY);
        printf("_XABORT_CONFLICT: %d\n", _XABORT_CONFLICT);
        printf("_XABORT_CAPACITY: %d\n", _XABORT_CAPACITY);
        printf("_XABORT_DEBUG: %d\n", _XABORT_DEBUG);
        printf("_XABORT_NESTED: %d\n", _XABORT_NESTED);
    }

    ~tree()
    {
        delete root;
    }

    // return true and set val only if lookup successful, 
    bool lookup(key_type key, val_type& val);

    // return true if insert successful
    bool insert(key_type key, val_type val);

    // return true if delete successful
    bool del(key_type key);

    // return true if entry with target key is found and val is set to new_val
    bool update(key_type key, val_type new_val);

    // return # of entries scanned
    size_t rangeScan(key_type start_key, size_t scan_size, void* result);

};
