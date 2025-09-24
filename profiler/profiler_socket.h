//
// Created by edwar on 23/09/2025.
//

#ifndef PROFILER_SOCKET_H
#define PROFILER_SOCKET_H

#include <string>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SocketHandle;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
typedef int SocketHandle;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

class ProfilerSocket {
public:
    ProfilerSocket();
    ~ProfilerSocket();

    // Métodos de conexión
    bool connect(const std::string& host, int port);
    void disconnect();
    bool isConnected() const;

    // Métodos de comunicación
    bool sendData(const std::string& data);
    std::string receiveData();

    // Configuración
    void setReconnectEnabled(bool enabled);
    void setReconnectInterval(int seconds);

private:
    SocketHandle socket_;
    std::atomic<bool> connected_;
    std::atomic<bool> shouldReconnect_;
    std::string host_;
    int port_;

    // Configuración de reconexión
    bool reconnectEnabled_;
    int reconnectInterval_;
    std::thread reconnectThread_;
    std::atomic<bool> stopReconnectThread_;

    // Métodos internos
    bool initializeSocket();
    void cleanup();
    void reconnectLoop();
    bool attemptConnection();

#ifdef _WIN32
    static bool wsaInitialized_;
    static int wsaRefCount_;
    static void initializeWSA();
    static void cleanupWSA();
#endif
};

#endif // PROFILER_SOCKET_H