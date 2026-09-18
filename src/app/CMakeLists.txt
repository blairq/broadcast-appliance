# ============================================================================
# src/app — `ba-app` (Proceso A: motor de emisión + UI de operación)
#
# El Proceso A es un ejecutable GUI (sin consola en Windows) que integra
# `ba_core` (dsp + ipc + persist) con la UI de operación (spec §6).
#
# Arquitectura (docs/prompts/primer_prompt.md §6, §14):
#   - `ba_core` es el núcleo: SIN Qt, SIN libobs, testeable por unidad.
#   - `ba-app` (A) usa /MD (CRT dinámica) por consistencia con libobs y Qt.
#   - Todo lo Qt/obs va tras `#ifdef _WIN32` (y, en la app completa, tras
#     `BA_APP_HAVE_QT_OBS`, definido solo cuando Qt 6 + libobs están presentes).
#
# Dos rutas de compilación, elegidas en el configure:
#   1. Windows + Qt 6 + libobs presentes  -> app COMPLETA (QApplication +
#      obs_startup + MainWindow). Se enlaza Qt 6 Widgets + libobs y se define
#      `BA_APP_HAVE_QT_OBS`.
#   2. Cross-compile MinGW de validación (o Linux) sin Qt/libobs -> modo
#      HEADLESS: solo `ba_core`, que ejercita la misma integración dsp/ipc/
#      persist (ver main.cpp, runHeadlessSelfTest). Así el target compila y se
#      valida en cualquier plataforma, y la app real se construye en el
#      entorno con Qt/libobs.
#
# /MD (CRT dinámica) por consistencia con libobs y Qt — hereda del root.
# ============================================================================

