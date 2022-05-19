#include <cstring>

#define INNER_KEY_NUM 38
#define LEAF_KEY_NUM 64 // <= 64 due to bitmap
#define MAX_HEIGHT 32 // should be enough

// #define PM
#define FINGERPRINT
#define EARLY_SPLIT 2
// #define Binary_Search // slower than linear search
// #define STRING_KEY 

#ifdef STRING_KEY // change length type if necessary
    #define MAX_LENGTH 8
    #ifdef MAX_LENGTH
        int MIN_LEN =  MAX_LENGTH - 6; // mkey is 48 bits
    #endif
    class StringKey {
    public:
        char* key;
        #ifdef MAX_LENGTH
            uint64_t mkey : 48;
            uint64_t length : 16;
        #else
            uint16_t length;
        #endif
	
    	StringKey() { key = nullptr; length = 0; }

    	StringKey(char* k, uint16_t len) 
        {
            key = k; length = len; 
        #ifdef MAX_LENGTH
            mkey = 0;
            if (len > MIN_LEN)
            {
                int bytes = len - MIN_LEN;
                for (int i = 0, j = 6 - bytes; i < bytes; i++, j++)
                    ((char*)&mkey)[j] = k[i];
            }
        #endif
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

        inline bool operator<(const StringKey &other) { return compare(other) < 0; }
        inline bool operator>(const StringKey &other) { return compare(other) > 0; }
        inline bool operator==(const StringKey &other) { return compare(other) == 0; }
        inline bool operator!=(const StringKey &other) { return compare(other) != 0; }
        inline bool operator<=(const StringKey &other) { return compare(other) <= 0; }
        inline bool operator>=(const StringKey &other) { return compare(other) >= 0; }
    };

    typedef StringKey key_type;
#else
    typedef uint64_t key_type; // key type shoud be >= 8 bytes, count is stored in first key of inner
#endif
typedef void* val_type;

inline static uint8_t getOneByteHash(key_type k)
{
#ifdef STRING_KEY
    uint8_t oneByteHashKey = std::_Hash_bytes(k.key, k.length, 1) & 0xff;
#else
    uint8_t oneByteHashKey = std::_Hash_bytes(&k, sizeof(key_type), 1) & 0xff;
#endif
    return oneByteHashKey;
}

/*-----------------------------------------------------------------------------------------------*/

static const uint64_t OFFSET = (uint64_t)(-1) >> (64 - LEAF_KEY_NUM);
static const int MID = LEAF_KEY_NUM / 2;

class Bitmap
{
 public:
    uint64_t bits;

    Bitmap()
    {
        bits = 0;
    }

    ~Bitmap() {}

    Bitmap(const Bitmap& bts)
    {
        bits = bts.bits;
    }

    Bitmap& operator=(const Bitmap& bts)
    {
        bits = bts.bits;
        return *this;
    }

    inline void set(const int pos)
    {
        bits |= ((uint64_t)1 << pos);
    }

    inline void setBits(const uint64_t b)
    {
        bits = b;
    }

    inline void reset(const int pos)
    {
        bits &= ~((uint64_t)1 << pos);
    }

    inline void clear()
    {
        bits = 0;
    }

    inline void clearRightHalf()
    {
        bits ^= (OFFSET >> MID);
    }

    inline bool test(const int pos) const
    {
        return bits & ((uint64_t)1 << pos);
    }

    inline void flip()
    {
        bits ^= OFFSET;
    }

    inline bool is_full()
    {
        return bits == OFFSET;
    }

    inline int count() 
    {
        return __builtin_popcountl(bits);
    }

    inline int first_set()
    {
        int idx = __builtin_ffsl(bits);
        return idx - 1;
    }

    inline int first_zero() 
    {
        int idx = __builtin_ffsl(bits ^ OFFSET);
        return idx - 1;
    }

    void print_bits()
    {
        for (uint64_t i = 0; i < 64; i++) { std::cout << ((bits >> i) & 1); }
        std::cout << std::endl;
    }
};
