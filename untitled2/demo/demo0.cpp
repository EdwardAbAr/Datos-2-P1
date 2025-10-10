#include "profiler.h"
#include <thread>
#include <vector>
#include <iostream>

struct Foo {
    int x[256];
    Foo() = default;  // Explicit default constructor
};
int main() {
    std::cout << "=== DEMO0 START ===" << std::endl;
    profiler::init();

    auto* a   = new int(42);
    auto* arr = new int[50000];
    auto* f1 = NEW(Foo,);
    auto* f2 = NEW(Foo,);

    delete a;
    delete[] arr;

    std::cout << "Sleeping before shutdown..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Calling shutdown explicitly..." << std::endl;
    profiler::shutdown();

    std::cout << "=== DEMO0 END ===" << std::endl;
    return 0;
}
/*int main() {
    profiler::init();

    auto* a   = new int(42);
    auto* arr = new int[50000];

    // Option 1: Direct placement new (safer)
    //auto* f1  = new (__FILE__, __LINE__, "Foo") Foo();
    //auto* f2  = new (__FILE__, __LINE__, "Foo") Foo();

    // Option 2: Using macro with explicit empty args
     auto* f1 = NEW(Foo,);
     auto* f2 = NEW(Foo,);

    delete a;
    delete[] arr;

    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Terminando demo (se reportaran leaks de Foo)" << std::endl;

    profiler::shutdown();  // Explicit shutdown for testing
    return 0;
}*/