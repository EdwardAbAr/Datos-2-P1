//
// Created by edwar on 23/09/2025.
//

#include "profiler_socket.h"
#include <iostream>
#include <chrono>
#include <cstring>

#ifdef _WIN32
bool ProfilerSocket::wsaInitialized_ = false;
int ProfilerSocket::wsaRefCount_ = 0;
#pragma comment(lib, "ws2_32.lib")
#endif

ProfilerSocket::ProfilerSocket()
    : socket_(INVALID_SOCKET), connected_(false), shouldReconnect_(false),
      port_(0), reconnectEnabled_(true), reconnectInterval_(5),
      stopReconnectThread_(false) {

#ifdef _WIN32
    initializeWSA();
#endif
}

ProfilerSocket::~ProfilerSocket() {
    disconnect();

#ifdef _WIN32
    cleanupWSA();
#endif
}

bool ProfilerSocket::connect(const std::string& host, int port) {
    if (connected_) {
        std::cout << "[ProfilerSocket] Ya está conectado" << std::endl;
        return true;
    }

    host_ = host;
    port_ = port;

    if (!initializeSocket()) {
        std::cerr << "[ProfilerSocket] Error inicializando socket" << std::endl;
        return false;
    }

    if (!attemptConnection()) {
        cleanup();

        // Si falla la conexión inicial y está habilitada la reconexión
        if (reconnectEnabled_) {
            std::cout << "[ProfilerSocket] Conexión inicial fallida, iniciando reconexión automática..." << std::endl;
            shouldReconnect_ = true;
            stopReconnectThread_ = false;
            reconnectThread_ = std::thread(&ProfilerSocket::reconnectLoop, this);
        }
        return false;
    }

    connected_ = true;
    std::cout << "[ProfilerSocket] Conectado a " << host << ":" << port << std::endl;
    return true;
}

void ProfilerSocket::disconnect() {
    connected_ = false;
    shouldReconnect_ = false;

    // Detener thread de reconexión si existe
    if (reconnectThread_.joinable()) {
        stopReconnectThread_ = true;
        reconnectThread_.join();
    }

    cleanup();
    std::cout << "[ProfilerSocket] Desconectado" << std::endl;
}

bool ProfilerSocket::isConnected() const {
    return connected_;
}

bool ProfilerSocket::sendData(const std::string& data) {
    if (!connected_ || socket_ == INVALID_SOCKET) {
        // Si no estamos conectados pero la reconexión está habilitada,
        // intentar reconectar
        if (reconnectEnabled_ && !shouldReconnect_) {
            shouldReconnect_ = true;
            if (!reconnectThread_.joinable()) {
                stopReconnectThread_ = false;
                reconnectThread_ = std::thread(&ProfilerSocket::reconnectLoop, this);
            }
        }
        return false;
    }

    int totalSent = 0;
    int dataSize = static_cast<int>(data.length());
    const char* buffer = data.c_str();

    while (totalSent < dataSize) {
        int sent = send(socket_, buffer + totalSent, dataSize - totalSent, 0);
        if (sent == SOCKET_ERROR) {
#ifdef _WIN32
            int error = WSAGetLastError();
            std::cerr << "[ProfilerSocket] Error enviando datos: " << error << std::endl;
#else
            std::cerr << "[ProfilerSocket] Error enviando datos: " << strerror(errno) << std::endl;
#endif
            connected_ = false;

            // Iniciar reconexión automática si está habilitada
            if (reconnectEnabled_) {
                shouldReconnect_ = true;
                if (!reconnectThread_.joinable()) {
                    stopReconnectThread_ = false;
                    reconnectThread_ = std::thread(&ProfilerSocket::reconnectLoop, this);
                }
            }
            return false;
        }
        totalSent += sent;
    }

    return true;
}

