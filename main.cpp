#include <iostream>
#include <thread>
#include <chrono>
#include <immintrin.h>

using namespace std;

uint64_t lock;
bool stop = false;

void inc(){
	int i = lock;
	lock = i + 1;
}

void Writer() {
	uint64_t i = 0;
	uint64_t count = 0;
Again1:
//	if (_xbegin() != _XBEGIN_STARTED)
//		goto Again1;
//	for (i = 0; i < 100000000000; i++)
//		lock = i;
//	inc();
//	lock ++;
//	count ++;
//	_xend();
//	if (!stop)
//		goto Again1;
	while (!stop)
		lock++;
	printf("Write thread completed %llu iterations\n", count);
}

void Reader() {
	uint64_t val, count = 0;
Again2:
	if (_xbegin() != _XBEGIN_STARTED)
		goto Again2;
	val = lock;
	count ++;
	_xend();
	if (!stop)
		goto Again2;
	printf("Read thread completed %llu iterations\n", count);
}

int main(int argc, char const *argv[])
{
	std::thread t1(Writer);
	std::thread t2(Reader);

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	stop = true;
	t1.join();
	t2.join();
	return 0;
}
