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

# ba_make_implib(<dll> <out-var>): biblioteca de importacion para enlazar
# contra una DLL que no trae la suya (OBS, FFmpeg del zip de OBS). Se arma
# desde la TABLA DE EXPORTACIONES de la propia DLL (objdump -> .def ->
# dlltool). Con MSVC/clang-cl sale <nombre>.lib (llvm-dlltool); con MinGW,
# lib<nombre>.dll.a. Se genera una sola vez en el arbol de build.
function(ba_make_implib dll outvar)
    get_filename_component(_name "${dll}" NAME_WE)
    get_filename_component(_file "${dll}" NAME)
    set(_dir "${CMAKE_BINARY_DIR}/win-import")
    if(MSVC)
        set(_lib "${_dir}/${_name}.lib")
        find_program(BA_DLLTOOL NAMES llvm-dlltool HINTS "${BA_WIN_LLVM}")
        find_program(BA_OBJDUMP NAMES llvm-objdump HINTS "${BA_WIN_LLVM}")
    else()
        set(_lib "${_dir}/lib${_name}.dll.a")
        find_program(BA_DLLTOOL NAMES x86_64-w64-mingw32-dlltool dlltool)
        find_program(BA_OBJDUMP NAMES x86_64-w64-mingw32-objdump objdump)
    endif()
    if(NOT EXISTS "${_lib}")
        if(NOT BA_DLLTOOL OR NOT BA_OBJDUMP)
            message(STATUS "ba_make_implib: sin dlltool/objdump para ${_file}")
            set(${outvar} "" PARENT_SCOPE)
            return()
        endif()
        file(MAKE_DIRECTORY "${_dir}")
        execute_process(COMMAND "${BA_OBJDUMP}" -p "${dll}" OUTPUT_VARIABLE _dump ERROR_QUIET)
        # GNU objdump: "\t[ 939] nombre"; llvm-objdump: "   939  0x76500  nombre".
        string(REGEX MATCHALL "\n\t\\[[ 0-9]+\\] [A-Za-z_][A-Za-z0-9_@]*|\n +[0-9]+ +0x[0-9a-f]+ +[A-Za-z_][A-Za-z0-9_@]*"
               _exports "${_dump}")
        set(_def "LIBRARY ${_file}\nEXPORTS\n")
        foreach(_line IN LISTS _exports)
            string(REGEX REPLACE "^.*[] ]([A-Za-z_][A-Za-z0-9_@]*)$" "\\1" _sym "${_line}")
            string(APPEND _def "${_sym}\n")
        endforeach()
        file(WRITE "${_dir}/${_name}.def" "${_def}")
        execute_process(
            COMMAND "${BA_DLLTOOL}" -m i386:x86-64 --input-def "${_dir}/${_name}.def"
                    --dllname "${_file}" --output-lib "${_lib}"
            RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rc EQUAL 0 OR NOT EXISTS "${_lib}")
            message(STATUS "ba_make_implib: dlltool fallo para ${_file}")
            set(${outvar} "" PARENT_SCOPE)
            return()
        endif()
    endif()
    set(${outvar} "${_lib}" PARENT_SCOPE)
endfunction()

# Raiz del arbol de fuentes vendoreado (headers, validos en toda plataforma).
set(BA_OBS_SRC_DIR "${CMAKE_SOURCE_DIR}/extern/obs-src" CACHE PATH
    "Arbol de fuentes de obs-studio vendoreado (headers de libobs)")