# --- Sources ----------------------------------------------------------------
# CONFIGURE_DEPENDS detecta los archivos nuevos sin reconfigurar a mano.
file(GLOB_RECURSE BA_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

# WIN32 en MSVC: subsistema GUI (sin consola). En Linux es un ejecutable normal.
# BroadcastEngine.cpp incluye <obs.h>: sin libobs no puede compilarse, asi que
# se saca de la lista (el resto del Proceso A sigue compilando headless).
if(NOT BA_HAVE_LIBOBS)
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/BroadcastEngine\\.cpp$")
    list(FILTER BA_APP_SOURCES EXCLUDE REGEX ".*/EngineSelfTest\\.cpp$")
endif()

if(MSVC)
    add_executable(ba-app WIN32 ${BA_APP_SOURCES})
else()
    add_executable(ba-app ${BA_APP_SOURCES})
endif()

# MinGW GUI: subsistema GUI (sin consola), igual que el launcher. Sin esto,
# MinGW enlaza como consola y el CRT espera `main` en vez de `WinMain`/
# `wWinMain` (ver el trampoline WinMain->wWinMain en main.cpp).
if(WIN32)
    set_target_properties(ba-app PROPERTIES WIN32_EXECUTABLE ON)
endif()

target_link_libraries(ba-app PRIVATE ba_core)

# --- Motor de emision sobre libobs (spec §6) --------------------------------
# libobs es cross-platform y el motor tambien: cuando `ba::libobs` existe se
# compila `BroadcastEngine` en CUALQUIER plataforma, no solo en Windows. Ese
# acoplamiento (todo lo de obs detras de `_WIN32`) era lo que mantenia el motor
# sin compilar ni ejecutar en ningun lado.
if(BA_HAVE_LIBOBS)
    target_link_libraries(ba-app PRIVATE ba::libobs)
    target_compile_definitions(ba-app PRIVATE BA_HAVE_LIBOBS=1)
    # rpath $ORIGIN: el ejecutable busca sus bibliotecas JUNTO A EL dentro del
    # bundle portable (bin/64bit/), no en el sistema. Es lo que hace que el
    # bundle corra en una maquina sin nada instalado.
    if(NOT WIN32)
        set_target_properties(ba-app PROPERTIES
            BUILD_RPATH "${BA_OBS_LINUX_PREFIX}/lib"
            INSTALL_RPATH "$ORIGIN")
    endif()
else()
    target_compile_definitions(ba-app PRIVATE BA_HAVE_LIBOBS=0)
endif()

# --- Qt 6 + libobs (spec §6) — solo cuando están disponibles ----------------
# En el build real (Windows) se enlaza Qt 6 Widgets + libobs y se compila la
# app completa. En un cross-compile MinGW de validación (o Linux) donde
# Qt/libobs no están, se compila el modo headless (solo ba_core).
#
# Cómo proveer libobs: el build de obs-studio produce `libobs` (lib estática o
# DLL) + headers. Se puede enlazar de dos formas:
#   a) si el build de OBS definió el target `obs`, se enlaza directamente; o
#   b) vía las variables de caché OBS_LIBRARIES + OBS_INCLUDE_DIRS.
if(WIN32)
    # --- Qt 6 vendored (extern/qt6) — build autocontenido/portable ----------
    # El producto portable NO debe depender de un Qt6 del sistema: si hay un
    # SDK de Qt 6.6.3 win64_mingw en extern/qt6 (lo deja `make deps-qt` vía
    # aqtinstall), se busca primero AHÍ. El layout de aqt es
    # extern/qt6/<ver>/mingw_64/{include,lib,bin} con lib/cmake/Qt6/Qt6Config.cmake.
    # Se detecta el prefijo real (el dir que contiene lib/cmake/Qt6/Qt6Config.cmake)
    # y se antepone a CMAKE_PREFIX_PATH, así find_package resuelve el SDK
    # VENDOR en vez de uno del sistema. Si no hay SDK vendored (p. ej. Linux o
    # un build sin `make deps-qt`), CMAKE_PREFIX_PATH no se toca y find_package
    # sigue usando el Qt6 del sistema (o no encuentra ninguno -> headless).
    if(NOT DEFINED BA_QT6_ROOT)
        set(BA_QT6_ROOT "${CMAKE_SOURCE_DIR}/extern/qt6")
    endif()
    if(EXISTS "${BA_QT6_ROOT}/${QT6_VERSION}/mingw_64/lib/cmake/Qt6/Qt6Config.cmake")
        set(BA_QT6_PREFIX "${BA_QT6_ROOT}/${QT6_VERSION}/mingw_64")
        list(PREPEND CMAKE_PREFIX_PATH "${BA_QT6_PREFIX}")
        message(STATUS "ba-app: Qt 6 vendored detectado en ${BA_QT6_PREFIX} (build autocontenido)")
    endif()
    find_package(Qt6 COMPONENTS Widgets QUIET)
    set(BA_APP_QT_OK ${Qt6_FOUND})

    if(TARGET obs)
        set(BA_APP_OBS_OK TRUE)
        set(BA_APP_OBS_LIBS obs)
    elseif(OBS_LIBRARIES AND OBS_INCLUDE_DIRS)
        set(BA_APP_OBS_OK TRUE)
        set(BA_APP_OBS_LIBS ${OBS_LIBRARIES})
    else()
        set(BA_APP_OBS_OK FALSE)
    endif()

    if(BA_APP_QT_OK AND BA_APP_OBS_OK)
        target_link_libraries(ba-app PRIVATE Qt6::Widgets ${BA_APP_OBS_LIBS})
        if(OBS_INCLUDE_DIRS)
            target_include_directories(ba-app PRIVATE ${OBS_INCLUDE_DIRS})
        endif()
        target_compile_definitions(ba-app PRIVATE BA_APP_HAVE_QT_OBS)
        # AUTOMOC para el Q_OBJECT de MainWindow.
        set_target_properties(ba-app PROPERTIES AUTOMOC ON)
        message(STATUS "ba-app: app completa (Qt 6 + libobs) -> QApplication + obs_startup")
    else()
        message(STATUS "ba-app: modo headless (Qt 6 / libobs ausentes) -> solo ba_core")
    endif()

    # --- Validación por compilación del modo completo (compile-proxy) --------
    # El SDK de Qt 6 win64_mingw que se vende en extern/qt6 es autocontenido:
    # trae headers + import libs (libQt6Core.a) + moc.exe, así que el modo
    # COMPLETO de la app (QApplication + MainWindow + AUTOMOC) se puede
    # COMPILEAR bajo el cross-build MinGW usando el moc del SDK vendored.
    #
    # Lo que NO hay en el cross-build es libobs como BUILD INPUT: el runtime de
    # OBS (extern/obs-studio) solo trae DLLs (obs.dll) y el obs.h vendoreado en
    # El obs.h real de libobs trae toda la API (sin limitarse a obs_startup/
    # obs_shutdown). La superficie de libobs que el modo completo de la app usa
    # es exactamente obs_startup + obs_shutdown (main.cpp; MainWindow.cpp solo
    # usa Qt), así que se genera un obs.h shadow con esas dos declaraciones y se
    # compila main.cpp + MainWindow.cpp con BA_APP_HAVE_QT_OBS como target
    # OBJECT (sin enlazar) — mismo criterio que ba-gateway-webrtc-realcheck.
    # Prueba: (1) el cierre de headers de Qt 6 del modo completo compila bajo
    # MinGW; (2) AUTOMOC usa el moc del SDK vendored sobre el Q_OBJECT de
    # MainWindow; (3) la firma de obs_startup/obs_shutdown es la que la app
    # llama. El ENLACE del modo completo (contra obs.dll + las import libs de
    # Qt) queda para el entorno con libobs real (MSVC / build con libobs).
    if(BA_APP_QT_OK)
        set(BA_APP_QT_SHADOW "${CMAKE_CURRENT_BINARY_DIR}/qt-obs-shadow/obs.h")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qt-obs-shadow")
        file(WRITE "${BA_APP_QT_SHADOW}"
"// obs.h shadow — solo para el compile-proxy del modo completo de ba-app.
//
// Declara el ÚNICO subconjunto de la API C de libobs que el modo completo de
// la app usa (main.cpp): obs_startup + obs_shutdown. No es el header real de
// libobs: existe para que el cross-build MinGW COMPILE el modo completo sin
// el SDK de libobs presente (los símbolos obs_* se resuelven en el enlace
// contra obs.dll).
#pragma once

#ifdef __cplusplus
extern \"C\" {
#endif

bool obs_startup(const char *module_name, const char *locale,
                 const char *plugin_config, bool is_hwnd, void *private_data);

void obs_shutdown(void);

#ifdef __cplusplus
}
#endif
")
        # --- AUTOMOC: cómo ejecutar el moc del SDK vendored -------------------
        # El moc del SDK vendored (extern/qt6/.../bin/moc.exe) es un PE de
        # Windows. OJO: WIN32 es la plataforma OBJETIVO (siempre TRUE en el
        # cross-build MinGW), no el host: lo que decide si el PE se ejecuta es
        # CMAKE_HOST_SYSTEM_NAME. Si el HOST no es Windows (cross desde Linux/
        # mac), el PE no corre directo: hay que pasarlo por wine. Si no hay
        # wine, se SALTA el compile-proxy (no se crea el target) en vez de
        # romper el configure. En Windows nativo el moc.exe se ejecuta directo,
        # sin shim ni wine.
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            # Host Windows: moc.exe nativo, AUTOMOC lo usa directo.
            # EXCLUDE_FROM_ALL: es un compile-proxy opcional, no parte del
            # producto; no debe entrar en el build por defecto (ni romper la
            # puerta de artefactos si el toolchain cross es parcial).
            add_library(ba-app-qt-realcheck EXCLUDE_FROM_ALL OBJECT
                "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/MainWindow.cpp")
            target_compile_definitions(ba-app-qt-realcheck PRIVATE BA_APP_HAVE_QT_OBS)
            # El shadow obs.h ANTES de cualquier otra ruta: gana sobre el obs.h
            # real de libobs (el proxy solo necesita dos símbolos, sin obs_startup).
            target_include_directories(ba-app-qt-realcheck BEFORE PRIVATE
                "${CMAKE_CURRENT_BINARY_DIR}/qt-obs-shadow"
                "${CMAKE_CURRENT_SOURCE_DIR}")
            # Qt 6 del SDK vendored: AUTOMOC usa el moc.exe de extern/qt6.
            target_link_libraries(ba-app-qt-realcheck PRIVATE Qt6::Widgets)
            set_target_properties(ba-app-qt-realcheck PROPERTIES AUTOMOC ON)
            message(STATUS "ba-app: realcheck de modo completo (Qt 6 vendored + obs.h shadow) -> compile-proxy")
        else()
            # Host no-Windows (cross): el moc.exe vendored es un PE -> wine.
            find_program(BA_WINE_EXE NAMES wine wine64)
            if(BA_WINE_EXE)
                # Shim POSIX que envuelve el moc.exe vendored bajo wine: AUTOMOC
                # lo llama en vez de ejecutar el PE directamente. La propiedad de
                # target AUTOMOC_EXECUTABLE hace que CMake SALTERA la detección
                # automática del moc y el sanity-test de configure (`moc -h`) que
                # fallaba al ejecutar el PE en el host Linux.
                set(BA_MOC_SHIM "${CMAKE_CURRENT_BINARY_DIR}/qt-obs-shadow/run-moc.sh")
                file(WRITE "${BA_MOC_SHIM}"
"#!/bin/sh
exec \"${BA_WINE_EXE}\" \"${BA_QT6_PREFIX}/bin/moc.exe\" \"$@\"
")
                execute_process(COMMAND chmod +x "${BA_MOC_SHIM}")
                # EXCLUDE_FROM_ALL: compile-proxy opcional (igual que la rama
                # Windows): no entra en el build por defecto.
                add_library(ba-app-qt-realcheck EXCLUDE_FROM_ALL OBJECT
                    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/MainWindow.cpp")
                target_compile_definitions(ba-app-qt-realcheck PRIVATE BA_APP_HAVE_QT_OBS)
                # El shadow obs.h ANTES de cualquier otra ruta: gana sobre el
                # obs.h real de libobs (el proxy solo necesita dos símbolos, sin
                # obs_startup).
                target_include_directories(ba-app-qt-realcheck BEFORE PRIVATE
                    "${CMAKE_CURRENT_BINARY_DIR}/qt-obs-shadow"
                    "${CMAKE_CURRENT_SOURCE_DIR}")
                # Qt 6 del SDK vendored: AUTOMOC usa el moc.exe de extern/qt6.
                target_link_libraries(ba-app-qt-realcheck PRIVATE Qt6::Widgets)
                set_target_properties(ba-app-qt-realcheck PROPERTIES
                    AUTOMOC ON
                    AUTOMOC_EXECUTABLE "${BA_MOC_SHIM}")
                message(STATUS "ba-app: realcheck de modo completo (Qt 6 vendored + obs.h shadow, moc vía wine) -> compile-proxy")
            else()
                message(STATUS "ba-app: realcheck desactivado (Qt 6 presente pero sin wine para ejecutar moc.exe)")
            endif()
        endif()
    else()
        message(STATUS "ba-app: realcheck desactivado (Qt 6 ausente)")
    endif()
endif()

# --- Instalación ------------------------------------------------------------
# Proceso A va a bin/64bit/ (spec §5.1).
install(TARGETS ba-app
    RUNTIME DESTINATION bin/64bit
)
