#include "profiler_socket.h"
#include <chrono>

namespace profiler {
    static constexpr const char* Host = "127.0.0.1";
    static constexpr int Port = 43210;

    static bool g_wsa_inited = false;
    //Ayuda a inicializar el socket
    static void ensure_wsa() {
        if (!g_wsa_inited) {
            WSADATA wsa_data; WSAStartup(MAKEWORD(2, 2), &wsa_data);
            g_wsa_inited = true;
        }
    }

    SocketClient::SocketClient() {} //Constructor
    SocketClient::~SocketClient(){stop(); WSACleanup();} //Destructor

    void SocketClient::start() {
        if (run_.exchange(true))return; //Ayuda aque solo se cree un hilo
        th_=std::thread(&SocketClient::run,this); //crea el hilo mpara la comunicación
    }

    void SocketClient::stop() {
        if (!run_.exchange(false))return; //si ya estaba detenido entonces no hace nada
        if (th_.joinable())th_.join(); //espera a que el hilo termine sus tareas y luego lo cierra
        close_socket();
    }

    bool SocketClient::connect_once() {
        ensure_wsa();
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0); //crea el socket como ipV4, lo declara como tcp/ip y el protocolo es por defecto
        if (s == INVALID_SOCKET)return false;

        sockaddr_in addr{}; //dirección del socket
        addr.sin_family = AF_INET;
        addr.sin_port = htons(Port);
        addr.sin_addr.s_addr = inet_addr(Host);

        if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==SOCKET_ERROR) {
            ::closesocket(s);
            return false;
        }
        socket_ = s;
        return true;
    }

    void SocketClient::close_socket() {
        //Verifica la validez del socket
        //Si es valido (tiene conexión) entonces
        if (socket_ != INVALID_SOCKET) {
            ::closesocket(socket_); //lo cierra
            socket_ = INVALID_SOCKET; // lo marca como desconectado
        }
    }

    void SocketClient::enqueue(std::string line) {
        std::lock_guard<std::mutex>lk(q_mtx_); //protege la cola bloqueando el acceso
        queue.push_back(std::move(line)); //se usa move() para evitar copias del mensaje (así nos aseguramos que no haya duplicidad)
    }

    bool SocketClient::send_all(const char *data, size_t len) {
        size_t sent = 0; //cantidad de bytes enviados
        //ciclo para enviar los bytes
        while (sent < len) {
            int n = ::send(socket_, data + sent, (int)(len - sent), 0);
            if (n<=0) return false; //en caso de que haya un error en la comunicación o este cerrada
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    void SocketClient::run() {
        using namespace std::chrono_literals;
        while (run_) {
            // Verifica el estado de la conexión, en caso de de haberla espera un tiempo antes de intentar reconectar
            if (socket_ == INVALID_SOCKET && !connect_once()) {
                std::this_thread::sleep_for(300ms);
                continue;
            }
            std::vector<std::string> batch; //genera un vector para los mensajes JSON
            {
                //Volvemos a bloquear la cola para que solo un hilo pueda modificarla
                std::lock_guard<std::mutex> lk(q_mtx_);
                //si la cola tiene elementos entonces los mueve al vector
                if (!queue.empty()){
                    batch.swap(queue);
                }

            }

            //Recorre linea por linea el vector
            for (auto& line : batch) {
                //si no se logra enviar todos los datos entonces marca error
                if (!send_all(line.c_str(),line.size())) {
                    close_socket(); //al marcarse un error entonces fuerza el cierre
                    break;
                }


                std::this_thread::sleep_for(20ms); //espera un momento antes de seguir
            }
        }
        //Verificamos que no queden elementos en cola luego del bucle
        if (socket_ != INVALID_SOCKET) {
            //volvemos a crear el vector, lo protejemos, encolamos y enviamos los mensajes restantes
            std::vector<std::string> batch;
            {
                std::lock_guard<std::mutex> lk(q_mtx_);
                batch.swap(queue);
            }
            for (auto& line : batch) {
                send_all(line.c_str(),line.size());
            }
        }
        close_socket(); //cerramos comunicación
    }
}