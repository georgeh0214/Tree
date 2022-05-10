#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

uint64_t lock;
bool stop = false;

void Writer() {
	uint64_t count = 0;
Again1:
	if (_xbegin() != _XBEGIN_STARTED)
		goto Again1;
	lock = 1;
	count ++;
	_xend();
	if (!stop)
		goto Again1;
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

	std::this_thread::sleep_for(std::chrono::milliseconds(3000));
	stop = true;
	t1.join();
	t2.join();
	return 0;
}