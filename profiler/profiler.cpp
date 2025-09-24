//
// Created by edwar on 23/09/2025.
//

#include "profiler.h"
#include "profiler_socket.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

// Definición de variables estáticas
bool Profiler::initialized_ = false;
std::mutex Profiler::mutex_;
std::unordered_map<void*, MemoryBlockInfo> Profiler::allocations_;
std::unique_ptr<ProfilerSocket> Profiler::socket_;
size_t Profiler::currentUsage_ = 0;
size_t Profiler::peakUsage_ = 0;
size_t Profiler::totalAllocations_ = 0;

void Profiler::initialize(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        std::cout << "[Profiler] Ya está inicializado" << std::endl;
        return;
    }

    // Inicializar socket
    socket_ = std::make_unique<ProfilerSocket>();
    if (!socket_->connect("127.0.0.1", port)) {
        std::cerr << "[Profiler] Error conectando al puerto " << port << std::endl;
        return;
    }

    // Resetear estadísticas
    allocations_.clear();
    currentUsage_ = 0;
    peakUsage_ = 0;
    totalAllocations_ = 0;

    initialized_ = true;
    std::cout << "[Profiler] Inicializado en puerto " << port << std::endl;

    // Enviar snapshot inicial
    sendSnapshot();
}

void Profiler::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;

    std::cout << "[Profiler] Generando reporte final..." << std::endl;
    detectLeaks();
    generateReport();

    // Cerrar conexión
    if (socket_) {
        socket_->disconnect();
        socket_.reset();
    }

    initialized_ = false;
    std::cout << "[Profiler] Desconectado" << std::endl;
}

bool Profiler::isInitialized() {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

void Profiler::recordAllocation(void* ptr, size_t size, const std::string& type,
                               const std::string& file, int line) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;

    // Crear registro del bloque
    MemoryBlockInfo info(ptr, size, type, file, line);
    allocations_[ptr] = info;

    // Actualizar estadísticas
    currentUsage_ += size;
    if (currentUsage_ > peakUsage_) {
        peakUsage_ = currentUsage_;
    }
    totalAllocations_++;

    // Enviar evento a la interfaz
    sendAllocationEvent(ptr, size, file, line, type);

    // Debug
    std::cout << "[Profiler] Asignación: " << ptr << " (" << size << " bytes) en "
              << file << ":" << line << std::endl;
}

void Profiler::recordDeallocation(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;

    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        // Actualizar estadísticas
        currentUsage_ -= it->second.size;

        // Marcar como liberado
        it->second.freed = true;

        // Enviar evento
        sendDeallocationEvent(ptr);

        // Debug
        std::cout << "[Profiler] Liberación: " << ptr << " (" << it->second.size << " bytes)" << std::endl;

        // Remover del mapa (opcional - se puede mantener para historial)
        // allocations_.erase(it);
    } else {
        std::cerr << "[Profiler] Advertencia: Intento de liberar puntero no rastreado: " << ptr << std::endl;
    }
}

void Profiler::detectLeaks() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;

    size_t leakCount = 0;
    size_t leakSize = 0;

    for (const auto& pair : allocations_) {
        const MemoryBlockInfo& info = pair.second;
        if (!info.freed) {
            leakCount++;
            leakSize += info.size;

            // Reportar cada leak
            sendLeakReport(info.address, info.size, info.file, info.line);

            std::cout << "[Profiler] LEAK: " << info.address << " (" << info.size
                      << " bytes) en " << info.file << ":" << info.line << std::endl;
        }
    }

    std::cout << "[Profiler] Resumen de fugas: " << leakCount << " bloques, "
              << leakSize << " bytes" << std::endl;
}

void Profiler::generateReport() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;

    sendSnapshot();

    std::cout << "\n=== REPORTE PROFILER ===" << std::endl;
    std::cout << "Uso actual: " << (currentUsage_ / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "Uso máximo: " << (peakUsage_ / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "Total asignaciones: " << totalAllocations_ << std::endl;
    std::cout << "Asignaciones activas: " << getActiveAllocations() << std::endl;
    std::cout << "========================" << std::endl;
}

size_t Profiler::getCurrentUsage() {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentUsage_;
}

size_t Profiler::getPeakUsage() {
    std::lock_guard<std::mutex> lock(mutex_);
    return peakUsage_;
}

size_t Profiler::getTotalAllocations() {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalAllocations_;
}

size_t Profiler::getActiveAllocations() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t active = 0;
    for (const auto& pair : allocations_) {
        if (!pair.second.freed) active++;
    }
    return active;
}

void Profiler::sendAllocationEvent(void* ptr, size_t size, const std::string& file,
                                  int line, const std::string& type) {
    if (!socket_) return;

    // Crear timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // Formatear dirección como string hexadecimal
    std::stringstream ss;
    ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(ptr);

    // Crear JSON
    std::stringstream json;
    json << "{"
         << "\"evento\":\"asignacion\","
         << "\"direccion\":\"" << ss.str() << "\","
         << "\"tamano\":" << size << ","
         << "\"tipo\":\"" << type << "\","
         << "\"archivo\":\"" << file << "\","
         << "\"linea\":" << line << ","
         << "\"timestamp\":" << timestamp
         << "}\n";

    socket_->sendData(json.str());
}

void Profiler::sendDeallocationEvent(void* ptr) {
    if (!socket_) return;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(ptr);

    std::stringstream json;
    json << "{"
         << "\"evento\":\"liberacion\","
         << "\"direccion\":\"" << ss.str() << "\","
         << "\"timestamp\":" << timestamp
         << "}\n";

    socket_->sendData(json.str());
}

void Profiler::sendLeakReport(void* ptr, size_t size, const std::string& file, int line) {
    if (!socket_) return;

    std::stringstream ss;
    ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(ptr);

    std::stringstream json;
    json << "{"
         << "\"evento\":\"leak_report\","
         << "\"direccion\":\"" << ss.str() << "\","
         << "\"tamano\":" << size << ","
         << "\"archivo\":\"" << file << "\","
         << "\"linea\":" << line
         << "}\n";

    socket_->sendData(json.str());
}

void Profiler::sendSnapshot() {
    if (!socket_) return;

    std::stringstream json;
    json << "{"
         << "\"evento\":\"snapshot\","
         << "\"uso_actual_mb\":" << (currentUsage_ / 1024.0 / 1024.0) << ","
         << "\"uso_max_mb\":" << (peakUsage_ / 1024.0 / 1024.0) << ","
         << "\"total_asignaciones\":" << totalAllocations_
         << "}\n";

    socket_->sendData(json.str());
}

void Profiler::getCallerInfo(std::string& file, int& line) {
    // Implementación simplificada - en un caso real usarías stack traces
    file = "unknown";
    line = 0;
}

// ====================================================================
// SOBRECARGA GLOBAL DE OPERADORES NEW/DELETE
// ====================================================================

void* operator new(size_t size) {
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();

    Profiler::recordAllocation(ptr, size, "new", "unknown", 0);
    return ptr;
}

void* operator new[](size_t size) {
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();

    Profiler::recordAllocation(ptr, size, "new[]", "unknown", 0);
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;

    Profiler::recordDeallocation(ptr);
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    if (!ptr) return;

    Profiler::recordDeallocation(ptr);
    std::free(ptr);
}