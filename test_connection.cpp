#include <iostream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

int main() {
    std::cout << "=== TEST DE CONEXIÓN TCP ===" << std::endl;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup falló" << std::endl;
        return 1;
    }
#endif

    // Crear socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error creando socket" << std::endl;
        return 1;
    }

    // Configurar dirección
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(43210);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    std::cout << "Intentando conectar a 127.0.0.1:43210..." << std::endl;

    // Intentar conexión
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error conectando - ¿Está la interfaz Qt ejecutándose?" << std::endl;
#ifdef _WIN32
        std::cerr << "Error code: " << WSAGetLastError() << std::endl;
#endif
        return 1;
    }

    std::cout << "✓ Conexión exitosa!" << std::endl;

    // Enviar mensaje JSON de prueba
    std::string testMsg = R"({"evento":"asignacion","direccion":"0x12345678","tamano":1024,"tipo":"test","archivo":"test.cpp","linea":42,"timestamp":1234567890})" "\n";

    if (send(sock, testMsg.c_str(), testMsg.length(), 0) < 0) {
        std::cerr << "Error enviando datos" << std::endl;
        return 1;
    }

    std::cout << "✓ Mensaje enviado: " << testMsg << std::endl;

    // Esperar un poco
    std::cout << "Esperando 2 segundos..." << std::endl;
#ifdef _WIN32
    Sleep(2000);
#else
    sleep(2);
#endif

    // Cerrar conexión
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    std::cout << "✓ Test completado - revisa la interfaz Qt" << std::endl;
    return 0;
}