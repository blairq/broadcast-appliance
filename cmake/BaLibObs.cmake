# ============================================================================
# cmake/BaLibObs.cmake — resuelve libobs de forma PORTABLE en Linux y Windows
#
# Proposito:
#   Define el target importado `ba::libobs` y la variable `BA_HAVE_LIBOBS`.
#   libobs es cross-platform: el motor de emision no necesita ningun `#ifdef`
#   para usarlo. Lo que cambia entre plataformas es DE DONDE sale la biblioteca,
#   y eso se resuelve aca de una vez.
#
#   Regla del proyecto (README, "Reglas del entorno de desarrollo"): nada se
#   instala globalmente y el bundle es autocontenido. Por eso libobs NO se busca
#   en el sistema: sale de `extern/`, igual que el resto de los assets de
#   terceros, y el bundle se lleva su copia (ver `BA_LIBOBS_RUNTIME_LIBS`).
#
# De donde sale en cada plataforma:
#   - Linux: `extern/obs-linux`, el prefijo donde se INSTALA libobs compilada
#     desde fuente (tag OBS_STUDIO_TAG de versions.cmake). Lo produce
#     `make deps-obs-linux`.
#   - Windows: headers del arbol de fuentes vendoreado (`extern/obs-src/libobs`,
#     son independientes de plataforma) y la biblioteca del runtime prebuilt
#     (`extern/obs-studio/bin/64bit/obs.dll`). MinGW no trae import lib para esa
#     DLL, asi que se genera una con `dlltool` a partir de la propia DLL.
#
# Si libobs no esta, NO es un error: `BA_HAVE_LIBOBS` queda en 0 y los targets
# que la usan se compilan en modo headless. Asi el arbol sigue compilando en un
# entorno sin las dependencias, que es lo que permite validar el resto.
# ============================================================================

set(BA_HAVE_LIBOBS 0)

# Raiz del arbol de fuentes vendoreado (headers, validos en toda plataforma).
set(BA_OBS_SRC_DIR "${CMAKE_SOURCE_DIR}/extern/obs-src" CACHE PATH
    "Arbol de fuentes de obs-studio vendoreado (headers de libobs)")

if(WIN32)
    # --- Windows: headers del arbol de fuentes + DLL del runtime prebuilt ----
    set(BA_OBS_WIN_RUNTIME "${CMAKE_SOURCE_DIR}/extern/obs-studio" CACHE PATH
        "Runtime portable de OBS para Windows (extern/obs-studio)")
    set(_ba_obs_dll "${BA_OBS_WIN_RUNTIME}/bin/64bit/obs.dll")

    if(EXISTS "${BA_OBS_SRC_DIR}/libobs/obs.h" AND EXISTS "${_ba_obs_dll}")
        # `obs-config.h`/`obsconfig.h` los GENERA el build de OBS; en el arbol
        # de fuentes crudo no existen. Se busca la copia que dejo el build de
        # Linux (los valores relevantes para compilar contra la API son los
        # mismos) y, si no esta, se avisa y se sigue sin libobs.
        set(_ba_obs_gen "${CMAKE_SOURCE_DIR}/extern/obs-linux/include/obs")
        if(NOT EXISTS "${_ba_obs_gen}/obsconfig.h")
            message(STATUS "ba-libobs: faltan los headers generados de OBS; se compila sin libobs")
        else()
            # Import lib desde la DLL: MinGW puede enlazar contra una DLL via
            # una lib de importacion generada con dlltool. Se genera una sola
            # vez en el arbol de build (no se toca extern/).
            # La import lib se genera a partir de la TABLA DE EXPORTACIONES de
            # la propia DLL: se listan los simbolos con objdump, se escribe un
            # .def y dlltool arma la biblioteca. (Un .def vacio produce una
            # import lib sin simbolos y el enlace falla con "referencia sin
            # definir" sobre funciones que la DLL si exporta.)
            set(_ba_obs_impdir "${CMAKE_BINARY_DIR}/libobs-import")
            set(_ba_obs_implib "${_ba_obs_impdir}/libobs.dll.a")
            if(NOT EXISTS "${_ba_obs_implib}")
                find_program(BA_DLLTOOL NAMES x86_64-w64-mingw32-dlltool dlltool)
                find_program(BA_OBJDUMP NAMES x86_64-w64-mingw32-objdump objdump)
                if(BA_DLLTOOL AND BA_OBJDUMP)
                    file(MAKE_DIRECTORY "${_ba_obs_impdir}")
                    execute_process(COMMAND "${BA_OBJDUMP}" -p "${_ba_obs_dll}"
                                    OUTPUT_VARIABLE _ba_obs_dump ERROR_QUIET)
                    # Las lineas de export tienen la forma "  [ 939] nombre".
                    string(REGEX MATCHALL "\n\t\\[[ 0-9]+\\] [A-Za-z_][A-Za-z0-9_]*"
                           _ba_obs_exports "${_ba_obs_dump}")
                    set(_ba_def "EXPORTS\n")
                    foreach(_line IN LISTS _ba_obs_exports)
                        string(REGEX REPLACE "^.*\\] " "" _sym "${_line}")
                        string(APPEND _ba_def "${_sym}\n")
                    endforeach()
                    file(WRITE "${_ba_obs_impdir}/obs.def" "${_ba_def}")
                    execute_process(
                        COMMAND "${BA_DLLTOOL}" --input-def "${_ba_obs_impdir}/obs.def"
                                --dllname obs.dll --output-lib "${_ba_obs_implib}"
                        RESULT_VARIABLE _ba_dlltool_rc OUTPUT_QUIET ERROR_QUIET)
                    if(NOT _ba_dlltool_rc EQUAL 0)
                        message(STATUS "ba-libobs: dlltool fallo; el cross-build no enlazara libobs")
                    endif()
                endif()
            endif()

            add_library(ba::libobs SHARED IMPORTED GLOBAL)
            set_target_properties(ba::libobs PROPERTIES
                IMPORTED_LOCATION "${_ba_obs_dll}"
                IMPORTED_IMPLIB "${_ba_obs_implib}"
                INTERFACE_INCLUDE_DIRECTORIES
                    "${BA_OBS_SRC_DIR}/libobs;${_ba_obs_gen}")
            set(BA_HAVE_LIBOBS 1)
            # Runtime que el bundle debe llevarse (spec §5.1, bin/64bit/).
            file(GLOB BA_LIBOBS_RUNTIME_LIBS "${BA_OBS_WIN_RUNTIME}/bin/64bit/*.dll")
            # Helper de grabacion: obs-ffmpeg lo busca JUNTO al ejecutable
            # (os_get_executable_path_ptr); sin el, la grabacion no arranca.
            file(GLOB BA_LIBOBS_RUNTIME_PROGRAMS "${BA_OBS_WIN_RUNTIME}/bin/64bit/obs-ffmpeg-mux.exe")
            set(BA_LIBOBS_PLUGIN_DIR "${BA_OBS_WIN_RUNTIME}/obs-plugins/64bit")
            set(BA_LIBOBS_DATA_DIR "${BA_OBS_WIN_RUNTIME}/data")
        endif()
    endif()
