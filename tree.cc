#include "tree.h"

// Inner
int Inner::find(char* key, int len)
{
    int r;
#ifdef Binary_Search
    int l = 1, mid; r = this->count();
    while (l <= r) {
        mid = (l + r) >> 1;
        if (compare(key, len, getKey(mid), getLen(mid)) <= 0)
            r = mid - 1;
        else
            l = mid + 1;
    }
#else
    for (r = 1; r <= this->count(); r++)
        if (compare(key, len, getKey(r), getLen(r)) <= 0)
            break;
    r--;
#endif
    return r;
}

Node* Inner::findChildSetPos(char* key, int len, short* pos)
{
    *pos = find(key, len);
    return this->ent[*pos].child;
}

Node* Inner::findChild(char* key, int len)
{
    return this->ent[find(key, len)].child;
}

void Inner::insertChild(short index, char* key, int len, Node* child)
{
    this->ent[index].offset = this->ent[index - 1].offset - len;
    this->ent[index].child = child;
    memcpy(getKey(index), key, len);
    this->meta_size += sizeof(InnerEntry);
    count() ++;
}

// Leaf
Leaf::Leaf(const Leaf& leaf) 
{
    versionLock.store(leaf->versionLock.load());
    next[0] = leaf->next[0];
    next[1] = leaf->next[1];
    meta_size = (char*)(&(ent[1])) - ((char*)this);
    ent[0].offset = LEAF_SIZE;
    count() = 0;
}

void Leaf::appendKeyEntry(char* key, int len, val_type val)
{
    int index = ++ count();
#ifdef FINGERPRINT
    this->ent[index].fp = key_hash_;
#endif
    this->ent[index].offset = this->ent[index - 1].offset - len;
    this->ent[index].val = val;
    memcpy(getKey(index), key, len);
    this->meta_size += sizeof(LeafEntry);
}

int Leaf::appendKey(char* key, int len)
{
    int offset = this->ent[count() ++].offset - len;
    memcpy(((char*)this) + offset, key, len);
    this->meta_size += sizeof(LeafEntry);
    return offset;
}

void Leaf::updateMeta()
{
    meta_size = (char*)(&(ent[count() + 1])) - ((char*)this);
}

void Leaf::consolidate(std::vector<int>& vec, int len)
{
    std::sort(vec.begin(), vec.begin() + len);
    count() = 0;
    int i, idx, offset;
    for (i = 0; i < len; i++)
    {
        idx = vec[i];
        offset = appendKey(getKey(idx), getLen(idx)); // ToDo: is memcpy safe to use with overlapping addresses
        this->ent[count()] = this->ent[idx];
        this->ent[count()].offset = offset;
    }
    updateMeta();
}

int Leaf::findKey(char* key, int len)
{
    int cnt = count();
    for (int i = 1; i <= cnt; i++)
    {
    #ifdef FINGERPRINT
        if (key_hash_ == this->ent[i].fp && compare(key, len, getKey(i), getLen(i)) == 0) // key found
    #else
        if (compare(key, len, getKey(i), getLen(i)) == 0) // key found
    #endif
            return i;
    }
    return -1;
}

Leaf* tree::findLeaf(char* key, int len, uint64_t& version, bool lock)
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
        current = ((Inner*)current)->findChild(key, len);
        if (current->isLocked(currentVersion) || !previous->checkVersion(previousVersion))
            goto RetryFindLeaf;
    }
    leaf = (Leaf*)current;
    if (lock && !current->upgradeToWriter(currentVersion))
        goto RetryFindLeaf;
    version = currentVersion;
    return leaf;
}

Leaf* tree::findLeafAssumeSplit(char* key, int len)
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
        current = (((Inner*)current)->findChildSetPos(key, len, &pos_[i]));
        if (current->isLocked(currentVersion) || !previous->checkVersion(previousVersion))
            goto RetryFindLeafAssumeSplit;
    }
    leaf = (Leaf*)current;
    if (leaf->findKey(key, len) >= 0) // key already exists
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

