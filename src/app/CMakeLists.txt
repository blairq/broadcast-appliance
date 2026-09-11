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
endif()

# --- Instalación ------------------------------------------------------------
# Proceso A va a bin/64bit/ (spec §5.1).
install(TARGETS ba-app
    RUNTIME DESTINATION bin/64bit
)
