#include "profiler.h"
#include "profiler_socket.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <new>

namespace profiler {
    struct Memory_Block {
        void* addr{};// Dirección asignada.
        std::size_t size{};// Tamaño en bytes del dato
        std::string file;// Archivo origen analizado
        int line{};// Línea de código analizada
        std::string type;// Tipo de dato (array, int, etc)
        std::uint64_t ts{};// marca de tiempo de la asignación.
        bool freed{false}; //para saber si ya se liberó el espacio
    };

    static std::unordered_map<void*, Memory_Block> m_blocks; //tabla hash de asignaciones
    static std::mutex mutex; //Mutex para proteger los datosç
    static std::atomic<double> m_usageMB{0.0}; //memoria en uso (en MB)
    static std::atomic<double> m_peakMB{0.0}; //pico de memoria (igualmente en MB)
    static std::atomic<long long> m_totalAllocs{0}; //total de asignaciones
    static std::atomic<long long> m_totalLeaksBytes{0}; //total de bytes fugados

    //por archivo
    static std::unordered_map<std::string, long long> m_bytesByFile; //tabla de uso de memoria por archivo (en bytes)
    static std::unordered_map<std::string, long long> m_leaksCountByFile; //tabla de fugas por archivo

    //socket
    static SocketClient m_client;
    static std::atomic<bool> m_inited{false};// inicialización global.
    static std::atomic<bool> m_runSnapshots{false};// control para los hilos de snapshots
    static std::thread m_snapThread;  //hilo para emitir snapshots periodicos
    static thread_local bool th_guard = false; //seguro para evitar recursión al detectar una asignación/desasignación

