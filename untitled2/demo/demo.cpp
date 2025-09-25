#include "profiler.h"
#include <thread>
#include <vector>
#include <iostream>

struct Foo { int x[256]; };

int main() {
    profiler::init();

    auto* a   = new int(42);
    auto* arr = new int[50000];
    auto* f1  = NEW(Foo);
    auto* f2  = NEW(Foo);

    delete a;
    delete[] arr;

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Terminando demo (se reportaran leaks de Foo)";
    return 0; // atexit -> memprof::shutdown() -> leak_report
}