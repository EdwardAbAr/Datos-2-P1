#pragma once
#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace profiler {
    class SocketClient {
    public:
        SocketClient();
        ~SocketClient();

        //Le hace enqueue al JSON para que el hilo lo envie
        void enqueue(std::string line);
        //Arranca el hilo emisor
        void start();

        void stop();

    private:
        void run();
        bool connect_once();
        void close_socket();
        bool send_all(const char* data, size_t len);
        std::atomic<bool> run_{false};
        std::thread th_;
        std::mutex q_mtx_; //ayuda a que solo un hilo pueda tener acceso a los datos
        std::vector<std::string> queue; //cola de mensajes JSON
        SOCKET socket_ = INVALID_SOCKET;

    };
}