    std::uint64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        //se hace un cast para que el tiempo sea en ms
        //steady no es afectado por un cambio en el reloj del sistema
        //time_since mide el tiempo desde cero
        //count() devuelve la parte entera del tiempo
    }

    //función para convertir las direcciones a string
    //ayuda al momento de enviarlas por el JSON
    static std::string addr_to_hex(void* p) {
        char buf[32]; //lista suficientemente grande para almacenar la dirección
        #ifdef _MSC_VER
                std::snprintf(buf, sizeof(buf), "0x%p", p); // 2. Formatea el puntero en texto (ej. 0x7ffeefbff5c0).
        #else
                std::snprintf(buf, sizeof(buf), "0x%p", p);// 2. Lo mismo, para compiladores no MSVC.
        #endif
                return std::string(buf); // 3. Convierte el buffer en std::string y lo devuelve.
    }

    //arma el mensaje para poder ser insertado en el JSON
    //Elimina caracteres que puedan ser conflictivos
    static std::string escape_json(const std::string& s) {
        std::string r;
        r.reserve(s.size()+8); //Reserva espacio extra en el string
        //Recorre cada carácter del string
        for (unsigned char c : s) {
            switch (c) {
                case '"': r += "\\\""; break;   //Una comilla doble la reemplaza por \".
                case '\\': r += "\\\\"; break;  // Una barra invertida la reemplaza por \\.
                case '\n': r += "\\n";  break;  // Salto de línea lo reemplaza por \n.
                case '\r': r += "\\r";  break;  // Retorno lo reemplaza por \r.
                case '\t': r += "\\t";  break;  // Tabulador lo reemplaza por \t
                // 4. Si no está en los casos anteriores:
                default:
                    if (c < 0x20) {
                        char b[7];
                        std::snprintf(b, sizeof(b), "\\u%04x", c); //Lo convierte aUnicode
                        r += b;
                    } else {
                        r += char(c);// Si es un carácter normal (visible) lo copia tal cual.
                    }
            }
        }
        return r; //En este punto la cadena ya esta lista para ser insertada en el JSON
    }

    static void send_line(std::string json_no_nl) {
        json_no_nl.push_back('\n');// Añade una linea para delimitar mensaje.
        m_client.enqueue(std::move(json_no_nl)); // Encola el evento para envío por socket.
    }

    void on_alloc(void *p, size_t n, const char *file, int line, const char *type) {
        if (!p)return; //si falla la asignación entonces no se hace nada
        if (th_guard)return; //si estamos dentro de la protección entonces retornamos para evitar la recursión
        th_guard=true;

        const std::string sfile = file ? file : ""; //Si el puntero es nulo entonces el string va a ser vacio
        const std::string stype = type ? type : "unknown";// Si no sabemos el tipo entonces el string lo indica como desconocido
        const auto ts = now_ms(); //registra el momento de la asignación
        {
            std::lock_guard<std::mutex> lock(mutex); //protejemos el bloque
            //Vamos a pasar los datos a la estructura Mememory_block
            Memory_Block block;
            block.addr=p;
            block.size=n;
            block.file=sfile;
            block.line=line;
            block.type=stype;
            block.ts=ts;
            block.freed=false;
            m_blocks[p]=std::move(block); //actualiza la tabla de has que creamos
            m_bytesByFile[sfile]+=static_cast<long long>(n); //suma los bytes de memoria utilizados
        }

        /*añadimos el valor de memoria (con una conversión a MB) a la variable de uso
         *sumamos el mismo valor para asegurarnos de leerlo bien luego
         la suma se hace porque otro hilo puede actualizar este valor
         */
        double usage= m_usageMB.fetch_add(static_cast<double>(n)/1000000.0) + static_cast<double>(n)/1000000.0;
        double prev=m_peakMB.load(); //obtenemos el pico de mamoria actual
        /*comparamos el uso actual con el historico
         * el ciclo se mantiene hasta que logra actualizar el valor del pico de memoria
         */
        while (usage > prev && !m_peakMB.compare_exchange_weak(prev, usage)) {}
        m_totalAllocs.fetch_add(1);

        //construimos el mensaje JSON
        std::string message= "{\"evento\":\"asignacion\",\"direccion\":\"" + addr_to_hex(p) +
                  "\",\"tamano\":" + std::to_string(n) +
                  ",\"archivo\":\"" + escape_json(sfile) +
                  "\",\"linea\":" + std::to_string(line) +
                  ",\"tipo\":\"" + escape_json(stype) +
                  "\",\"timestamp\":" + std::to_string(ts) + "}";
        send_line(std::move(message));
        th_guard=false;
    }

    void on_free(void* p) {
        if (!p) return; //si tenemos puntero nulo entonces no hay nada que liberar
        if (th_guard) return;
        th_guard = true;

        std::size_t sz = 0;//tamaño liberado
        std::string file;//archivo asociado
        bool was_active = false;//marca si la dirección estaba viva

        {
            std::lock_guard<std::mutex> lk(mutex);
            auto it = m_blocks.find(p);//busca el bloque por dirección.
            /*en caso de existir y que no este liberado:
             *va al valor y lo marca como liberado
             Recupera su tamaño y el archivo asociado
             */

            if (it != m_blocks.end() && !it->second.freed) {
                it->second.freed = true;
                sz = it->second.size;
                file = it->second.file;
                was_active = true;
                auto fit = m_bytesByFile.find(file); //usamos auto porque no sabemos explicitamente el tipo de dato que estamos manejando
                /*Si encontramos el archivo en la tabla entonces:
                * obtenemos su tamaño
                * resta el valor para dejarlo en cero
                * en caso de que quede un valor negativo entonces se ajusta a cero
                */
                if (fit != m_bytesByFile.end()) {
                    fit->second -= static_cast<long long>(sz);
                    if (fit->second < 0) fit->second = 0;
                }
            }
        }

        //actualizamos el uso en memoria para restar la memoria de lo que acabamos de liberar
        if (was_active) {
            m_usageMB.fetch_sub(double(sz) /1000000.0);

            //se crea y envia el mensaje JSON
            std::string message = "{\"evento\":\"liberacion\",\"direccion\":\"" + addr_to_hex(p) +
                "\",\"timestamp\":" + std::to_string(now_ms()) + "}";
            send_line(std::move(message));

            th_guard = false;
        }
    }

    static void leak_report() {
        long long bytes_leaked=0;
        {
            std::lock_guard<std::mutex> lk(mutex);
            for (auto& kv: m_blocks) {
                Memory_Block& block = kv.second; //obtenemos la dirección en memoria
                if (!block.freed) {
                    bytes_leaked+=static_cast<long long>(block.size);
                    m_leaksCountByFile[block.file]++;

                    //creamos el mensaje de que hubo una fuga de memoria
                    std::string message = "{\"evento\":\"leak_report\",\"direccion\":\"" + addr_to_hex(block.addr) +
                       "\",\"tamano\":" + std::to_string(block.size) +
                       ",\"archivo\":\"" + escape_json(block.file) +
                       "\",\"timestamp\":" + std::to_string(now_ms()) + "}";
                    send_line(std::move(message));
                }
            }
        }
        m_totalLeaksBytes.store(bytes_leaked);
        std::string snap = "{\"evento\":\"snapshot\",\"uso_actual_mb\":" + std::to_string(m_usageMB.load()) +
                     ",\"uso_max_mb\":" + std::to_string(m_peakMB.load()) +
                     ",\"total_asignaciones\":" + std::to_string(m_totalAllocs.load()) +
                     ",\"timestamp\":" + std::to_string(now_ms()) + "}";
        send_line(std::move(snap));
    }

    static void snap_thread() {
        using namespace std::chrono_literals;
        while (m_runSnapshots.load()) {
            std::string message = "{\"evento\":\"snapshot\",\"uso_actual_mb\":" + std::to_string(m_usageMB.load()) +
                    ",\"uso_max_mb\":" + std::to_string(m_peakMB.load()) +
                    ",\"total_asignaciones\":" + std::to_string(m_totalAllocs.load()) +
                    ",\"timestamp\":" + std::to_string(now_ms()) + "}";
            send_line(std::move(message));
            std::this_thread::sleep_for(800ms); //asegura que sea periodico
        }
    }

    static void exit_handler() {
        shutdown();
    }
    void init() {
        bool expected = false;
        if (!m_inited.compare_exchange_strong(expected, true)) return; // Si ya estaba inicializado, salir.

        m_client.start();//arranca el cliente de socket (hilo/cola).
        m_runSnapshots.store(true);//activa flag para el hilo periódico.
        m_snapThread = std::thread(&snap_thread);//lanza el hilo de snapshots.
        std::atexit(&exit_handler);//registra handler para cerrar automáticamente.
    }

    void snapshot() {
        std::string j = "{\"evento\":\"snapshot\",\"uso_actual_mb\":" + std::to_string(m_usageMB.load()) +
                        ",\"uso_max_mb\":" + std::to_string(m_peakMB.load()) +
                        ",\"total_asignaciones\":" + std::to_string(m_totalAllocs.load()) +
                        ",\"timestamp\":" + std::to_string(now_ms()) + "}";
        send_line(std::move(j));                          // Enviar snapshot.
    }

    void shutdown() {
        if (!m_inited.load()) return;// Si nunca se inició, nada que hacer.
        //señala al hilo de snapshots que se detenga
        if (m_runSnapshots.exchange(false)) {
            if (m_snapThread.joinable()) m_snapThread.join();
        }
        leak_report();//reporta fugas restantes y snapshot final.
        m_client.stop();//detiene los sockets.
        m_inited.store(false);//marca sistema como no inicializado.
    }

}
using namespace profiler;
void* operator new(std::size_t n) {
    if (!profiler::m_inited.load()) profiler::init();
    if (profiler::th_guard) return std::malloc(n);//si esta dentro del guard, delega directo a malloc
    profiler::th_guard = true; //activa el guard para evitar la recursión
    void* p = std::malloc(n);//asignación real con malloc
    profiler::th_guard = false;
    if (!p) throw std::bad_alloc();//propaga fallo estándar
    profiler::on_alloc(p, n, "", 0, "unknown"); // registra asignación sin info de archivo/línea
    return p;//devuelve puntero.
}

