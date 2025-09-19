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
# 1) Librería de instrumentación: profiler
#    - Windows: enlaza Winsock (ws2_32)
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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Charts)

add_executable(untitled2
        main.cpp
        # Para que el IDE muestre estos headers en el target (no se compilan por sí solos):
        profiler/profiler.h
        profiler/profiler_socket.h
)

target_link_libraries(untitled2 PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Charts
        # <- descomenta si la UI llama funciones de la lib
        # profiler
)

target_include_directories(untitled2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(untitled2 PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

# ============================================
# 3A) Copiar DLLs de Qt junto al .exe (opción manual)
#     Si usas MinGW, aún podrías necesitar libstdc++/libgcc/libwinpthread
# ============================================
#if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
#    set(DEBUG_SUFFIX "")
#    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
#        set(DEBUG_SUFFIX "d")
#    endif()

    # Ajusta a tu ruta real de Qt
#    set(QT_INSTALL_PATH "C:/Qt/6.9.2/mingw_64")

    # Plugin de plataforma (qwindows)
#    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
#        add_custom_command(TARGET untitled2 POST_BUILD
#                COMMAND ${CMAKE_COMMAND} -E make_directory
#                "$<TARGET_FILE_DIR:untitled2>/plugins/platforms/"
#                COMMAND ${CMAKE_COMMAND} -E copy
#                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
#                "$<TARGET_FILE_DIR:untitled2>/plugins/platforms/"
#        )
 #   endif()

    # DLLs principales de Qt usadas por el target
 #   foreach(QT_LIB Core Gui Widgets Network Charts)
 #       add_custom_command(TARGET untitled2 POST_BUILD
 #               COMMAND ${CMAKE_COMMAND} -E copy
  #              "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
   #             "$<TARGET_FILE_DIR:untitled2>"
    #    )
#    endforeach()

    # (Opcional) Runtimes de MinGW si hiciera falta:
    # add_custom_command(TARGET untitled2 POST_BUILD
    #     COMMAND ${CMAKE_COMMAND} -E copy_if_different
    #             "C:/Qt/Tools/mingw1310_64/bin/libstdc++-6.dll"
    #             "C:/Qt/Tools/mingw1310_64/bin/libgcc_s_seh-1.dll"
    #             "C:/Qt/Tools/mingw1310_64/bin/libwinpthread-1.dll"
    #             "$<TARGET_FILE_DIR:untitled2>"
    # )
#endif()

# ============================================
# 3B) Alternativa recomendada: usar windeployqt
#     (Descomenta este bloque y puedes comentar el 3A)
# ============================================
 if (WIN32)
     set(QT_INSTALL_PATH "C:/Qt/6.9.2/mingw_64")
     add_custom_command(TARGET untitled2 POST_BUILD
         COMMAND "${QT_INSTALL_PATH}/bin/windeployqt.exe" "$<TARGET_FILE:untitled2>"
         COMMENT "Empaquetando dependencias Qt con windeployqt"
     )
 endif()

# ============================================
# 4) (Opcional) Demo que linkea profiler
# ============================================
# if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/demo.cpp")
#     add_executable(demo demo/demo.cpp)
#     target_link_libraries(demo PRIVATE profiler)
#     target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/profiler)
# endif()

