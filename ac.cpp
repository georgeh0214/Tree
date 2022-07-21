#include <iostream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cassert>
#include <fstream>
#include <sys/stat.h>

#include <libpmemobj.h>

using namespace std;

#define SIZE 1073741824 // PMEMOBJ_MIN_POOL // 8388608
#define PATH "./pool"


struct N256
{
    void* dummy[32];
} __attribute__((aligned(256)));

void* alloc_size(PMEMobjpool* pop, PMEMoid* oid, uint32_t size, uint16_t class_id) {
    assert(pop && "alloc_size: invalid pool");
    thread_local pobj_action act;
    *oid = pmemobj_xreserve(pop, &act, size, 0, POBJ_CLASS_ID(class_id));
    void* ret = pmemobj_direct(*oid);
    assert(ret && "alloc_size: failed");
    return ret;
}

int main()
{
	PMEMobjpool * pop;
    struct stat buffer;
    if (stat(PATH, &buffer) == 0)
    {
    	printf("Pool exists!\n");
        exit(1);
    }
    else
    {
    	printf("Creating pool!\n");
        POBJ_LAYOUT_BEGIN(Test);
        // POBJ_LAYOUT_TOID(Test, char);
        POBJ_LAYOUT_END(Test);
    	pop = pmemobj_create(PATH, POBJ_LAYOUT_NAME(Test), SIZE, 0666);
	    assert(pop && "pmemobj_create");

        pobj_alloc_class_desc arg;
        arg.unit_size = sizeof(N256);
        arg.alignment = 256;
        arg.units_per_block = 4096;
        arg.header_type = POBJ_HEADER_NONE;
        if (pmemobj_ctl_set(pop, "heap.alloc_class.new.desc", &arg) != 0)
        {
            printf("Allocation class N256 initialization failed!\n");
            exit(1);
        }
        uint16_t class_id = arg.class_id;
        printf("Allocation class id for N256: %d\n", class_id);

        PMEMoid B1;
        for (int i = 0; i < 100000; i++)
            void* b1 = alloc_size(pop, &B1, sizeof(N256), class_id);


        arg.unit_size = 512;
        arg.alignment = 256;
        arg.units_per_block = 4096;
        arg.header_type = POBJ_HEADER_NONE;
        if (pmemobj_ctl_set(pop, "heap.alloc_class.new.desc", &arg) != 0)
        {
            printf("Allocation class N512 initialization failed!\n");
            exit(1);
        }
        class_id = arg.class_id;
        printf("Allocation class id for N512: %d\n", class_id);

        for (int i = 0; i < 100000; i++)
            void* b1 = alloc_size(pop, &B1, 512, class_id);
    }

    pmemobj_close(pop);
    return 0;
}