#pragma once // hace que el archivo se compile una unica vez
#include <cstddef>
#include <cstdint>

namespace profiler {
    void init();//para inicializar el programa
    void snapshot();
    void shutdown(); //cierra de forma ordenada la comunicación

    //las siguientes funciones ayudan a los operadores a registrar los cambios de memoria
    void on_alloc(void* p, size_t n, const char* file, int line, const char* type);
    void on_free(void* p);

    //para la metrica de tiempo
    uint64_t now_ms();

    //nos va a ayudar a saber de que función y en cual linea estamos analizando la memoria
    #define NEW(T, ...)( new (__FILE__, __LINE__, #T) T(__VA_ARGS__) )


}

//sobrecargas de los operadores
void *operator new[](size_t size);

void* operator new(size_t size);
void operator delete[](void *p) noexcept; //noexcept ayuda a que la función no tenga excepciones y en caso de tenerla entonces la termina
void operator delete (void* p) noexcept;

//esto es utilizado por NEW para saber la linea, archivo y tipo que se analiza
void* operator new(size_t n, const char* file, int line, const char* type);
