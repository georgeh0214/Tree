extern static const uint64_t offset;

class Bitmap
{
 public:
    uint64_t bits;


    Bitset()
    {
        bits = 0;
    }

    ~Bitset() {}

    Bitset(const Bitset& bts)
    {
        bits = bts.bits;
    }

    Bitset& operator=(const Bitset& bts)
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

    inline void flip()
    {
        bits ^= offset;
    }

    inline bool is_full()
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

    inline int first_zero() 
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