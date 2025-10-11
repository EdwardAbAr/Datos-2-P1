#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include "profiler.h"

using namespace std::chrono_literals;

// -----------------------------------------------------------------------------
// Utilidades básicas
// -----------------------------------------------------------------------------
static inline void pausa_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static inline std::string mb_str(std::size_t bytes) {
    double mb = bytes / (1024.0 * 1024.0);
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << mb << " MB";
    return os.str();
}

// -----------------------------------------------------------------------------
// Ruido ligero: asigna y libera bloques pequeños sin afectar el pico de memoria.
// Esto sirve para que el perfilador registre actividad de manera continua.
// -----------------------------------------------------------------------------
void churn_pequenio(unsigned rondas = 300) {
    std::vector<char*> ring(64, nullptr);
    size_t idx = 0;

    std::cout << "[Churn] Inicio de actividad ligera (bloques 4–16 KB)." << std::endl;

    for (unsigned i = 0; i < rondas; ++i) {
        if (ring[idx]) { delete[] ring[idx]; ring[idx] = nullptr; }
        // 4–16 KB
        size_t sz = (4 + (i % 13)) * 1024;
        char* p = new char[sz];
        if (p) { p[0] = char(i); }
        ring[idx] = p;
        idx = (idx + 1) % ring.size();
        if ((i % 50) == 0) pausa_ms(5);
    }

    for (auto* p : ring) if (p) delete[] p;
    std::cout << "[Churn] Finalizado. Se liberaron los bloques pequeños." << std::endl;
}

// -----------------------------------------------------------------------------
// Asigna bloques de 256 KB hasta aproximarse a target_mb.
// Mantiene una fracción viva por un momento para que el pico sea visible,
// luego libera casi todo y (opcional) deja un leak pequeño para probar el reporte.
// -----------------------------------------------------------------------------
void asignar_hasta_presupuesto_mb(double target_mb = 15.0, bool dejar_leak = true) {
    const size_t TAM_BLOQUE = 256 * 1024;        // 256 KB
    const double proporcion_retener = 0.40;      // ~40% vivos temporalmente
    const size_t presupuesto_bytes = static_cast<size_t>(target_mb * 1024.0 * 1024.0);

    std::vector<char*> bloques;
    bloques.reserve(128);

    std::cout << "[Carga] Objetivo de uso de memoria: ~" << target_mb << " MB." << std::endl;

    // Asignación escalonada para que se aprecie la subida en snapshots
    size_t acumulado = 0;
    while (acumulado + TAM_BLOQUE <= presupuesto_bytes) {
        char* p = new char[TAM_BLOQUE];
        if (!p) break;

        // Tocar algunas páginas para materializar el uso físico (evita reservas perezosas)
        for (size_t i = 0; i < TAM_BLOQUE; i += 4096) p[i] = char((i >> 12) & 0x7F);

        bloques.push_back(p);
        acumulado += TAM_BLOQUE;

        if ((bloques.size() % 8) == 0) {
            std::cout << "  - Progreso de asignación: " << mb_str(acumulado) << std::endl;
            pausa_ms(40);
        }
    }

    std::cout << "[Carga] Asignación alcanzada: " << mb_str(acumulado)
              << " (" << bloques.size() << " bloques de 256 KB)." << std::endl;

    profiler::snapshot();  // marca de estado para tu interfaz

    // Retener una parte para sostener un pico breve
    std::shuffle(bloques.begin(), bloques.end(), std::mt19937{123});
    size_t a_retener = static_cast<size_t>(bloques.size() * proporcion_retener);

    std::cout << "[Carga] Se retendrá temporalmente ~" << std::fixed << std::setprecision(0)
              << (proporcion_retener * 100) << "% de bloques (" << a_retener
              << ") para sostener el pico." << std::endl;

    for (size_t i = a_retener; i < bloques.size(); ++i) {
        delete[] bloques[i];
        bloques[i] = nullptr;
    }

    std::cout << "[Carga] Pico sostenido por un momento para visualización..." << std::endl;
    pausa_ms(600);
    profiler::snapshot();

    // Liberar casi todo lo retenido y decidir si se deja fuga o no
    char* fuga = nullptr;
    if (dejar_leak && a_retener > 0) {
        // Leak intencional de 128 KB para validar el leak_report del perfilador
        fuga = new char[128 * 1024];
        if (fuga) std::memset(fuga, 1, 128 * 1024);
        std::cout << "[Carga] Se dejó una fuga intencional de 128 KB para prueba de reporte de fugas."
                  << std::endl;
    } else {
        std::cout << "[Carga] No se dejarán fugas; se liberará toda la memoria retenida." << std::endl;
    }

    for (size_t i = 0; i < a_retener; ++i) {
        if (bloques[i]) { delete[] bloques[i]; bloques[i] = nullptr; }
    }

    std::cout << "[Carga] Memoria liberada. Realizando snapshot intermedio..." << std::endl;
    pausa_ms(400);
    profiler::snapshot();

    if (!dejar_leak) {
        std::cout << "[Resumen] No se reportan fugas por diseño en esta ejecución." << std::endl;
    } else {
        (void)fuga; // se mantiene vivo hasta el shutdown para que el leak_report lo capture
        std::cout << "[Resumen] Se espera que el perfilador reporte al menos 1 fuga (128 KB)."
                  << std::endl;
    }
}

int main(int argc, char** argv) {
    profiler::init();

    // Parámetros ajustables por línea de comando:
    // argv[1] = objetivo MB (double), argv[2] = "leak" | "noleak"
    double objetivo_mb = 15.0;     // ~15 MB por defecto
    bool dejar_leak = true;        // por defecto se deja una fuga pequeña

    if (argc > 1) {
        try { objetivo_mb = std::stod(argv[1]); }
        catch (...) { /* se mantiene el valor por defecto */ }
    }
    if (argc > 2) {
        std::string flag = argv[2];
        for (auto& c : flag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (flag == "noleak" || flag == "no_leak" || flag == "sinleak" || flag == "sin_fuga") {
            dejar_leak = false;
        }
    }

    std::cout << "=== Demo ligera de memoria ===" << std::endl;
    std::cout << "Objetivo aproximado: " << objetivo_mb << " MB | "
              << (dejar_leak ? "Modo: con fuga intencional" : "Modo: sin fugas")
              << std::endl;

    // Calentamiento breve: asignaciones pequeñas que no mueven mucho el uso total
    {
        std::cout << "[Warmup] Iniciando calentamiento..." << std::endl;
        std::vector<int*> v;
        for (int i = 0; i < 200; ++i) {
            int* p = new int[256]; // ~1 KB
            if (p) p[0] = i;
            v.push_back(p);
        }
        for (auto* p : v) delete[] p;
        std::cout << "[Warmup] Finalizado." << std::endl;
    }

    pausa_ms(150);

    // Actividad ligera en un hilo aparte (para que el perfilador tenga eventos entre snapshots)
    std::thread hilo_churn([] { churn_pequenio(400); });

    // Bloque principal que empuja el uso de memoria hasta ~objetivo_mb
    asignar_hasta_presupuesto_mb(objetivo_mb, dejar_leak);

    if (hilo_churn.joinable()) hilo_churn.join();

    std::cout << "Demo ligera terminada. Se procederá a cerrar el perfilador." << std::endl;

    // Tu shutdown ya emite:
    // - leak_report con las direcciones/tamaños no liberados (si hay)
    // - snapshot final con métricas acumuladas
    profiler::shutdown();
    std::cout << "Perfilador cerrado. Ejecución completa." << std::endl;
    return 0;
}
