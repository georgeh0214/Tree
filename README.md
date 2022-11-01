# Tree
A volatile/persistent B+-Tree that supports both integer and string keys

Implemented adaptive prefix optimization for string keys. The idea is to adaptively change the short, in-place prefix as new keys are inserted (or after node split), such that no two keys share the same prefix in the same inner node.

# Some Recommanded Settings
If key length <= 8 (integer keys) and running in DRAM: \
INNER_KEY_NUM 38 \
LEAF_KEY_NUM 64 \
FINGERPRINT \
SIMD \

If key length <= 8 (integer keys) and running in PM: \
INNER_KEY_NUM 38 \
LEAF_KEY_NUM 13 \
PM \
ALIGNED_ALLOC \

If key length > 8 (string keys) and running in DRAM: \
INNER_KEY_NUM 64 \
LEAF_KEY_NUM 64 \
FINGERPRINT \
SIMD \
Binary_Search \
STRING_KEY \
PREFIX  \
ADAPTIVE_PREFIX (optional, depending on key distribution and length) \

If key length > 8 (string keys) and running in PM: \
INNER_KEY_NUM 17 \
LEAF_KEY_NUM 64 \
PM \
ALIGNED_ALLOC \
FINGERPRINT \
SIMD \
Binary_Search \
STRING_KEY \
PREFIX  \
ADAPTIVE_PREFIX (optional, depending on key distribution and length) \
