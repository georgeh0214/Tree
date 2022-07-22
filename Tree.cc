#include "Tree.h"

int Tree::compare(const char* k1, const char* k2)
{
    return compareKey(k1, k2, meta.key_len);
}

Node* Tree::findInnerChild(Node* inner, const char* key, short* pos)
{
    int r;
    if (meta.binary_search)
    {
        int l = 1, mid; r = getInnerCount(inner);
        while (l <= r) {
            mid = (l + r) >> 1;
            if (compare(key, getInnerKey(inner, mid)) <= 0)
                r = mid - 1;
            else
                l = mid + 1;
        }
    }
    else
    {
        int cnt = getInnerCount(inner);
        for (r = 1; r <= cnt; r++)
            if (compare(key, getInnerKey(inner, r)) <= 0)
                break;
        r--;
    }
    *pos = r;
    return getInnerChild(inner, r);
}

char* Tree::searchLeaf(Node* leaf, const char* key)
{
    if (meta.enable_fp && meta.enable_simd)
    {
        char* cur_key;
        int i;
        // a. set every byte to key_hash in a 64B register
        __m512i key_64B = _mm512_set1_epi8((char)key_hash_);

        // b. load fp into another 64B register
        __m512i fgpt_64B = _mm512_loadu_si512(&getLeafFP(leaf, 0)); // _mm512_loadu_si512, _mm512_load_si512 (64 align)

        // c. compare them
        __mmask64 mask = _mm512_cmpeq_epu8_mask(key_64B, fgpt_64B);

        mask &= offset; // in case meta.leaf_key_num < 64
        mask &= getLeafBitmap(leaf)->bits; // only check existing entries
        for (i = 0; mask; i++, mask >>= 1)
        {
            cur_key = getLeafKey(leaf, i);
            if ((mask & 1) && compare(key, cur_key) == 0)
                return cur_key + meta.value_len;
        }
    }
    else
    {
        uint64_t bits = getLeafBitmap(leaf)->bits;
        char* cur_key = getLeafKey(leaf, 0);
        uint32_t leaf_ent_size = meta.key_len + meta.value_len;
        int i;
        for (i = 0; bits != 0; i++, bits = bits >> 1, cur_key += leaf_ent_size) 
        {
            if (meta.enable_fp)
                if ((bits & 1) && key_hash_ == getLeafFP(leaf, i) && compare(key, cur_key) == 0)
                    return cur_key + meta.value_len;
            else
                if ((bits & 1) && compare(key, cur_key) == 0)
                    return cur_key + meta.value_len;
        }
    }
    return nullptr;
}

void Tree::insertLeafEntry(Node* leaf, const char* key, char* val)
{
    char* insert_key_pos;
    Bitmap* bmp = getLeafBitmap(leaf);
    int i = bmp->first_zero(offset);

    if (meta.enable_fp)
    {
        getLeafFP(leaf, i) = key_hash_;
        if (i >= 48) // if not on the first cache line with bitmap
            clwb(&getLeafFP(leaf, i), sizeof(key_hash_));
    }
    insert_key_pos = getLeafKey(leaf, i);
    std::memcpy(insert_key_pos, key, meta.key_len);
    std::memcpy(insert_key_pos + meta.key_len, val, meta.value_len);
    clwb(insert_key_pos, meta.key_len + meta.value_len);
    sfence();

    bmp->set(i);
    clwb(leaf, 64); // flush first cacheline (Bitmap)
    sfence();
}

Node* Tree::findLeaf(const char* key, uint64_t& version, bool lock)
{
    uint64_t currentVersion, previousVersion;
    Node* current, * previous;
    int i;
    short pos;

RetryFindLeaf: 
    current = this->root;
    if (current->isLocked(currentVersion) || current != this->root)
        goto RetryFindLeaf;
    for (i = this->height; i > 0; i--)
    {
        previous = current;
        previousVersion = currentVersion;
        current = findInnerChild(current, key, &pos);
        if (current->isLocked(currentVersion) || !previous->checkVersion(previousVersion))
            goto RetryFindLeaf;
    }
    if (lock && !current->upgradeToWriter(currentVersion))
        goto RetryFindLeaf;
    version = currentVersion;
    return current;
}