if(WIN32)
    # --- Windows: headers del arbol de fuentes + DLL del runtime prebuilt ----
    set(BA_OBS_WIN_RUNTIME "${CMAKE_SOURCE_DIR}/extern/obs-studio" CACHE PATH
        "Runtime portable de OBS para Windows (extern/obs-studio)")
    set(_ba_obs_dll "${BA_OBS_WIN_RUNTIME}/bin/64bit/obs.dll")

    if(EXISTS "${BA_OBS_SRC_DIR}/libobs/obs.h" AND EXISTS "${_ba_obs_dll}")
        # `obsconfig.h` lo GENERA el build de OBS; en el arbol de fuentes crudo
        # no existe. Se genera uno propio con los valores del layout de Windows
        # (los mismos que usa el obs.dll del zip): no depende de otro build.
        set(_ba_obs_gen "${CMAKE_BINARY_DIR}/libobs-gen")
        file(WRITE "${_ba_obs_gen}/obsconfig.h"
             "#pragma once\n#define OBS_DATA_PATH \"../../data\"\n"
             "#define OBS_PLUGIN_PATH \"../../obs-plugins/64bit\"\n"
             "#define OBS_PLUGIN_DESTINATION \"obs-plugins/64bit\"\n"
             "#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n")
        # clang-cl se presenta como MSVC pero no trae el intrinseco _udiv128
        # que usa util/util_uint64.h: se lo da este header, forzado en todo lo
        # que enlaza libobs (division 128/64 con la instruccion div).
        file(WRITE "${_ba_obs_gen}/ba_obs_compat.h" [=[
#pragma once
#if defined(__clang__) && defined(_M_X64) && !__has_builtin(_udiv128)
static __inline unsigned __int64 _udiv128(unsigned __int64 high, unsigned __int64 low,
                                          unsigned __int64 div, unsigned __int64* rem) {
    unsigned __int64 q, r;
    __asm__("divq %4" : "=a"(q), "=d"(r) : "a"(low), "d"(high), "rm"(div));
    *rem = r;
    return q;
}
#endif
]=])
        set(_ba_obs_implib "")
        ba_make_implib("${_ba_obs_dll}" _ba_obs_implib)
        if(_ba_obs_implib)

            add_library(ba::libobs SHARED IMPORTED GLOBAL)
            set_target_properties(ba::libobs PROPERTIES
                IMPORTED_LOCATION "${_ba_obs_dll}"
                IMPORTED_IMPLIB "${_ba_obs_implib}"
                INTERFACE_INCLUDE_DIRECTORIES
                    "${BA_OBS_SRC_DIR}/libobs;${_ba_obs_gen}")
            if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
                set_property(TARGET ba::libobs PROPERTY INTERFACE_COMPILE_OPTIONS
                             "/FI${_ba_obs_gen}/ba_obs_compat.h")
            endif()
            set(BA_HAVE_LIBOBS 1)
            # Runtime que el bundle debe llevarse (spec §5.1, bin/64bit/).
            file(GLOB BA_LIBOBS_RUNTIME_LIBS "${BA_OBS_WIN_RUNTIME}/bin/64bit/*.dll")
            # Lo de la interfaz de OBS no viaja: su Qt (A trae el suyo), el
            # frontend, scripting y la libreria de su plugin WebRTC.
            list(FILTER BA_LIBOBS_RUNTIME_LIBS EXCLUDE REGEX
                 "/(Qt6[^/]*|obs-frontend-api|obs-scripting|lua51|datachannel)\\.dll$")
            # Plugins que dependen de la interfaz de OBS (Qt, frontend, CEF) o
            # de programas externos (VLC): fuera del bundle, con sus datos.
            set(BA_LIBOBS_PLUGIN_EXCLUDE aja-output-ui decklink-captions decklink-output-ui
                frontend-tools obs-browser obs-vst obs-webrtc obs-websocket vlc-video)
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
            INTERFACE_INCLUDE_DIRECTORIES "${BA_OBS_INCLUDE_DIR}"
            # Al enlazar, las dependencias de libobs (FFmpeg -> libva, ...) se
            # resuelven con las vendoreadas, nunca con las del sistema.
            INTERFACE_LINK_OPTIONS "-Wl,-rpath-link,${CMAKE_SOURCE_DIR}/extern/obs-deps-linux/lib")
        set(BA_HAVE_LIBOBS 1)
        # Todo lo que el bundle se lleva: libobs, el modulo grafico y los
        # plugins de encoder/salida/captura.
        file(GLOB BA_LIBOBS_RUNTIME_LIBS "${BA_OBS_LINUX_PREFIX}/lib/*.so*")
        # Sus dependencias compiladas desde fuente (tools/deps/obs-deps-linux.sh):
        # FFmpeg, x264, libva (cargador), libv4l2. Van junto a libobs: el bundle
        # no usa las del sistema.
        set(BA_OBS_DEPS_PREFIX "${CMAKE_SOURCE_DIR}/extern/obs-deps-linux")
        # + lo que piden los plugins LSP (CLAP): cairo, pixman, libpng, libsndfile.
        foreach(_pat avcodec avdevice avfilter avformat avutil swresample swscale x264 va va-drm v4l2 v4lconvert
                     cairo pixman-1 png16 sndfile)
            file(GLOB _dep "${BA_OBS_DEPS_PREFIX}/lib/lib${_pat}.so.*")
            list(APPEND BA_LIBOBS_RUNTIME_LIBS ${_dep})
        endforeach()
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
