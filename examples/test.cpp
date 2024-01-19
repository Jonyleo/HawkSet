#include <thread>


int test_trap_hawkset(int a) {
    return 1000;
} 
 

void gaming() {

    test_trap_hawkset(123123);
}

void gaming2() {
    gaming();
}

int main(int argc, char *argv[])
{

    std::thread t(gaming);

    gaming2();

    t.join();
    return 0;
}