Node* Tree::findLeafAssumeSplit(const char* key)
{
    Node* current, * previous;
    uint64_t currentVersion, previousVersion;
    int i;

RetryFindLeafAssumeSplit:
    current = this->root;
    if (current->isLocked(currentVersion) || current != this->root)
        goto RetryFindLeafAssumeSplit;
    pos_[0] = this->height;
    for (i = pos_[0]; i > 0; i--)
    {
        anc_[i] = current;
        versions_[i] = currentVersion;
        previous = current;
        previousVersion = currentVersion;
        if (!isInnerFull(current))
            pos_[0] = i;
        current = findInnerChild(current, key, &pos_[i]);
        if (current->isLocked(currentVersion) || !previous->checkVersion(previousVersion))
            goto RetryFindLeafAssumeSplit;
    }
    if (searchLeaf(current, key) != nullptr) // key already exists
    {
        if (current->checkVersion(currentVersion))
            return nullptr;
        else
            goto RetryFindLeafAssumeSplit;
    }
    if (!current->upgradeToWriter(currentVersion)) // failed to acquire lock
        goto RetryFindLeafAssumeSplit;
    return current;
}

bool Tree::lockStack(Node* n) // ToDo: Is only locking top most ancester enough
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

bool Tree::lookup(const char* key, char* val)
{
    uint64_t version;
    Node* leaf;
    char* value_ptr;

    initOp(key); // set fp if necessary

RetryLookup:
    leaf = findLeaf(key, version, false);
    value_ptr = searchLeaf(leaf, key);
    if (value_ptr)
        std::memcpy(val, value_ptr, meta.value_len);
    if (!leaf->checkVersion(version))
        goto RetryLookup;
    return value_ptr != nullptr;
}

bool Tree::update(const char* key, char* new_val)
{
    uint64_t version;
    Node* leaf;
    char* value_ptr;

    initOp(key); // set fp if necessary

RetryUpdate:
    leaf = findLeaf(key, version, true);
    value_ptr = searchLeaf(leaf, key);
    if (value_ptr)
    {
        std::memcpy(value_ptr, new_val, meta.value_len);
        clwb(value_ptr, meta.value_len);
        sfence();
    }
    leaf->unlock();
    return value_ptr != nullptr;
}

