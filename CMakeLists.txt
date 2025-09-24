cmake_minimum_required(VERSION 3.25)
project(untitled2_all LANGUAGES CXX)

# ===== Configuración C++ =====
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Autogeneración de meta-objetos/uis/recursos para Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ============================================
# Configuración de Qt6
# ============================================
set(QT_INSTALL_PATH "C:/Qt/6.9.2/mingw_64")
set(CMAKE_PREFIX_PATH ${QT_INSTALL_PATH} ${CMAKE_PREFIX_PATH})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Charts)
message(STATUS "Qt6 encontrado en: ${Qt6_DIR}")

# ============================================
# 1) Librería de instrumentación: profiler
# ============================================
add_library(profiler STATIC
        profiler/profiler.cpp
        profiler/profiler_socket.cpp
)

target_include_directories(profiler PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/profiler
)

target_compile_definitions(profiler PUBLIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

if (WIN32)
    target_link_libraries(profiler PUBLIC ws2_32)
endif()

# ============================================
# 2) Ejecutable de la UI (Qt)
# ============================================
add_executable(untitled2
        main.cpp
        # Headers para mostrar en el IDE
        profiler/profiler.h
        profiler/profiler_socket.h
)

target_link_libraries(untitled2 PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Charts
)

target_include_directories(untitled2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(untitled2 PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

# ============================================
# 3) windeployqt para copiar dependencias de Qt
# ============================================
if (WIN32 AND EXISTS "${QT_INSTALL_PATH}/bin/windeployqt.exe")
    add_custom_command(TARGET untitled2 POST_BUILD
            COMMAND "${QT_INSTALL_PATH}/bin/windeployqt.exe" "$<TARGET_FILE:untitled2>"
            COMMENT "Empaquetando dependencias Qt con windeployqt"
    )
endif()

# ============================================
# 4) Demo ejecutables (opcional)
# ============================================
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/demo.cpp")
    add_executable(demo demo/demo.cpp)
    target_link_libraries(demo PRIVATE profiler)
    target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/profiler)

    target_compile_definitions(demo PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    message(STATUS "Demo principal creado")
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/simple_demo.cpp")
    add_executable(simple_demo demo/simple_demo.cpp)
    target_link_libraries(simple_demo PRIVATE profiler)
    target_include_directories(simple_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/profiler)

    target_compile_definitions(simple_demo PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    message(STATUS "Demo simple creado")
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_connection.cpp")
    add_executable(test_connection test_connection.cpp)

    if (WIN32)
        target_link_libraries(test_connection PRIVATE ws2_32 wsock32)
    endif()

    target_compile_definitions(test_connection PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    message(STATUS "Test de conexión creado")
endif()