void  operator delete(void* p) noexcept {
  if (!p) return;//si el puntero es nulo no hay que devolver
  if (profiler::th_guard) { std::free(p); return; }
  profiler::on_free(p);//registra la liberación
  std::free(p);//libera el espacio
}

void* operator new[](std::size_t n) {
  if (!profiler::m_inited.load()) profiler::init();
  if (profiler::th_guard) return std::malloc(n);
  profiler::th_guard = true;
  void* p = std::malloc(n);
  profiler::th_guard = false;
  if (!p) throw std::bad_alloc();//manejo de error estándar.
  profiler::on_alloc(p, n, "", 0, "array");//registra como array
  return p;//retorna puntero.
}

void  operator delete[](void* p) noexcept {
  if (!p) return;
  if (profiler::th_guard) { std::free(p); return; }
  profiler::on_free(p);
  std::free(p);
}

void* operator new(std::size_t n, const char* file, int line, const char* type) {
  if (!profiler::m_inited.load())profiler::init();
  if (profiler::th_guard) return std::malloc(n);
  profiler::th_guard = true;
  void* p = std::malloc(n);
  profiler::th_guard = false;
  if (!p) throw std::bad_alloc();
  profiler::on_alloc(p, n, file, line, type ? type : "unknown");
  return p;
}

