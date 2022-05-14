// #define PM
// #define Binary_Search

#define INNER_KEY_NUM 14
#define LEAF_KEY_NUM 14 // <= 64 for now, recommand 14/30/46/62
#define MAX_HEIGHT 32 // should be enough

typedef uint64_t key_type; // >= 8 bytes
typedef void* val_type;

inline static uint8_t getOneByteHash(key_type key)
{
    uint8_t oneByteHashKey = std::_Hash_bytes(&key, sizeof(key_type), 1) & 0xff;
    return oneByteHashKey;
}

static const uint64_t OFFSET = (uint64_t)(-1) >> (64 - LEAF_KEY_NUM);
static const int MID = LEAF_KEY_NUM / 2;

#define EARLY_SPLIT 2

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
