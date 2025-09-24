//
// Created by edwar on 24/09/2025.
//
#include "profiler.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== DEMO DEL PROFILER ===" << std::endl;

    // Inicializar
    std::cout << "Inicializando profiler..." << std::endl;
    Profiler::initialize(43210);

    if (!Profiler::isInitialized()) {
        std::cerr << "Error: No se pudo conectar. ¿Está la interfaz corriendo?" << std::endl;
        return 1;
    }

    std::cout << "Conectado! Creando memoria..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Crear algunos bloques simples
    int* arr1 = new int[1000];      // 4KB
    double* arr2 = new double[500]; // 4KB
    char* arr3 = new char[10000];   // 10KB

    std::cout << "Creados 3 arrays (total ~18KB)" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Liberar solo 2
    delete[] arr1;
    delete[] arr2;
    std::cout << "Liberados 2 arrays, 1 queda como leak" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Stats
    std::cout << "Uso actual: " << (Profiler::getCurrentUsage() / 1024.0) << " KB" << std::endl;
    std::cout << "Asignaciones activas: " << Profiler::getActiveAllocations() << std::endl;

    std::cout << "Cerrando en 5 segundos..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    Profiler::shutdown();
    std::cout << "Demo terminado" << std::endl;

    return 0;
}