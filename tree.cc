#include "tree.h"

void* Inner::findChildSetPos(key_type key, short* pos)
{
#ifdef Binary_Search
    int l = 1, r = this->count(), mid;
    while (l <= r) {
        mid = (l + r) >> 1;
        if (key <= this->ent[middle].key)
            r = mid - 1;
        else
            l = mid + 1;
    }
    *pos = r;
    return this->ent[r].child;
#else
    int i;
    for (i = 1; i <= this->count(); i++)
        if (key <= this->ent[i].key)
            break;
    *pos = --i;
    return this->ent[i].child;
#endif
}

void* Inner::findChild(key_type key)
{
#ifdef Binary_Search
    int l = 1, r = this->count(), mid;
    while (l <= r) {
        mid = (l + r) >> 1;
        if (key <= this->ent[middle].key)
            r = mid - 1;
        else
            l = mid + 1;
    }
    return this->ent[r].child;
#else
    int i;
    for (i = 1; i <= this->count(); i++)
        if (key <= this->ent[i].key)
            break;
    return this->ent[i].child;
#endif
}

Leaf::Leaf() 
{
    std::memset(this, 0, sizeof(Leaf));
}

Leaf::Leaf(const Leaf& leaf) 
{
    memcpy(this, &leaf, sizeof(Leaf));
}

void Leaf::insertEntry(key_type key, val_type val)
{
    int i = leaf->bitmap.first_zero();
    leaf->ent[i].key = key;
    leaf->ent[i].val = val;
    leaf->bitmap.set(i);
}

bool tree::lookup(key_type key, val_type& val)
{
    Inner* cur;
    Leaf* leaf;
    uint64_t bits;
    int i;
    uint8_t key_hash = hashcode1B(key);
RetryLookup:
    if (_xbegin() != _XBEGIN_STARTED)
        goto RetryLookup;
    // Search inners
    cur = (Inner*)(this->root);
    for (i = this->height; i > 0; i--)
    {
        if (cur->lock())
        {
            _xabort(1);
            goto RetryLookup;
        }
        cur = (Inner*)(cur->findChild(key));
    }

    // Search leaf
    leaf = (Leaf*)cur;
    if (leaf->isInsertLocked())
    {
        _xabort(2);
        goto RetryLookup;
    }
    bits = leaf->bitmap.bits;
    for (i = 0; bits != 0; i++) 
    {
        if (bits & 1)
        {
            count ++;
            if (key == leaf->ent[i].key) // key found
            {
                val = leaf->ent[i].val;
                _xend();
                return true;
            }
        }
        bits = bits >> 1;
    }
    _xend();
    return false;
}

