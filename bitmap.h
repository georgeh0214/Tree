#include <cstring>
#include <algorithm>
#include <cassert>

#define MAX_HEIGHT 32 // should be enough

// #define PM
#define FINGERPRINT
#define SIMD
#define PREFETCH
#define BRANCH_PRED
#define Binary_Search

#define MAX_KEY_LENGTH 8
#define INNER_SIZE 256
#define LEAF_SIZE 256

typedef void* val_type;

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

inline static uint8_t getOneByteHash(char* key, int len)
{
	uint8_t oneByteHashKey = std::_Hash_bytes(key, len, 1) & 0xff;
	return oneByteHashKey;
}