bool Tree::insert(const char* key, char* val) 
{
    Node* current, * leaf;
    initOp(key); // set fp if necessary

RetryInsert: 
    leaf = findLeafAssumeSplit(key);
    if (!leaf) return false; // key already exists
    if (getLeafCount(leaf) < meta.leaf_key_num) // no split insert
    {
        insertLeafEntry(leaf, key, val);
        leaf->unlock();
        return true;
    }
    else // split insert
    {
        Node* new_node;
        char* start_key_pos;
        int i, r, count, left_key_num, right_key_num;
        uint32_t leaf_ent_size;
        short p;
        if (!lockStack(leaf))
            goto RetryInsert;
        // sort indexes of leaf entries, find middle split key
        thread_local std::vector<int> sorted_pos(meta.leaf_key_num);
        for (i = 0; i < meta.leaf_key_num; i++)
            sorted_pos[i] = i;

        // struct less_than_key
        // {
        //     char* first_key_pos;
        //     uint32_t leaf_ent_size;
        //     uint32_t key_len;

        //     less_than_key(char* fkp, uint32_t les, uint32_t kl)
        //     {
        //         first_key_pos = fkp;
        //         leaf_ent_size = les;
        //         key_len = kl;
        //     }     

        //     inline bool operator() (int i, int j)
        //     {
        //         return compareKey(first_key_pos + i * leaf_ent_size, first_key_pos + j * leaf_ent_size, key_len) < 0;
        //     }
        // };
        leaf_ent_size = meta.key_len + meta.value_len;
        // std::sort(sorted_pos.begin(), sorted_pos.end(), less_than_key(getLeafKey(leaf, 0), leaf_ent_size, meta.key_len));

        //debug
        char* first_key_pos = getLeafKey(leaf, 0);
        uint32_t key_len = meta.key_len;
        std::sort(sorted_pos.begin(), sorted_pos.end(), [first_key_pos, leaf_ent_size, key_len](int i, int j) {
            return compareKey(first_key_pos + i * leaf_ent_size, first_key_pos + j * leaf_ent_size, key_len) < 0;
        });

        // debug 
        for (i = 1; i < 64; i++)
        {
            if (compare(getLeafKey(leaf, i-1), getLeafKey(leaf, i)) >= 0)
            {
                printf("Wrong order after sort!\n");
            }
        }

        int split_pos = meta.leaf_key_num / 2;
        char* split_key = getLeafKey(leaf, split_pos);
        bool alt = leaf->alt();

        // alloc new leaf
        if (meta.pop) // PM.
        {
            void* next_ptr_addr = getNextLeafPtrAddr(leaf, sizeof(PMEMoid) * (alt + 1));
            new_node = alloc_leaf((PMEMoid*)next_ptr_addr);
        }
        else
        {
            new_node = alloc_leaf();
            *(Node**)getNextLeafPtrAddr(leaf, sizeof(Node*) * (alt + 1)) = new_node;
        }
        std::memcpy(new_node, leaf, meta.leaf_size);

        // set bitmap of both leaves
        Bitmap* bmp = getLeafBitmap(new_node);
        for (i = 0; i <= split_pos; i++)
            bmp->reset(sorted_pos[i]);
        clwb(new_node, meta.leaf_size);
        uint64_t bits = bmp->bits;
        bmp = getLeafBitmap(leaf);
        bmp->setBits(bits);
        bmp->flip(offset);
        clwb(bmp, sizeof(Bitmap));
        sfence();

        // insert new entry, unlock leaf if not root
        current = new_node;
        if (compare(key, split_key) <= 0)
            current = leaf;
        insertLeafEntry(current, key, val);
        new_node->unlock();
        if (pos_[0] > 0)
            leaf->unlockFlipAlt(alt);

        // debug
        bmp = getLeafBitmap(leaf);
        printf("Leaf has: %d records\n", bmp->count());
        for (int i = 0; i < 64; i++)
        {
            if (bmp->test(i) && compare(getLeafKey(leaf, i), split_key) > 0)
            {
                printf("Wrong key at leaf!\n");
            }
        }

        bmp = getLeafBitmap(new_node);
        printf("New node has: %d records\n", bmp->count());
        for (int i = 0; i < 64; i++)
        {
            if (bmp->test(i) && compare(getLeafKey(new_node, i), split_key) <= 0)
            {
                printf("Wrong key at new node!\n");
            }
        }


        // Update inners
        Node* new_inner;
        int h = pos_[0];
        left_key_num = meta.inner_key_num / 2;
        right_key_num = meta.inner_key_num - left_key_num;
        for (int level = 1; level <= h; level++)
        {
            current = anc_[level];
            count = getInnerCount(current);
            p = pos_[level] + 1; // insert pos
            if (count < meta.inner_key_num) // if last inner to update
            {
                // if (p <= count)
                // {
                    start_key_pos = getInnerKey(current, p);
                    std::memmove(start_key_pos + leaf_ent_size, start_key_pos, leaf_ent_size * (count - p + 1));
                // }
                insertInnerEntry(current, split_key, new_node, p);
                getInnerCount(current) ++;
                current->unlock();
                return true;
            }

            new_inner = alloc_inner(); // split inner
            
            if (p <= left_key_num) // insert to left inner
            {
                std::memcpy(getInnerKey(new_inner, 0), getInnerKey(current, left_key_num + 1), leaf_ent_size * (right_key_num));
                start_key_pos = getInnerKey(current, p);
                std::memmove(start_key_pos + leaf_ent_size, start_key_pos, leaf_ent_size * (left_key_num - p + 1));
                insertInnerEntry(current, split_key, new_node, p);
                getInnerCount(current) = left_key_num + 1;
                getInnerCount(new_inner) = right_key_num - 1;

                // for (r = right_key_num, i = INNER_KEY_NUM; r >= 0; r--, i--)
                //     new_inner->ent[r] = current->ent[i];
                // for (i = left_key_num - 1; i >= p; i--)
                //     current->ent[i + 1] = current->ent[i];
                // current->insertChild(p, split_key, new_node);
                // split_key = new_inner->ent[0].key;
                // new_inner->count() = right_key_num;
            }
            else
            {
                std::memcpy(getInnerKey(new_inner, p - left_key_num), getInnerKey(current, p), leaf_ent_size * (count - p + 1));
                insertInnerEntry(new_inner, split_key, new_node, p - left_key_num - 1);
                std::memcpy(getInnerKey(new_inner, 0), getInnerKey(current, left_key_num + 1), leaf_ent_size * (p - left_key_num - 1));
                getInnerCount(current) = left_key_num;
                getInnerCount(new_inner) = right_key_num;
                
                // for (r = right_key_num, i = INNER_KEY_NUM; i >= p; i--, r--)
                //     new_inner->ent[r] = current->ent[i];
                // new_inner->insertChild(r, split_key, new_node);
                // p = r;
                // for (--r; r >= 0; r--, i--)
                //     new_inner->ent[r] = current->ent[i];
                // split_key = new_inner->ent[0].key;
                // new_inner->count() = right_key_num;
            }
            split_key = getInnerKey(new_inner, 0);
            new_node = new_inner;
            if (level < h) // do not unlock root
                current->unlock();
        }

        // new root
        new_inner = alloc_inner();
        while (!new_inner->lock()) {}
        getInnerCount(new_inner) = 1;
        *(Node**)(getInnerKey(new_inner, 0) + meta.key_len) = this->root;
        // start_key_pos = getInnerChild(new_inner, 0);
        // *(Node**)start_key_pos = this->root;
        insertInnerEntry(new_inner, split_key, new_node, 1);

        // new_inner->count() = 1;
        // new_inner->ent[0].child = this->root;
        // new_inner->insertChild(1, split_key, new_node);

        current = this->root;
        this->root = new_inner;
        if (++(this->height) > MAX_HEIGHT)
            printf("Stack overflow!\n");
        new_inner->unlock();
        if (this->height > 1) // previous root is a nonleaf
            current->unlock();
        else // previous root is a leaf
            current->unlockFlipAlt(alt);
        return true;
    }
}

