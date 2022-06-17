#include <cstring>
#include <algorithm>
#include <cassert>

// #define INNER_KEY_NUM 38
// #define LEAF_KEY_NUM 64 // <= 64 due to bitmap
#define MAX_HEIGHT 32 // should be enough

// #define PM
#define FINGERPRINT
#define SIMD
#define PREFETCH
#define BRANCH_PRED
#define EARLY_SPLIT 2
#define Binary_Search // only faster with STRING_KEY

#define MAX_KEY_LENGTH 2048
#define INNER_SIZE 4096
#define LEAF_SIZE 4096



inline static int compare(char* k1, uint16_t len1, char* k2, uint16_t len2)
{
	int res;
	if (len1 < len2)
	{
		res = std::memcmp(k1, k2, len1);
        return res? res : -1;
	}
	else
    {
        res = std::memcmp(k1, k2, len2);
        return  res? res : len1 == len2? 0 : 1;
    }
}

struct StringKey {
    char* key;
    uint16_t length;

	StringKey() 
	{ 
	    key = nullptr; 
	    length = 0; 
	}

	StringKey(char* k, uint16_t len) 
    {
        key = k; 
        length = len; 
    }

    inline int compare(const StringKey &other) 
    {
        int res;
        if (length < other.length)
        {
            res = std::memcmp(key, other.key, length);
            return res? res : -1;
        }
        else
        {
            res = std::memcmp(key, other.key, other.length);
            return  res? res : length == other.length? 0 : 1;
        }
    }

    inline int compare(const char* k, uint16_t len)
    {
        int res;
        if (length < len) // second key is too short
        {
            res = std::memcmp(key, k, length);
            return res? res : -1;
        }
        else
        {
            res = std::memcmp(key, k, len);
            return  res? res : length == len? 0 : 1;
        }
    }

    

    inline bool operator<(const StringKey &other) { return compare(other) < 0; }
    inline bool operator>(const StringKey &other) { return compare(other) > 0; }
    inline bool operator==(const StringKey &other) { return compare(other) == 0; }
    inline bool operator!=(const StringKey &other) { return compare(other) != 0; }
    inline bool operator<=(const StringKey &other) { return compare(other) <= 0; }
    inline bool operator>=(const StringKey &other) { return compare(other) >= 0; }
};

// typedef StringKey key_type;
typedef void* val_type;

inline static uint8_t getOneByteHash(char* key, int len)
{
	uint8_t oneByteHashKey = std::_Hash_bytes(key, len, 1) & 0xff;
	return oneByteHashKey;
}

/*-----------------------------------------------------------------------------------------------*/

// static const uint64_t OFFSET = (uint64_t)(-1) >> (64 - LEAF_KEY_NUM);
// static const int MID = LEAF_KEY_NUM / 2;

// class Bitmap
// {
//  public:
//     uint64_t bits;

//     Bitmap()
//     {
//         bits = 0;
//     }

//     ~Bitmap() {}

//     Bitmap(const Bitmap& bts)
//     {
//         bits = bts.bits;
//     }

//     Bitmap& operator=(const Bitmap& bts)
//     {
//         bits = bts.bits;
//         return *this;
//     }

//     inline void set(const int pos)
//     {
//         bits |= ((uint64_t)1 << pos);
//     }

//     inline void setBits(const uint64_t b)
//     {
//         bits = b;
//     }

//     inline void reset(const int pos)
//     {
//         bits &= ~((uint64_t)1 << pos);
//     }

//     inline void clear()
//     {
//         bits = 0;
//     }

//     inline void clearRightHalf()
//     {
//         bits ^= (OFFSET >> MID);
//     }

//     inline bool test(const int pos) const
//     {
//         return bits & ((uint64_t)1 << pos);
//     }

//     inline void flip()
//     {
//         bits ^= OFFSET;
//     }

//     inline bool is_full()
//     {
//         return bits == OFFSET;
//     }

//     inline int count() 
//     {
//         return __builtin_popcountl(bits);
//     }

//     inline int first_set()
//     {
//         int idx = __builtin_ffsl(bits);
//         return idx - 1;
//     }

//     inline int first_zero() 
//     {
//         int idx = __builtin_ffsl(bits ^ OFFSET);
//         return idx - 1;
//     }

//     void print_bits()
//     {
//         for (uint64_t i = 0; i < 64; i++) { std::cout << ((bits >> i) & 1); }
//         std::cout << std::endl;
//     }
// };
