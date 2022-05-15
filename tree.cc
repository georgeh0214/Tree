#include "tree.h"

Node* Inner::findChildSetPos(key_type key, short* pos)
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
    uint64_t i;
    for (i = 1; i <= this->count(); i++)
        if (key <= this->ent[i].key)
            break;
    *pos = --i;
    return this->ent[i].child;
#endif
}

Node* Inner::findChild(key_type key)
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
    uint64_t i;
    for (i = 1; i <= this->count(); i++)
        if (key <= this->ent[i].key)
            break;
    return this->ent[--i].child;
#endif
}

Leaf::Leaf(const Leaf& leaf) 
{
    memcpy(this, &leaf, sizeof(Leaf));
}

void Leaf::insertEntry(key_type key, val_type val)
{
    int i = this->bitmap.first_zero();
    this->ent[i].key = key;
    this->ent[i].val = val;
    this->bitmap.set(i);
}

bool tree::lookup(key_type key, val_type& val)
{
    uint64_t bits, currentVersion, previousVersion;
    Node* current, * previous;
    Leaf* leaf;
    int i;
    bool ret;

RetryLookup:
    ret = false;
    current = this->root;
    if (current->isLocked(currentVersion))
        goto RetryLookup;

    for (i = this->height; i > 0; i--)
    {
        if (current->isLocked(currentVersion))
            goto RetryLookup;
        previous = current;
        previousVersion = currentVersion;
        current = (((Inner*)current)->findChild(key));
        if (!previous->checkVersion(previousVersion))
            goto RetryLookup;
    }

    // Search leaf
    leaf = (Leaf*)current;
    if (leaf->isLocked(currentVersion))
        goto RetryLookup;
    bits = leaf->bitmap.bits;
    for (i = 0; bits != 0; i++) 
    {
        if ((bits & 1) && key == leaf->ent[i].key) // key found
        {
            val = leaf->ent[i].val;
            ret = true;
            break;
        }
        bits = bits >> 1;
    }
    if (!leaf->checkVersion(currentVersion))
        goto RetryLookup;
    return ret;
}

bool tree::insert(key_type key, val_type val) 
{
    thread_local Inner* anc[MAX_HEIGHT];
    thread_local short pos[MAX_HEIGHT];

    uint64_t bits;
    Inner* current, * previous;
    Leaf* leaf;
    int i, r, count;

RetryInsert: 
    if (!this->root->lock())
        goto RetryInsert;
    current = (Inner*)this->root;
    previous = current;
    for (i = this->height; i > 1; i--)
    {
        anc[i] = current;
        current = (Inner*)(current->findChildSetPos(key, &pos[i]));
        if (!current->isFull())
        {
            if (!current->lock())
            {
                previous->unlock();
                goto RetryInsert;
            }
            previous->unlock();
            previous = current;
        }
    }
    if (i == 1)
    {
        anc[i] = current;
        leaf = (Leaf*)(current->findChildSetPos(key, &pos[i]));
        if (!leaf->lock())
        {
            previous->unlock();
            goto RetryInsert;
        }
        if (!leaf->bitmap.is_full())
            previous->unlock();
    }
    else
        leaf = (Leaf*)current;

    // Search leaf
    bits = leaf->bitmap.bits;
    count = 0;
    for (i = 0; bits != 0; i++) 
    {
        if (bits & 1)
        {
            count ++;
            if (key == leaf->ent[i].key) // key exists, return false
                return false;
        }
        bits = bits >> 1;
    }

    // No split
    if (count != LEAF_KEY_NUM)
    {
        leaf->insertEntry(key, val);
        leaf->unlock();
        return true;
    }
    else // split insert
    {
        // sort indexes of leaf entries, find middle split key
        int sorted_pos[LEAF_KEY_NUM];
        for (i = 0; i < LEAF_KEY_NUM; i++)
            sorted_pos[i] = i;
        LeafEntry* entries = leaf->ent;
        std::sort(sorted_pos, sorted_pos + LEAF_KEY_NUM, [entries](int i, int j){ return entries[i].key < entries[j].key; });
        int split_pos = LEAF_KEY_NUM / 2;
        key_type split_key = leaf->ent[sorted_pos[split_pos]].key;
        bool alt = leaf->alt();

        // alloc new leaf
        Leaf* new_leaf = new (allocate_leaf()) Leaf(*leaf);
        leaf->next[1 - alt] = new_leaf; // track in unused ptr

        // set bitmap of both leaves
        for (i = 0; i < split_pos; i++)
            new_leaf->bitmap.reset(sorted_pos[i]);
        leaf->bitmap.setBits(new_leaf->bitmap.bits);
        leaf->bitmap.flip();

        // insert new entry, unlock leaf if not root
        if (key <= split_key) 
            leaf->insertEntry(key, val);
        else
            new_leaf->insertEntry(key, val);
        new_leaf->unlock();
        if (this->height > 0)
            leaf->unlockFlipAlt(alt);

        // Update inners
        Node* new_child = new_leaf;
        Inner* new_inner;
        int h = this->height;
        for (int level = 1; level <= h; level++)
        {
            current = anc[level];
            count = current->count();
            short p = pos[level] + 1;
            if (count < INNER_KEY_NUM) // if last inner to update
            {
                for (i = count; i >= p; i--)
                    current->ent[i + 1] = current->ent[i];
                current->ent[p].key = split_key;
                current->ent[p].child = new_child;
                current->count() ++;
                current->unlock();
                return true;
            }
            new_inner = new (allocate_inner()) Inner(); // else split inner
#define LEFT_KEY_NUM (INNER_KEY_NUM / 2)
#define RIGHT_KEY_NUM (INNER_KEY_NUM - LEFT_KEY_NUM)
            if (p <= LEFT_KEY_NUM) // insert to left inner
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; r >= 0; r--, i--)
                    new_inner->ent[r] = current->ent[i];
                for (i = LEFT_KEY_NUM - 1; i >= p; i--)
                    current->ent[i + 1] = current->ent[i];
                current->ent[p].key = split_key;
                current->ent[p].child = new_child;
            }
            else
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; i >= p; i--, r--)
                    new_inner->ent[r] = current->ent[i];
                new_inner->ent[r].key = split_key;
                new_inner->ent[r].child = new_child;
                for (--r; r >= 0; r--, i--)
                    new_inner->ent[r] = current->ent[i];
            }
            split_key = new_inner->ent[0].key;
            new_child = new_inner;
            current->count() = LEFT_KEY_NUM;
            new_inner->count() = RIGHT_KEY_NUM;
        }

        // new root
        new_inner = new (allocate_inner()) Inner();
        while (!new_inner->lock()) {}
        new_inner->count() = 1;
        new_inner->ent[0].child = this->root;
        new_inner->ent[1].child = new_child;
        new_inner->ent[1].key = split_key;

        Node* old_root = this->root;
        this->root = new_inner;
        this->height ++;
        if (this->height > 1) // previous root is a nonleaf
            ((Inner*)old_root)->unlock();
        else // previous root is a leaf
            ((Leaf*)old_root)->unlockFlipAlt(alt);
        new_inner->unlock();
        return true;
    }
}
