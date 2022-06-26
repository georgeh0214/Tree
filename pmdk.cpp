#include <iostream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cassert>
#include <fstream>
#include <sys/stat.h>

#include <libpmemobj.h>

using namespace std;

#define SIZE PMEMOBJ_MIN_POOL // 8388608
#define PATH "./pool"

struct Node;

POBJ_LAYOUT_BEGIN(Test);
POBJ_LAYOUT_TOID(Test, struct Node);
POBJ_LAYOUT_END(Test);

struct Node
{
    int a;
    // Node* next;
    PMEMoid next;
};

int main()
{
	PMEMobjpool * pop;
    struct stat buffer;
    if (stat(PATH, &buffer) == 0)
    {
    	printf("Pool exists!\n");
    	pop = pmemobj_open(PATH, POBJ_LAYOUT_NAME(Test));
    	assert(pop && "pmemobj_open");

    	PMEMoid root = pmemobj_root(pop, sizeof(struct Node));
    	struct Node * head = (struct Node *)pmemobj_direct(root);
    	cout << head->a << " " << pmemobj_direct(head->next) << endl;

    	if (!OID_IS_NULL(head->next))
    	{
    		struct Node * new_node = (struct Node *)pmemobj_direct(head->next);
    		cout << new_node->a << " " << pmemobj_direct(new_node->next) << endl;
    	}

    }
    else
    {
    	printf("Creating pool!\n");
    	pop = pmemobj_create(PATH, POBJ_LAYOUT_NAME(Test), PMEMOBJ_MIN_POOL, 0666);
	    assert(pop && "pmemobj_create");

	    PMEMoid root = pmemobj_root(pop, sizeof(struct Node));
	    cout << "Node type num: " << pmemobj_type_num(root) << endl;
    	struct Node * head = (struct Node *)pmemobj_direct(root);
    	head->a = 214;

    	if (pmemobj_alloc(pop, &head->next, sizeof(Node), 0, NULL, NULL) != 0)
    		assert(false && "pmemobj_alloc");
    	struct Node * new_node = (struct Node *)pmemobj_direct(head->next);
    	new_node->a = 214;
    	new_node->next = OID_NULL;
    	pmemobj_persist(pop, new_node, sizeof(Node));
    	pmemobj_persist(pop, head, sizeof(Node));

    	// PMEMoid p;
    	// if (pmemobj_alloc(pop, &p, sizeof(Node), 0, NULL, NULL) != 0)
    	// 	assert(false && "pmemobj_alloc");
    	// struct Node * new_node = (struct Node *)pmemobj_direct(p);
    	// new_node->a = 214;
    	// new_node->next = nullptr;
    	// pmemobj_persist(pop, new_node, sizeof(Node));

    	// head->next = new_node;
    	// pmemobj_persist(pop, head, sizeof(Node));


    }

    pmemobj_close(pop);
    return 0;
}