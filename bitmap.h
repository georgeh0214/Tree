#include <cstring>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <sys/stat.h>
#include <cassert>

#define MAX_HEIGHT 32 // should be enough

#define NEW_PERSIST // asm volatile

#define PREFETCH // not effective

inline static uint8_t getOneByteHash(const char* k, uint32_t len)
{
    return std::_Hash_bytes(k, len, 1) & 0xff;
}

inline static int compare(const char* k1, const char* k2, uint32_t len)
{
    // Method 1: memcmp
    // return std::memcmp(k1, k2, len);

    // Method 2: compare each char
    // for (int i = 0; i < len; i++)
    // if (*k1++ != *k2++)
//          return k1[-1] < k2[-1] ? -1 : 1;
    // return 0;

    // Method 3: compare 8 bytes at a time
    int i = len - 8;
    while (i >= 0)
    {
        if (*(uint64_t*)k1 != *(uint64_t*)k2)
            return *(uint64_t*)k1 > *(uint64_t*)k2 ? -1 : 1;
        k1 += 8;
        k2 += 8;
        i -= 8;
    }
    for (i = i + 8; i; i--)
        if (*k1++ != *k2++)
            return k1[-1] < k2[-1] ? -1 : 1;
    return 0;

}

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

    inline bool test(const int pos) const
    {
        return bits & ((uint64_t)1 << pos);
    }

    inline void flip(uint64_t offset)
    {
        bits ^= offset;
    }

    inline bool is_full(uint64_t offset)
    {
        return bits == offset;
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

    inline int first_zero(uint64_t offset) 
    {
        int idx = __builtin_ffsl(bits ^ offset);
        return idx - 1;
    }

    void print_bits()
    {
        for (uint64_t i = 0; i < 64; i++) { std::cout << ((bits >> i) & 1); }
        std::cout << std::endl;
    }
};

// struct B256 {
//     void* dummy[32];
// } __attribute__((aligned(256)));

// struct B512 {
//     void* dummy[64];
// } __attribute__((aligned(256)));

// struct B768 {
//     void* dummy[96];
// } __attribute__((aligned(256)));

// struct B1024 {
//     void* dummy[128];
// } __attribute__((aligned(256)));

// struct B1280 {
//     void* dummy[160];
// } __attribute__((aligned(256)));

// struct B1536 {
//     void* dummy[192];
// } __attribute__((aligned(256)));

// struct B1792 {
//     void* dummy[224];
// } __attribute__((aligned(256)));

// struct B2048 {
//     void* dummy[256];
// } __attribute__((aligned(256)));

// struct B2304 {
//     void* dummy[288];
// } __attribute__((aligned(256)));

// struct B2560 {
//     void* dummy[320];
// } __attribute__((aligned(256)));

// struct B2816 {
//     void* dummy[352];
// } __attribute__((aligned(256)));

// struct B3072 {
//     void* dummy[384];
// } __attribute__((aligned(256)));

// struct B3328 {
//     void* dummy[416];
// } __attribute__((aligned(256)));

// struct B3584 {
//     void* dummy[448];
// } __attribute__((aligned(256)));

// struct B3840 {
//     void* dummy[480];
// } __attribute__((aligned(256)));

// struct B4096 {
//     void* dummy[512];
// } __attribute__((aligned(256)));