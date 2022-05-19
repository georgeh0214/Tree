#include "tree.h"

// Inner
Node* Inner::findChildSetPos(key_type key, short* pos)
{
#ifdef Binary_Search
    int l = 1, r = this->count(), mid;
    while (l <= r) {
        mid = (l + r) >> 1;
        if (key <= this->ent[mid].key)
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
        if (key <= this->ent[mid].key)
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
#ifdef FINGERPRINT
    this->fp[i] = key_hash_;
#endif
    this->bitmap.set(i);
}

int Leaf::findKey(key_type key)
{
    uint64_t bits = this->bitmap.bits;
    for (int i = 0; bits != 0; i++) 
    {
    #ifdef FINGERPRINT
        if ((bits & 1) && key_hash_ == this->fp[i] && key == this->ent[i].key) // key found
    #else
        if ((bits & 1) && key == this->ent[i].key) // key found
    #endif
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
    previous = nullptr;
    current = this->root;
    if (current->isLocked(currentVersion) || current != this->root)
        goto RetryFindLeaf;
    for (i = this->height; i > 0; i--)
    {
        previous = (Inner*)current;
        previousVersion = currentVersion;
        current = ((Inner*)current)->findChild(key);
        if (current->isLocked(currentVersion) || !previous->checkVersion(previousVersion))
            goto RetryFindLeaf;
    }
    leaf = (Leaf*)current;
    if (lock && !current->upgradeToWriter(currentVersion))
        goto RetryFindLeaf;
    version = currentVersion;
    return leaf;
}

Leaf* tree::findLeafAssumeSplit(key_type key)
{
    uint64_t currentVersion, previousVersion;
    Node* current, * previous;
    Leaf* leaf;
    int i;

RetryFindLeafAssumeSplit:
    previous = nullptr;
    current = this->root;
    if (current->isLocked(currentVersion) || current != this->root)
        goto RetryFindLeafAssumeSplit;
    pos_[0] = this->height;
    for (i = pos_[0]; i > 0; i--)
    {
        anc_[i] = (Inner*)current;
        versions_[i] = currentVersion;
        previous = current;
        previousVersion = currentVersion;
        if (!((Inner*)current)->isFull())
            pos_[0] = i;
        current = (((Inner*)current)->findChildSetPos(key, &pos_[i]));
        if (current->isLocked(currentVersion) || !previous->checkVersion(previousVersion))
            goto RetryFindLeafAssumeSplit;
    }
    leaf = (Leaf*)current;
    if (leaf->findKey(key) >= 0) // key already exists
    {
        if (leaf->checkVersion(currentVersion))
            return nullptr;
        else
            goto RetryFindLeafAssumeSplit;
    }
    if (!leaf->upgradeToWriter(currentVersion))
        goto RetryFindLeafAssumeSplit;
    return leaf;
}

bool tree::lockStack(Leaf* n)
{
    int i, h = pos_[0];
    for (i = 1; i <= h; i++)
        if (!anc_[i]->upgradeToWriter(versions_[i]))
            break;
    if (i <= h)
    {
        for (--i; i > 0; i--)
            anc_[i]->unlock();
        n->unlock();
        return false;
    }
    return true;
}

bool tree::lookup(key_type key, val_type& val)
{
    uint64_t version;
    Leaf* leaf;
    int i;
#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key);
#endif

RetryLookup:
    leaf = findLeaf(key, version, false);
    if ((i = leaf->findKey(key)) >= 0)
        val = leaf->ent[i].val;
    if (!leaf->checkVersion(version))
        goto RetryLookup;
    return i >= 0;
}

bool tree::update(key_type key, val_type new_val)
{
    uint64_t version;
    Leaf* leaf;
    int i;
#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key);
#endif

RetryUpdate:
    leaf = findLeaf(key, version, true);
    if ((i = leaf->findKey(key)) >= 0)
        leaf->ent[i].val = new_val;
    leaf->unlock();
    return i >= 0;
}

bool tree::insert(key_type key, val_type val) 
{
    Inner* current;
    Leaf* leaf;
    int i, r, count;
#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key);
#endif

RetryInsert: 
    leaf = findLeafAssumeSplit(key);
    if (!leaf) return false;
    if (leaf->count() < LEAF_KEY_NUM) // no split
    {
        leaf->insertEntry(key, val);
        leaf->unlock();
        return true;
    }
    else // need split
    {
        if (!lockStack(leaf))
            goto RetryInsert;
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
        for (i = 0; i <= split_pos; i++)
            new_leaf->bitmap.reset(sorted_pos[i]);
        leaf->bitmap.setBits(new_leaf->bitmap.bits);
        leaf->bitmap.flip();

        // insert new entry, unlock leaf if not root
        if (key <= split_key) 
            leaf->insertEntry(key, val);
        else
            new_leaf->insertEntry(key, val);
        new_leaf->unlock();
        if (pos_[0] > 0)
            leaf->unlockFlipAlt(alt);

        // Update inners
        Node* new_child = new_leaf;
        Inner* new_inner;
        int h = pos_[0];
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
            if (level < h) // do not unlock root
                current->unlock();
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
        if (++(this->height) > MAX_HEIGHT)
            printf("Stack overflow!\n");
        new_inner->unlock();
        if (this->height > 1) // previous root is a nonleaf
            ((Inner*)old_root)->unlock();
        else // previous root is a leaf
            ((Leaf*)old_root)->unlockFlipAlt(alt);
        return true;
    }
}

void tree::rangeScan(key_type start_key, ScanHelper& sh)
{
    Leaf* leaf, * next_leaf;
    uint64_t leaf_version, next_version, bits;
    int i;
    // std::vector<Leaf*> leaves; // ToDo: is phantom allowed?

RetryScan:
    sh.reset();
    leaf = findLeaf(start_key, leaf_version, false);
    bits = leaf->bitmap.bits;
    for (int i = 0; bits != 0; i++, bits = bits >> 1)
        if ((bits & 1) && leaf->ent[i].key >= start_key) // compare with key in first leaf
            sh.scanEntry(leaf->ent[i]);
    next_leaf = leaf->sibling();
    if (!next_leaf)
    {
	if (!leaf->checkVersion(leaf_version))
	    goto RetryScan;
	return;
    }
    if (next_leaf->isLocked(next_version) || !leaf->checkVersion(leaf_version))
        goto RetryScan;
    leaf = next_leaf;
    leaf_version = next_version;
    do {
        bits = leaf->bitmap.bits;
        for (int i = 0; bits != 0; i++, bits = bits >> 1) 
            if (bits & 1) // entry found
                sh.scanEntry(leaf->ent[i]);
        next_leaf = leaf->sibling();
	if (!next_leaf)
        { 
            if (!leaf->checkVersion(leaf_version))
                goto RetryScan;
            return;
        }
        if (next_leaf->isLocked(next_version) || !leaf->checkVersion(leaf_version))
            goto RetryScan;
        leaf = next_leaf;
        leaf_version = next_version;
    } while (!sh.stop());
}
