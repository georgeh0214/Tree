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

int Inner::halfIndex()
{
    int l = 1, mid, r = this->count(), half_space = this->keySpace() / 2;
    while (l <= r) {
        mid = (l + r) >> 1;
        if (half_space > (INNER_SIZE - ent[mid].offset))
            r = mid - 1;
        else
            l = mid + 1;
    }
    assert(r > 0 && r <= count());
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

void Leaf::appendKey(char* key, int len, val_type val)
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

void Leaf::appendKeyEntry(char* key, int len, LeafEntry entry)
{
    int cnt = ++ count();
    this->ent[cnt] = entry;
    this->ent[cnt].offset = this->ent[cnt - 1].offset - len;
    memcpy(getKey(cnt), key, len);
    this->meta_size += sizeof(LeafEntry);
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
        if (idx != i + 1)
            appendKeyEntry(getKey(idx), getLen(idx), ent[idx]); // ToDo: is memcpy safe to use with overlapping addresses
        else
            count() ++;
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
        leaf->appendKey(key, len, val);
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
        // Find position of new insert key (r)
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
        half_space = leaf->keySpace() / 2;
        offset = 0;
        for (i = cnt - 1; offset < half_space; i--) // find greater half keys
        {
            offset += leaf->getLen(sorted_pos[i]);
        }
        for (l = i + 1; l < cnt; l++) // copy those keys to new leaf in ascending order, may speed up sorting in future split?
        {
            idx = sorted_pos[l];
            new_leaf->appendKeyEntry(leaf->getKey(idx), leaf->getLen(idx), leaf->ent[idx]);
        }
        if (r > i) // if insert key belongs to new leaf
        {
            new_leaf->appendKey(key, len, val);
            split_key_len = leaf->getLen(sorted_pos[i]);
            std::memcpy(split_key_, leaf->getKey(sorted_pos[i]), split_key_len);
            leaf->consolidate(sorted_pos, i + 1);
        }
        else
        {
            if (r == i)
            {
                split_key_len = len;
                std::memcpy(split_key_, key, split_key_len);
            }
            else
            {
                idx = sorted_pos[i];
                split_key_len = leaf->getLen(idx);
                std::memcpy(split_key_, leaf->getKey(idx), split_key_len);
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
                for (i = level + 1; i <= h; i++) // release upper parents that will not be updated
                    anc_[i].unlock();
                key = current->getKey(cnt); // start of last key
                // ToDo: is memcpy safe to use with overlapping addresses
                std::memcpy(key - split_key_len, key, current->ent[p - 1].offset - current->ent[cnt].offset); 
                for (i = cnt; i >= p; i--)
                {
                    current->ent[i + 1] = current->ent[i];
                    current->ent[i + 1].offset -= split_key_len;
                }
                current->insertChild(p, split_key_, split_key_len, new_child);
                current->unlock();
                return true;
            }

            new_inner = new (allocate_inner()) Inner(); // else split inner
            i = current->halfIndex(); // i is the index that separates original inner into two halves

            if (p < i) // split key belongs to left inner
            {
                // move all keys & entries with idx >= i to new inner
                l = current->ent[i - 1].offset - current->ent[cnt].offset;
                std::memcpy(((char*)new_inner) + INNER_SIZE - l, current->getKey(cnt), l);
                offset = INNER_SIZE - current->ent[i - 1].offset;
                for (l = i, r = 1; l <= cnt; l++, r++)
                {
                    new_inner->ent[r] = current->ent[l];
                    new_inner->ent[r].offset += offset;
                }
                new_inner->count() = cnt - i + 1;
                new_inner->updateMeta();
                
                current->count() = cnt = --i;
                key = current->getKey(cnt); // start of last key
                // ToDo: is memcpy safe to use with overlapping addresses
                std::memcpy(key - split_key_len, key, current->ent[p - 1].offset - current->ent[cnt].offset);
                for (i; i >= p; i--)
                {
                    current->ent[i + 1] = current->ent[i];
                    current->ent[i + 1].offset -= split_key_len;
                }
                current->updateMeta();
                current->insertChild(p, split_key_, split_key_len, new_child);
                
                cnt ++;
                new_inner->ent[0].child = current->ent[cnt].child;
                
                split_key_len = current->getLen(cnt);
                std::memcpy(split_key_, current->getKey(cnt), split_key_len);
                current->count() --;
                current->meta_size -= sizeof(InnerEntry);
            }
            else // split key belongs to new inner
            {
                // copy keys from index i ~ p-1 to new inner
                l = current->ent[i - 1].offset - current->ent[p - 1].offset;
                std::memcpy(((char*)new_inner) + INNER_SIZE - l, current->getKey(p - 1), l);
                offset = INNER_SIZE - current->ent[i - 1].offset;
                for (l = i, r = 1; l < p; l++, r++)
                {
                    new_inner->ent[r] = current->ent[l];
                    new_inner->ent[r].offset += offset;
                }
                new_inner->count() = p - i;
                new_inner->updateMeta();
                new_inner->insertChild(new_inner->count() + 1, split_key_, split_key_len, new_child); // insert new key
                // copy keys from index p ~ cnt to new inner
                l = current->ent[p - 1].offset - current->ent[cnt].offset;
                std::memcpy(new_inner->getKey(new_inner->count()) - l, current->getKey(cnt), l);
                offset -= split_key_len;
                for (l = p, r = new_inner->count() + 1; l <= cnt; l++, r++)
                {
                    new_inner->ent[r] = current->ent[l];
                    new_inner->ent[r].offset += offset;
                }
                new_inner->count() = cnt - i + 2;
                new_inner->updateMeta();
                i--;
                new_inner->ent[0].child = current->ent[i].child;

                split_key_len = current->getLen(i);
                std::memcpy(split_key_, current->getKey(i), split_key_len);
                current->count() = i - 1;
                current->updateMeta();
            }
            new_child = new_inner;
            if (level < h) // do not unlock root
                current->unlock();
        }

        // new root
        new_inner = new (allocate_inner()) Inner();
        while (!new_inner->lock()) {}
        new_inner->ent[0].child = this->root;
        new_inner->insertChild(1, split_key, split_key_len, new_child);

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

void tree::rangeScan(key_type start_key, int len, ScanHelper& sh)
{
    Leaf* leaf, * next_leaf;
    uint64_t leaf_version, next_version, bits;
    int i;
    // std::vector<Leaf*> leaves; // ToDo: is phantom allowed?

#ifdef FINGERPRINT
    key_hash_ = getOneByteHash(key, len);
#endif

// RetryScan:
//     sh.reset();
//     leaf = findLeaf(start_key, len, leaf_version, false);
//     bits = leaf->bitmap.bits;
//     for (int i = 1; bits != 0; i++, bits = bits >> 1)
//         if ((bits & 1) && compare(leaf->getKey(i)) >= 0 leaf->ent[i].key >= start_key) // compare with key in first leaf
//             sh.scanEntry(leaf->ent[i]);
//     next_leaf = leaf->sibling();
//     if (!next_leaf)
//     {
// 	if (!leaf->checkVersion(leaf_version))
// 	    goto RetryScan;
// 	return;
//     }
//     if (next_leaf->isLocked(next_version) || !leaf->checkVersion(leaf_version))
//         goto RetryScan;
//     leaf = next_leaf;
//     leaf_version = next_version;
//     do {
//         bits = leaf->bitmap.bits;
//         for (int i = 0; bits != 0; i++, bits = bits >> 1) 
//             if (bits & 1) // entry found
//                 sh.scanEntry(leaf->ent[i]);
//         next_leaf = leaf->sibling();
// 	if (!next_leaf)
//         { 
//             if (!leaf->checkVersion(leaf_version))
//                 goto RetryScan;
//             return;
//         }
//         if (next_leaf->isLocked(next_version) || !leaf->checkVersion(leaf_version))
//             goto RetryScan;
//         leaf = next_leaf;
//         leaf_version = next_version;
//     } while (!sh.stop());
}

