//
// Created by edwar on 23/09/2025.
//

#ifndef PROFILER_H
#define PROFILER_H

#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <memory>

// Forward declaration
class ProfilerSocket;

// Estructura para almacenar información de cada bloque de memoria
struct MemoryBlockInfo {
    void* address;
    size_t size;
    std::string type;
    std::string file;
    int line;
    std::chrono::system_clock::time_point timestamp;
    bool freed;

    MemoryBlockInfo() : address(nullptr), size(0), line(0), freed(false) {
        timestamp = std::chrono::system_clock::now();
    }

    MemoryBlockInfo(void* addr, size_t sz, const std::string& tp, const std::string& f, int l)
        : address(addr), size(sz), type(tp), file(f), line(l), freed(false) {
        timestamp = std::chrono::system_clock::now();
    }
};

// Clase principal del profiler
class Profiler {
public:
    // Métodos de control del profiler
    static void initialize(int port = 43210);
    static void shutdown();
    static bool isInitialized();

    // Métodos de rastreo (llamados por los operadores sobrecargados)
    static void recordAllocation(void* ptr, size_t size, const std::string& type = "unknown",
                               const std::string& file = "unknown", int line = 0);
    static void recordDeallocation(void* ptr);

    // Detección de fugas
    static void detectLeaks();
    static void generateReport();

    // Estadísticas
    static size_t getCurrentUsage();
    static size_t getPeakUsage();
    static size_t getTotalAllocations();
    static size_t getActiveAllocations();

private:
    // Estado interno del profiler
    static bool initialized_;
    static std::mutex mutex_;
    static std::unordered_map<void*, MemoryBlockInfo> allocations_;
    static std::unique_ptr<ProfilerSocket> socket_;

    // Estadísticas
    static size_t currentUsage_;
    static size_t peakUsage_;
    static size_t totalAllocations_;

    // Métodos internos
    static void sendAllocationEvent(void* ptr, size_t size, const std::string& file, int line, const std::string& type);
    static void sendDeallocationEvent(void* ptr);
    static void sendLeakReport(void* ptr, size_t size, const std::string& file, int line);
    static void sendSnapshot();

    // Obtener información de la llamada (stack trace simplificado)
    static void getCallerInfo(std::string& file, int& line);
};

// Macros para facilitar el uso
#define PROFILER_INIT() Profiler::initialize()
#define PROFILER_SHUTDOWN() Profiler::shutdown()

#endif // PROFILER_H