bool tree::insert(key_type key, val_type val) 
{
    thread_local Inner* anc[MAX_HEIGHT];
    thread_local short pos[MAX_HEIGHT];

    volatile long long sum; // for simple backoff
    Inner* cur;
    Leaf* leaf;
    uint64_t bits;
    int i, r, count;
    uint8_t key_hash = hashcode1B(key);

RetryInsert: 
    if (_xbegin() != _XBEGIN_STARTED)
    {
        sum= 0;
        for (int i=(rdtsc() % 1024); i>0; i--) sum += i;
        goto RetryInsert;
    }
    // Search inners
    cur = (Inner*)(this->root);
    for (i = this->height; i > 0; i--)
    {
        if (cur->lock())
        {
            _xabort(3);
            goto RetryInsert;
        }
        anc[i] = cur;
        cur = (Inner*)(cur->findChildSetPos(key, &pos[i]));
    }

    // Search leaf
    leaf = (Leaf*)cur;
    if (leaf->isInsertLocked())
    {
        _xabort(4);
        goto RetryInsert;
    }
    bits = leaf->bitmap.bits;
    for (i = 0; bits != 0; i++) 
    {
        if (bits & 1)
        {
            count ++;
            if (key == leaf->ent[i].key) // key exists, return false
            {
                _xend();
                return false;
            }
        }
        bits = bits >> 1;
    }
    leaf->setInsertLock();
    if (count == LEAF_KEY_NUM) // need split, lock ancestors
        for (i = 1; i <= this->height; i++)
        {
            anc[i]->lock() = 1;
            if (anc[i]->count() < INNER_KEY_NUM)
                break;
        }
    _xend();

    // If no split
    if (count != LEAF_KEY_NUM)
    {
        leaf->insertEntry(key, val);
        leaf->releaseInsertLock();
        return true;
    }
    else // split insert
    {
        std::sort(leaf->ent, leaf->ent + LEAF_KEY_NUM, leafEntryCompareFunc); // sort all entries
#define MID (LEAF_KEY_NUM / 2)
        key_type split_key = leaf->ent[MID].key; // middle key as split key
        bits = leaf->bitmap.bits ^ (offset >> MID); // set right half bits in bitmap to 0
        leaf->bitmap.setBits(bits);
        Leaf* new_leaf = new allocate_leaf() Leaf(leaf); // alloc new leaf
        leaf->next[1 - leaf->alt] = new_leaf; // track using unused ptr
        new_leaf->bitmap.flip(); // set bitmap of new leaf
        if (key < split_key) // insert new entry
            leaf->insertEntry(key, val);
        else
            new_leaf->insertEntry(key, val);
        new_leaf->releaseInsertLock();
        if (this->height > 0)
            leaf->releaseInsertLock();
        // Update inners
        void* new_child = (void*)new_leaf;
        for (int level = 1; level <= this->height; level++)
        {
            cur = anc[level];
            count = cur->count();
            short p = pos[level] + 1;
            if (count < INNER_KEY_NUM) // if last inner to update
            {
                for (i = count; i >= p; i--)
                    cur->ent[i + 1] = cur->ent[i];
                cur->ent[p].key = split_key;
                cur->ent[p].child = new_child;
                cur->count() ++;
                cur->lock() = 0;
                return true;
            }
            Inner* new_inner = new allocate_inner() Inner(); // else split inner
#define LEFT_KEY_NUM (INNER_KEY_NUM / 2)
#define RIGHT_KEY_NUM (INNER_KEY_NUM - LEFT_KEY_NUM)
            if (p <= LEFT_KEY_NUM) // insert to left inner
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; r >= 0; r--, i--)
                    new_inner->ent[r] = cur->ent[i];
                for (i = LEFT_KEY_NUM - 1; i >= p; i--)
                    cur->ent[i + 1] = cur->ent[i];
                cur->ent[p].key = split_key;
                cur->ent[p].child = new_child;
            }
            else
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; i >= p; i--, r--)
                    new_inner->ent[r] = cur->ent[i];
                new_inner->ent[p].key = split_key;
                new_inner->ent[p].child = new_child;
                for (--r; r >= 0; r--, i--)
                    new_inner->ent[r] = cur->ent[i];
            }
            split_key = new_inner->ent[0].key;
            new_child = (void*)new_inner;
            cur->count() = LEFT_KEY_NUM;
            if (level < this->height) // do not clear lock bit of root
                cur->lock() = 0; 
            new_inner->count() = RIGHT_KEY_NUM;
            new_inner->lock() = 0;
        }

        new_inner = new allocate_inner() Inner();
        new_inner->lock() = 1;
        new_inner->count() = 1;
        new_inner->ent[0].child = this->root;
        new_inner->ent[1].child = new_child;
        new_inner->ent[1].key = split_key;

        void* old_root = this->root;
        this->root = new_inner;
        this->height ++;
        if (this->height > 1) // previous root is a nonleaf
            ((Inner*)old_root)->lock() = 0;
        else // previous root is a leaf
            ((Leaf*)old_root)->releaseInsertLock();
        new_inner->lock() = 0;
        return true;
    }
}