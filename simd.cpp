#include <iostream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cassert>
#include <fstream>
#include <sys/stat.h>

#include <immintrin.h>

using namespace std;

struct Node
{
    uint8_t fp[64];
};

int main()
{
	Node* n = new Node;
    std::memset(n, 0, sizeof(Node));

    for (int i = 0; i < 64; i++)
        assert(n->fp[i] == 0);

    n->fp[0] = 1;

    uint8_t key_hash_ = 1;

    __m512i key_64B = _mm512_set1_epi8((char)key_hash_);

    __m512i fgpt_64B = _mm512_loadu_si512((void const *)n->fp); // _mm512_loadu_si512, _mm512_load_si512 (64 align)

    __mmask64 mask = _mm512_cmpeq_epu8_mask(key_64B, fgpt_64B);

    

    return 0;
}