bool tree::lookup(char* key, int len, val_type& val)
{
    uint64_t version;
    Leaf* leaf;
    int i;

#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key, len);
#endif

RetryLookup:
    leaf = findLeaf(key, len, version, false);
    if ((i = leaf->findKey(key, len)) >= 0)
        val = leaf->ent[i].val;
    if (!leaf->checkVersion(version))
        goto RetryLookup;
    return i >= 0;
}

bool tree::update(char* key, int len, val_type new_val)
{
    uint64_t version;
    Leaf* leaf;
    int i;

#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key, len);
#endif

RetryUpdate:
    leaf = findLeaf(key, len, version, true);
    if ((i = leaf->findKey(key, len)) >= 0)
        leaf->ent[i].val = new_val;
    leaf->unlock();
    return i >= 0;
}

bool tree::insert(char* key, int len, val_type val) 
{
    Leaf* leaf;
    
#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key, len);
#endif

RetryInsert: 
    leaf = findLeafAssumeSplit(key, len);
    if (!leaf) return false;
    if (leaf->freeSpace() > len) // no split
    {
        leaf->appendKeyEntry(key, len, val);
        leaf->unlock();
        return true;
    }
    else // need split
    {
        if (!lockStack(leaf))
            goto RetryInsert;

        Node* new_child;
        Inner* current, new_inner;
        Leaf* new_leaf;
        int i, idx, l, mid, r, cnt, half_space, offset, split_key_len;
        char* split_key;
        std::vector<int> sorted_pos(cnt); 
        bool alt;

        // sort leaf key indirection array
        cnt = leaf->count(); 
        for (i = 0; i < cnt; i++)
            sorted_pos[i] = i + 1;
        std::sort(sorted_pos.begin(), sorted_pos.end(), [leaf](int i, int j)
        {
            return compare(leaf->getKey(i), leaf->getLen(i), leaf->getKey(j), leaf->getLen(j)) < 0;
        });
        // Find position of new insert key
        l = 0; r = cnt - 1;
        while (l <= r) {
            mid = (l + r) >> 1;
            if (compare(key, len, getKey(sorted_pos[mid]), getLen(sorted_pos[mid])) <= 0)
                r = mid - 1;
            else
                l = mid + 1;
        }
        // alloc new leaf
        new_leaf = new (allocate_leaf()) Leaf(*leaf); // inherit lock, next ptrs
        alt = leaf->alt();
        leaf->next[1 - alt] = new_leaf; // track new leaf in unused ptr
        // leaf key redistribution
        int half_space = ((LEAF_SIZE - leaf->ent[count()].offset) + len) / 2;
        for (i = cnt - 1; i > r && new_leaf->keySpace() < half_space; i--) // try copy half higher keys to new leaf
        {
            idx = sorted_pos[i];
            l = leaf->getLen(idx);
            offset = new_leaf->appendKey(leaf->getKey(idx), l);
            cnt = count();
            new_leaf->ent[cnt] = leaf->ent[idx];
            new_leaf->ent[cnt].offset = offset;
        }
        if (new_leaf->keySpace() < half_space) // insert key belongs to new leaf
        {
            new_leaf->appendKeyEntry(key, len, val); // insert new key
            while (new_leaf->keySpace() < half_space) // keep inserting into new leaf
            {
                idx = sorted_pos[i];
                l = leaf->getLen(idx);
                offset = new_leaf->appendKey(leaf->getKey(idx), l);
                cnt = count();
                new_leaf->ent[cnt] = leaf->ent[idx];
                new_leaf->ent[cnt].offset = offset;
                i--;
            }
            split_key_len = leaf->getLen(sorted_pos[i]); // set split key
            split_key = leaf->getKey(sorted_pos[i]);
            // split_key = new char[split_key_len];
            // std::memcpy(split_key, leaf->getKey(sorted_pos[i]), split_key_len);
            // consolidate leaf
            leaf->consolidate(sorted_pos, i + 1);
        }
        else // insert key belongs to original leaf
        {
            // if (compare(key, len, leaf->getKey(idx), leaf->getLen(idx)) >= 0) // insert key is split key
            if (r > i) // r is new key insert pos, i is idx of current greatest key in left leaf
            {
                split_key_len = len;
                split_key = key;
                // split_key = new char[split_key_len];
                // std::memcpy(split_key, key, split_key_len);
            }
            else
            {
                idx = sorted_pos[i];
                split_key_len = leaf->getLen(idx);
                split_key = leaf->getKey(idx);
                // split_key = new char[split_key_len];
                // std::memcpy(split_key, leaf->getKey(idx), split_key_len);
            }
            leaf->consolidate(sorted_pos, i + 1);
            leaf->appendKeyEntry(key, len, val);
        }
        // unlock leaf if not root
        new_leaf->unlock();
        if (pos_[0] > 0)
            leaf->unlockFlipAlt(alt);


        // Update inners
        new_child = new_leaf;
        int h = pos_[0];
        for (int level = 1; level <= h; level++)
        {
            current = anc_[level];
            cnt = current->count();
            p = pos_[level] + 1;
            if (current->freeSpace() >= split_key_len) // if last inner to update
            {
                for (i = level + 1; i <= h; i++) // release upper parents not updated
                    anc_[i].unlock();
                key = current->getKey(cnt); // start of last key
                // ToDo: is memcpy safe to use with overlapping addresses
                std::memcpy(key - split_key_len, key, current->ent[p - 1].offset - current->ent[cnt].offset); 
                for (i = cnt; i >= p; i--)
                {
                    current->ent[i + 1] = current->ent[i];
                    current->ent[i + 1].offset -= split_key_len;
                }
                current->insertChild(p, split_key, new_child);
                current->unlock();
                return true;
            }

            new_inner = new (allocate_inner()) Inner(); // else split inner
            half_space = (INNER_SIZE - current->ent[cnt].offset + split_key_len) / 2;
            offset = 0; // simulate left inner key space after split

            // after loop, i is the index that separates original inner into two halves
            for (i = 1; (INNER_SIZE - current->ent[i].offset) < half_space; i++) {}

            if (p < i) // split key belongs to left inner
            {
                for (r = i; r <= cnt; r++) // move all keys with idx >= i to new inner
                {
                    
                }
            }

            for (i = cnt; i > p && offset < half_space; i--) // try copy half higher keys to new inner
            {
                offset += current->getLen(i);

                new_inner->ent[++ new_inner->count()]
                l = leaf->getLen(idx);
                offset = new_leaf->appendKey(leaf->getKey(idx), l);
                new_leaf->ent[cnt] = leaf->ent[idx];
                new_leaf->ent[cnt].offset = offset;
            }

            if (offset < half_space) // insert split key to new inner
            {
                offset += split_key_len;
                for (i; offset < half_space; i--)
                    offset += current->getLen(i);
                for ()
            }


            if (p <= LEFT_KEY_NUM) // insert to left inner
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; r >= 0; r--, i--)
                    new_inner->ent[r] = current->ent[i];
                for (i = LEFT_KEY_NUM - 1; i >= p; i--)
                    current->ent[i + 1] = current->ent[i];
                current->insertChild(p, split_key, new_child);
                split_key = new_inner->ent[0].key;
                new_inner->count() = RIGHT_KEY_NUM;
            }
            else
            {
                for (r = RIGHT_KEY_NUM, i = INNER_KEY_NUM; i >= p; i--, r--)
                    new_inner->ent[r] = current->ent[i];
                new_inner->insertChild(r, split_key, new_child);
                p = r;
                for (--r; r >= 0; r--, i--)
                    new_inner->ent[r] = current->ent[i];
                split_key = new_inner->ent[0].key;
                new_inner->count() = RIGHT_KEY_NUM;
            }
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

#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key, len);
#endif

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