else()
    # --- Linux: prefijo de instalacion de libobs compilada desde fuente -----
    set(BA_OBS_LINUX_PREFIX "${CMAKE_SOURCE_DIR}/extern/obs-linux" CACHE PATH
        "Prefijo donde se instalo libobs compilada desde fuente")

    find_library(BA_OBS_LIBRARY
        NAMES obs
        PATHS "${BA_OBS_LINUX_PREFIX}/lib"
        NO_DEFAULT_PATH)          # NO del sistema: solo el vendoreado.
    find_path(BA_OBS_INCLUDE_DIR
        NAMES obs.h
        PATHS "${BA_OBS_LINUX_PREFIX}/include/obs"
        NO_DEFAULT_PATH)

    if(BA_OBS_LIBRARY AND BA_OBS_INCLUDE_DIR)
        add_library(ba::libobs SHARED IMPORTED GLOBAL)
        set_target_properties(ba::libobs PROPERTIES
            IMPORTED_LOCATION "${BA_OBS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BA_OBS_INCLUDE_DIR}")
        set(BA_HAVE_LIBOBS 1)
        # Todo lo que el bundle se lleva: libobs, el modulo grafico y los
        # plugins de encoder/salida/captura.
        file(GLOB BA_LIBOBS_RUNTIME_LIBS "${BA_OBS_LINUX_PREFIX}/lib/*.so*")
        # Helper de grabacion: obs-ffmpeg lo busca JUNTO al ejecutable
        # (os_get_executable_path_ptr); sin el, la grabacion no arranca.
        file(GLOB BA_LIBOBS_RUNTIME_PROGRAMS "${BA_OBS_LINUX_PREFIX}/bin/obs-ffmpeg-mux")
        set(BA_LIBOBS_PLUGIN_DIR "${BA_OBS_LINUX_PREFIX}/lib/obs-plugins")
        set(BA_LIBOBS_DATA_DIR "${BA_OBS_LINUX_PREFIX}/share/obs")
    endif()
endif()

if(BA_HAVE_LIBOBS)
    message(STATUS "ba-libobs: ENCONTRADA (el motor de emision se compila)")
else()
    message(STATUS "ba-libobs: ausente; ba-app y ba-sources se compilan headless")
    message(STATUS "           Linux: 'make deps-obs-linux' compila libobs en extern/obs-linux")
endif()