// void Tree::rangeScan(key_type start_key, ScanHelper& sh)
// {
//     Leaf* leaf, * next_leaf;
//     uint64_t leaf_version, next_version, bits;
//     int i;
//     // std::vector<Leaf*> leaves; // ToDo: is phantom allowed?

//     initOp(start_key);

// RetryScan:
//     sh.reset();
//     leaf = findLeaf(start_key, leaf_version, false);
//     bits = leaf->bitmap.bits;
//     for (int i = 0; bits != 0; i++, bits = bits >> 1)
//     #if defined(PM) && defined(STRING_KEY)
//         if ((bits & 1) && compare((char*)pmemobj_direct(leaf->ent[i].key), start_key.key, leaf->ent[i].len, start_key.length) >= 0)
//     #else
//         if ((bits & 1) && leaf->ent[i].key >= start_key) // compare with key in first leaf
//     #endif
//             sh.scanEntry(leaf->ent[i]);

//     while(!sh.stop())
//     {
//         next_leaf = leaf->sibling();
//         if (!next_leaf) // end of leaves
//         {
//             if (!leaf->checkVersion(leaf_version))
//                 goto RetryScan;
//             return;
//         }
//         if (next_leaf->isLocked(next_version) || !leaf->checkVersion(leaf_version))
//             goto RetryScan;
//         leaf = next_leaf;
//         leaf_version = next_version;
//         bits = leaf->bitmap.bits;
//         for (i = 0; bits != 0; i++, bits = bits >> 1) 
//             if (bits & 1) // entry found
//                 sh.scanEntry(leaf->ent[i]);
//     }
// }

