CC=g++
# CC=clang++

# Flag for debugging runs
# CFLAGS=-O0 -g -std=c++11 -pthread -mrtm -msse4.1 -mavx2

# Flag for test runs
override CFLAGS += -g -std=c++17 -pthread -O3 # -mrtm -msse4.1 -mavx2 -Wall -O3 # -ljemalloc

#CFLAGS=-g -std=c++17 -pthread -msse4.1 -mavx2 -Wall -O3 -fsanitize=undefined,implicit-conversion,nullability,float-divide-by-zero,unsigned-integer-overflow,bounds,bool

# INCLUDE=-I./common
LIB=-lpmemobj

COMMON_DEPENDS= tree_api.hpp tree.h tree.cc 
COMMON_SOURCES= tree.cc

# -----------------------------------------------------------------------------
TARGETS=main tree_wrapper

all: ${TARGETS}

# -----------------------------------------------------------------------------
main: main.cpp tree.cc
	${CC} -mavx512f -mavx512bw main.cpp tree.cc ${CFLAGS}

tree_wrapper: tree_wrapper.hpp tree_wrapper.cpp ${COMMON_DEPENDS}
	${CC} -o libtree_wrapper.so ${CFLAGS} -mavx512f -mavx512bw -ljemalloc -fPIC -shared tree_wrapper.cpp ${COMMON_SOURCES} ${LIB}
# -----------------------------------------------------------------------------
clean:
	-rm -rf a.out core *.s ${TARGETS} libtree_wrapper.so
