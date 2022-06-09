#include "tree.h"

// Inner
int Inner::find(key_type key)
{
    int r;
#ifdef PREFIX
    #ifdef ADAPTIVE_PREFIX 
        int node_prefix_offset = this->prefix_offset();
        int res = this->ent[1].key.compare(key, node_prefix_offset);
        if (res != 0) // partial key before prefix do not match, either left most child or right most child
            return res < 0? this->count() : 0;
        if (key_prefix_offset_ != node_prefix_offset) // update search key prefix and offset if necessary
        {
            key_prefix_offset_ = node_prefix_offset;
            key_prefix_ = getPrefixWithOffset(key.key, key.length, key_prefix_offset_);
        }
    #endif
    #ifdef Binary_Search // first ent[i].key >= key, return i-1, otherwise i
        int l = 1, mid; r = this->count();
        while (l <= r) {
            mid = (l + r) >> 1;
            if (key_prefix_ <= this->ent[mid].key.prefix)
                r = mid - 1;
            else
                l = mid + 1;
        }
        if (r < this->count() && key_prefix_ == this->ent[r + 1].key.prefix) // not right most child and may have collision
        {
            int res = key.compare(ent[r + 1].key);
            if (res > 0) // key > ent[r + 1], search right
            {
                for (++r; r <= this->count(); r++)
                    if (key_prefix_ <= this->ent[r].key.prefix && key <= this->ent[r].key)
                        break;
                r --;
            }
            else if (res < 0) // key < ent[r + 1], search left
            {
                for (r; r > 0; r--)
                    if (key_prefix_ >= this->ent[r].key.prefix && key >= this->ent[r].key)
                        break;
            }
        }
    #else
        for (r = 1; r <= this->count(); r++)
            if (key_prefix_ <= this->ent[r].key.prefix && key <= this->ent[r].key)
                break;
        r--;
    #endif
#else
    #ifdef Binary_Search
        int l = 1, mid; r = this->count();
        while (l <= r) {
            mid = (l + r) >> 1;
            if (key <= this->ent[mid].key)
                r = mid - 1;
            else
                l = mid + 1;
        }
    #else
        for (r = 1; r <= this->count(); r++)
            if (key <= this->ent[r].key)
                break;
        r--;
    #endif
#endif
    return r;
}

Node* Inner::findChildSetPos(key_type key, short* pos)
{
    *pos = find(key);
    return this->ent[*pos].child;
}

Node* Inner::findChild(key_type key)
{
    return this->ent[find(key)].child;
}

void Inner::insertChild(short index, key_type key, Node* child)
{
#ifdef ADAPTIVE_PREFIX
    key.prefix = getPrefixWithOffset(key.key, key.length, this->prefix_offset());
#endif
    this->ent[index].key = key;
    this->ent[index].child = child;
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
    resetPrefix(key);
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
    resetPrefix(key);
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

bool tree::lockStack(Leaf* n) // ToDo: Is only locking top most ancester enough
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

    initOp(key);

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

    initOp(key);

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

    initOp(key);

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
                current->insertChild(p, split_key, new_child);
                current->count() ++;
                #ifdef ADAPTIVE_PREFIX
                    current->adjustPrefix(p);
                #endif
                current->unlock();
                return true;
            }
            new_inner = new (allocate_inner()) Inner(); // else split inner
            #ifdef ADAPTIVE_PREFIX
                new_inner->prefix_offset() = current->prefix_offset();
            #endif
#define LEFT_KEY_NUM (INNER_KEY_NUM / 2)
#define RIGHT_KEY_NUM (INNER_KEY_NUM - LEFT_KEY_NUM)
            current->count() = LEFT_KEY_NUM;
            new_inner->count() = RIGHT_KEY_NUM;
            if (p <= LEFT_KEY_NUM) // insert to left inner
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; r >= 0; r--, i--)
                    new_inner->ent[r] = current->ent[i];
                for (i = LEFT_KEY_NUM - 1; i >= p; i--)
                    current->ent[i + 1] = current->ent[i];
                current->insertChild(p, split_key, new_child);
            #ifdef ADAPTIVE_PREFIX
                current->adjustPrefix(p);
                new_inner->adjustPrefix();
            #endif
            }
            else
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; i >= p; i--, r--)
                    new_inner->ent[r] = current->ent[i];
                new_inner->insertChild(r, split_key, new_child);
                for (--r; r >= 0; r--, i--)
                    new_inner->ent[r] = current->ent[i];
            #ifdef ADAPTIVE_PREFIX
                current->adjustPrefix();
                new_inner->adjustPrefix(r);
            #endif
            }
            split_key = new_inner->ent[0].key;
            new_child = new_inner;
            if (level < h) // do not unlock root
                current->unlock();
        }

        // new root
        new_inner = new (allocate_inner()) Inner();
        while (!new_inner->lock()) {}
        new_inner->count() = 1;
        new_inner->ent[0].child = this->root;
        // #ifdef ADAPTIVE_PREFIX
        //     new_inner->prefix_offset() = split_key.length;
        // #endif
        new_inner->insertChild(1, split_key, new_child);

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

    initOp(start_key);

RetryScan:
    sh.reset();
    resetPrefix(key);
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

void tree::initOp(key_type& key)
{
#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key);
#endif
}

void tree::resetPrefix(key_type& key)
{
#ifdef PREFIX
    key_prefix_ = key.prefix;
    #ifdef ADAPTIVE_PREFIX
        key_prefix_offset_ = 0;
    #endif
#endif
}