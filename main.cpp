#include <iostream>
#include <thread>
#include <chrono>
#include <immintrin.h>
#include <random>
#include "tree.h"

using namespace std;

#define N 100000000
#define MAX_LENGTH 8

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
        val_type v = nullptr;
        for (int i = 0; i < N; i++)
        {
//                printf("Insert Key: %llu \n", *(uint64_t*)keys[i]);
                t_.insert(keys[i], lens[i], v);
        }

int missing = 0;
        for (int i = 0; i < N; i++)
        {
                if (!t_.lookup(keys[i], lens[i], v))
                {
                        printf("Missing key: %llu \n", *(uint64_t*)keys[i]);
                //      exit(1);
                        missing++;
                }
        }
        if (missing)
            printf("# missing keys: %d \n", missing);
        else
            printf("Load verified!\n");
        // for (int i = 0; i < N; i++)
        // {
        //      if (!t_.update(keys[i], lens[i], v))
        //      {
        //              printf("Missing key!\n");
        //              exit(1);
        //      }
        // }
/*        Inner* root = (Inner*)t_.root;
        Leaf* left = (Leaf*)root->ent[0].child;
        Leaf* right = (Leaf*)root->ent[1].child;
        printf("Left child cnt: %d \n", left->count());
        for (int i = 1; i <= left->count(); i++)
             printf("Left Key: %llu \n", *(uint64_t*)left->getKey(i));

        printf("Right child cnt: %d \n", right->count());
        for (int i = 1; i <= right->count(); i++)
             printf("Right Key: %llu \n", *(uint64_t*)right->getKey(i));
*/
        return 0;
}