std::string ProfilerSocket::receiveData() {
    if (!connected_ || socket_ == INVALID_SOCKET) {
        return "";
    }

    char buffer[1024];
    int received = recv(socket_, buffer, sizeof(buffer) - 1, 0);

    if (received == SOCKET_ERROR) {
#ifdef _WIN32
        int error = WSAGetLastError();
        std::cerr << "[ProfilerSocket] Error recibiendo datos: " << error << std::endl;
#else
        std::cerr << "[ProfilerSocket] Error recibiendo datos: " << strerror(errno) << std::endl;
#endif
        connected_ = false;
        return "";
    }

    if (received == 0) {
        std::cout << "[ProfilerSocket] Conexión cerrada por el servidor" << std::endl;
        connected_ = false;
        return "";
    }

    buffer[received] = '\0';
    return std::string(buffer);
}

void ProfilerSocket::setReconnectEnabled(bool enabled) {
    reconnectEnabled_ = enabled;
    if (!enabled) {
        shouldReconnect_ = false;
    }
}

void ProfilerSocket::setReconnectInterval(int seconds) {
    reconnectInterval_ = seconds;
}

bool ProfilerSocket::initializeSocket() {
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
#ifdef _WIN32
        std::cerr << "[ProfilerSocket] Error creando socket: " << WSAGetLastError() << std::endl;
#else
        std::cerr << "[ProfilerSocket] Error creando socket: " << strerror(errno) << std::endl;
#endif
        return false;
    }

    return true;
}

void ProfilerSocket::cleanup() {
    if (socket_ != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(socket_);
#else
        close(socket_);
#endif
        socket_ = INVALID_SOCKET;
    }
}

void ProfilerSocket::reconnectLoop() {
    while (shouldReconnect_ && !stopReconnectThread_) {
        std::this_thread::sleep_for(std::chrono::seconds(reconnectInterval_));

        if (!shouldReconnect_ || stopReconnectThread_) break;

        std::cout << "[ProfilerSocket] Intentando reconectar..." << std::endl;

        // Limpiar socket anterior
        cleanup();

        // Intentar nueva conexión
        if (initializeSocket() && attemptConnection()) {
            connected_ = true;
            shouldReconnect_ = false;
            std::cout << "[ProfilerSocket] Reconexión exitosa" << std::endl;
            break;
        }
    }
}

bool ProfilerSocket::attemptConnection() {
    if (socket_ == INVALID_SOCKET) return false;

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<unsigned short>(port_));

    // Convertir dirección IP
#ifdef _WIN32
    if (inet_pton(AF_INET, host_.c_str(), &serverAddr.sin_addr) != 1) {
        // Si no es una IP válida, intentar resolver como hostname
        struct hostent* he = gethostbyname(host_.c_str());
        if (!he) {
            std::cerr << "[ProfilerSocket] No se pudo resolver " << host_ << std::endl;
            return false;
        }
        std::memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#else
    if (inet_pton(AF_INET, host_.c_str(), &serverAddr.sin_addr) != 1) {
        struct hostent* he = gethostbyname(host_.c_str());
        if (!he) {
            std::cerr << "[ProfilerSocket] No se pudo resolver " << host_ << std::endl;
            return false;
        }
        std::memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#endif

    // Intentar conexión
    if (::connect(socket_, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (!shouldReconnect_) {
            std::cerr << "[ProfilerSocket] Error conectando: " << error << std::endl;
        }
#else
        if (!shouldReconnect_) {
            std::cerr << "[ProfilerSocket] Error conectando: " << strerror(errno) << std::endl;
        }
#endif
        return false;
    }

    return true;
}

#ifdef _WIN32
void ProfilerSocket::initializeWSA() {
    if (!wsaInitialized_) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "[ProfilerSocket] WSAStartup falló: " << result << std::endl;
            return;
        }
        wsaInitialized_ = true;
    }
    wsaRefCount_++;
}

void ProfilerSocket::cleanupWSA() {
    wsaRefCount_--;
    if (wsaRefCount_ <= 0 && wsaInitialized_) {
        WSACleanup();
        wsaInitialized_ = false;
        wsaRefCount_ = 0;
    }
}
#endif