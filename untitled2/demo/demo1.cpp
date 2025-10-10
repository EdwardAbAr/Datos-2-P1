#include <iostream>

#include "profiler.h"
#include <vector>
#include <thread>

int main() {
    profiler::init();

    for (int i = 0; i < 1000; ++i) {
        auto* arr = NEW_ARRAY(int, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        delete[] arr;
    }


    std::cout << "Demo 2 terminado.\n";
    return 0;
}
