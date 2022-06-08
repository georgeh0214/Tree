#include <iostream>
#include <thread>
#include <chrono>
#include <immintrin.h>
#include <random>
#include "tree.h"

using namespace std;

#define N 1000000
#define MAX_LENGTH 26

char* keys[N];
short lens[N];

void generateStringKey()
{
	short len;
	for (int i = 0; i < N; i++)
	{
		len = rand() % MAX_LENGTH + 1;
		keys[i] = new char[len];
		for (int j = 0; j < len; j++)
			keys[i][j] = rand() % 256;
		lens[i] = len;
	}
}

int main(int argc, char const *argv[])
{
	srand(time(NULL));
	tree t_;
	generateStringKey();
	key_type k;
	val_type v = nullptr;
	for (int i = 0; i < N; i++)
	{
		key_type k(keys[i], lens[i]);
		t_.insert(k, v);
	}
	
	for (int i = 0; i < N; i++)
	{
		k = key_type(keys[i], lens[i]);
		if (!t_.lookup(k, v))
		{
			printf("Missing key!\n");
			exit(1);
		}
	}
	for (int i = 0; i < N; i++)
	{
		k = key_type(keys[i], lens[i]);
		if (!t_.update(k, nullptr))
		{
			printf("Update failed!\n");
			exit(1);
		}
	}
	return 0;
}
