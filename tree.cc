#include "tree.h"

// Inner
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

// Leaf
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

int Leaf::findKey(key_type key)
{
    uint64_t bits = this->bitmap.bits;
    for (int i = 0; bits != 0; i++) 
    {
        if ((bits & 1) && key == this->ent[i].key) // key found
            return i;
        bits = bits >> 1;
    }
    return -1;
}

// Tree
Leaf* tree::findLeaf(key_type key, uint64_t& version, bool lock)
{
    uint64_t currentVersion, previousVersion;
    Node* current;
    Inner* previous;
    Leaf* leaf;
    int i;

RetryFindLeaf: 
    current = this->root;
    if (current->isLocked(currentVersion) || current != this->root)
        goto RetryFindLeaf;
    for (i = this->height; i > 0; i--)
    {
        previous = (Inner*)current;
        previousVersion = currentVersion;
        current = ((Inner*)current)->findChild(key);
        if (!previous->checkVersion(previousVersion) || current->isLocked(currentVersion))
            goto RetryFindLeaf;
    }
    leaf = (Leaf*)current;
    if (lock && !current->upgradeToWriter(currentVersion))
        goto RetryFindLeaf;
    version = currentVersion;
    return leaf;
}

Leaf* tree::findLeafAssumeSplit(key_type key, Node** ancestor, int& count)
{
    uint64_t currentVersion, previousVersion;
    Inner* current, * previous;
    Leaf* leaf;
    int i;

RetryFindLeafAssumeSplit:
    current = (Inner*)this->root;
    if (!current->lock())
        goto RetryFindLeafAssumeSplit;
    if (current != this->root)
    {
        current->unlock();
        goto RetryFindLeafAssumeSplit;
    }
    previous = current;
    for (i = this->height; i > 1; i--)
    {
        anc_[i] = current;
        current = (Inner*)(current->findChildSetPos(key, &pos_[i]));
        if (!current->lock()) // lock current
        {
            previous->unlock();
            goto RetryFindLeafAssumeSplit;
        }
        if (!current->isFull()) // new previous
        {
            previous->unlock();
            previous = current;
        }
        else
            current->unlock();

        // if (!current->isFull())
        // {
        //     if (!current->lock())
        //     {
        //         previous->unlock();
        //         goto RetryInsert;
        //     }
        //     previous->unlock();
        //     previous = current;
        // }
        // else if (current->isLocked(currentVersion))
        // {
        //     previous->unlock();
        //     goto RetryInsert;
        // }
    }
    if (i == 1) // last layer inner
    {
        anc_[i] = current;
        leaf = (Leaf*)(current->findChildSetPos(key, &pos_[i]));
        if (!leaf->lock())
        {
            previous->unlock();
            goto RetryFindLeafAssumeSplit;
        }
        if ((count = leaf->count()) < LEAF_KEY_NUM) // leaf not full
        {
            previous->unlock();
            *ancestor = nullptr;
        }
        else
            *ancestor = previous;
    }
    else // no inner
    {
        *ancestor = nullptr;
        leaf = (Leaf*)current;
        count = leaf->count();
    }
    return leaf;
}

bool tree::lookup(key_type key, val_type& val)
{
    uint64_t version;
    Leaf* leaf;
    int i;

RetryLookup:
    leaf = findLeaf(key, version, false);
    if ((i = leaf->findKey(key)) >= 0)
        val = leaf->ent[i].val;
    if (!leaf->checkVersion(version))
        goto RetryLookup;
    return i >= 0;


    // current = this->root;
    // if (current->isLocked(currentVersion))
    //     goto RetryLookup;

    // for (i = this->height; i > 0; i--)
    // {
    //     if (current->isLocked(currentVersion))
    //         goto RetryLookup;
    //     previous = current;
    //     previousVersion = currentVersion;
    //     current = (((Inner*)current)->findChild(key));
    //     if (!previous->checkVersion(previousVersion))
    //         goto RetryLookup;
    // }

    // // Search leaf
    // leaf = (Leaf*)current;
    // if (leaf->isLocked(currentVersion))
    //     goto RetryLookup;
    // if ((i = leaf->findKey(key)) >= 0)
    //     val = leaf->ent[i].val;
    // if (!leaf->checkVersion(currentVersion))
    //     goto RetryLookup;
    // return i >= 0;
}

bool tree::insert(key_type key, val_type val) 
{
    uint64_t currentVersion;
    Node* ancestor;
    Inner* current;
    Leaf* leaf;
    int i, r, count;

    leaf = findLeaf(key, currentVersion, true); // only lock leaf

RetryInsert: 
    if (leaf->count() < LEAF_KEY_NUM) // no split
    {
        if (leaf->findKey(key) >= 0) // key already exists
        {
            leaf->unlock();
            return false;
        }
    InsertReturn:
        leaf->insertEntry(key, val);
        leaf->unlock();
        return true;
    }
    else // need split, retraverse and lock both ancester and leaf
    {
        leaf->unlock();
        leaf = findLeafAssumeSplit(key, &ancestor, count);
        if (leaf->findKey(key) >= 0) // key already exists
        {
            if (ancestor) ancestor->unlock();
            leaf->unlock();
            return false;
        }
        if (count < LEAF_KEY_NUM) // node was split by other thread
        {
            if (ancestor) ancestor->unlock();
            goto InsertReturn;
        }
        // split insert
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
            current = anc_[level];
            count = current->count();
            short p = pos_[level] + 1;
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
