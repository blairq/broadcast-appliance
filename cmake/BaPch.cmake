# ============================================================================
# cmake/BaPch.cmake — headers precompilados, SOLO con lo de afuera
#
# Lo más caro del build no es nuestro código: es leer una y otra vez lo externo
# que nunca cambia. Medido (2026-11-04): <nlohmann/json.hpp> 0,84 s en 827 de 1534
# objetos; Qt (QObject, QVariant, QJson*, QTimer…) ~0,8 s en 200–480; la base de la
# estándar (<string>, <mutex>, <algorithm>…) ~0,4 s en ~1300.
#
# Cómo:
#   - Un PCH lo arma SOLO un «donante»: una biblioteca chica (ba_pch_<sabor>) con un
#     .cpp vacío, que enlaza lo de afuera que precompila y nada más. Los demás
#     objetivos lo REUSAN (REUSE_FROM): un PCH por sabor y no uno por biblioteca.
#   - Solo cabeceras externas. NUNCA una nuestra: cambian, y tocar una invalidaría
#     el PCH y, con él, todo lo que lo usa.
#   - Los sabores (lo que de verdad usa cada parte, contado en compile_commands):
#       base   la estándar + nlohmann/json            (ba_core, gateway, tests sin Qt)
#       gst    base + <gst/gst.h>                      (ba_motor: 21 de sus 23 .cpp)
#       qt     base + lo de QtCore que usa la app      (ba_app_*, programas y tests con Qt)
#     El donante de qt se crea en src/app (los objetivos importados de Qt no se ven
#     fuera de esa carpeta); los otros, al final del CMakeLists raíz.
#   - Reusar un PCH exige las mismas banderas que el donante (clang rechaza el PCH
#     si una definición del PCH falta o cambia; gcc lo ignora con -Winvalid-pch).
#     Por eso los donantes no tienen definiciones propias: las suyas son las de lo
#     que enlazan (json, Qt, GStreamer), y un objetivo reusa solo si enlaza lo mismo
#     (las definiciones DE MÁS, nuestras BA_*, no tocan el PCH). ba_pch_aplicar()
#     revisa cada objetivo y, si algo no coincide, lo deja SIN PCH (compila como
#     antes), nunca lo fuerza:
#       - POSITION_INDEPENDENT_CODE, MSVC_RUNTIME_LIBRARY y CXX_STANDARD iguales al
#         donante (ba_core es PIC: tiene su donante base_pic);
#       - opciones de compilación propias o heredadas que no sean de avisos (-W…, /w…);
#       - definiciones que cambian cabeceras de afuera (_USE_MATH_DEFINES, _GNU_SOURCE,
#         UNICODE, JSON_*, QT_NO_*…): el PCH ya las leyó sin eso.
#     Por archivo: los .c, .m, .mm y los que tienen opciones propias (-fno-rtti, /GR-:
#     el gateway con libwebrtc) o están fuera de src/ (terceros: qrcodegen) van sin PCH.
#   - ccache: el donante compila SIN ccache. El .pch lleva las rutas absolutas de su
#     árbol; con base_dir, ccache le daría a un worktree el .pch de otro árbol (con las
#     rutas de aquel). Armar un PCH son segundos; lo que importa es que los que lo usan
#     sí se cachean (ccache hashea el .pch: CMakeLists raíz, sloppiness pch_defines…).
#
#   BA_PCH=OFF: sin headers precompilados (para ver si a un .cpp le falta un include
#   que el PCH le tapaba; los headers los vigila test_headers_solos, siempre sin PCH).
# ============================================================================

option(BA_PCH "Headers precompilados con lo de afuera (std, nlohmann/json, Qt, GStreamer)" ON)

# --- Las cabeceras de cada sabor ---------------------------------------------------
# La estándar: lo que incluye más de un tercio de los .cpp de las bibliotecas grandes.
set(BA_PCH_STD
    <algorithm> <array> <atomic> <chrono> <condition_variable> <cstddef> <cstdint>
    <cstring> <deque> <filesystem> <functional> <initializer_list> <map> <memory>
    <mutex> <optional> <set> <string> <string_view> <thread> <tuple> <type_traits>
    <unordered_map> <utility> <vector>)
set(BA_PCH_JSON <nlohmann/json.hpp>)
# QtCore: lo que usan las áreas de la app (ba_app_ui, ba_app_mcp, sala, equipos…).
set(BA_PCH_QT
    <QAbstractListModel> <QByteArray> <QCoreApplication> <QDateTime> <QHash>
    <QJsonArray> <QJsonDocument> <QJsonObject> <QJsonValue> <QList> <QMap> <QMetaObject>
    <QObject> <QPointer> <QSet> <QString> <QStringList> <QTimer> <QUrl> <QVariant>
    <QVariantList> <QVariantMap>)
set(BA_PCH_GST <gst/gst.h>)

# Opciones que no cambian el PCH (avisos). Lo demás, distinto del donante: sin PCH.
set(_BA_PCH_OPCION_INOCUA "^(SHELL:)?([-/]W|[-/]w|-Wno-|/wd|/we|/wo)")
# Definiciones que cambian lo que dicen las cabeceras de afuera.
set(_BA_PCH_DEF_PELIGROSA
    "(^|[:;>])(_[A-Z]|JSON_|QT_NO_|QT_USE_|QT_DISABLE_|QT_STRICT|QT_RESTRICTED|QT_DEPRECATED|UNICODE|NOMINMAX|WIN32_LEAN|STRICT[=;>]|STRICT$|GLIB_|G_|GST_)")

# ba_pch(<target> <sabor>): el sabor de un objetivo cuando no es el de siempre (qt si
# enlaza Qt, si no base). ba_motor usa gst.
function(ba_pch target sabor)
    set_property(TARGET ${target} PROPERTY BA_PCH_SABOR ${sabor})
endfunction()

# ba_pch_hilos(<variable>): Threads::Threads fuera de Windows (lo enlaza ba_core). Va en
# los donantes: en Linux trae -pthread, y el PCH tiene que verlo igual que quien lo usa.
function(ba_pch_hilos variable)
    set(_h "")
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        set(_h Threads::Threads)
    endif()
    set(${variable} ${_h} PARENT_SCOPE)
endfunction()

# ba_pch_donante(<sabor> [PIC] ENLAZA <target>... CABECERAS <h>...): el que arma el PCH
# de un sabor, en la carpeta actual.
function(ba_pch_donante sabor)
    cmake_parse_arguments(PARSE_ARGV 1 _d "PIC" "" "ENLAZA;CABECERAS")
    if(NOT BA_PCH)
        return()
    endif()
    set(_n ba_pch_${sabor})
    if(_d_PIC)
        set(_n ba_pch_${sabor}_pic)
    endif()
    if(TARGET ${_n})
        return()
    endif()
    set(_vacio "${CMAKE_BINARY_DIR}/pch/${_n}.cpp")
    if(NOT EXISTS "${_vacio}")
        file(WRITE "${_vacio}" "// El donante del PCH ${_n} (cmake/BaPch.cmake): no tiene código.\n")
    endif()
    add_library(${_n} STATIC "${_vacio}")
    target_link_libraries(${_n} PRIVATE ${_d_ENLAZA})
    set(_hs "")
    foreach(_h IN LISTS _d_CABECERAS)
        list(APPEND _hs "$<$<COMPILE_LANGUAGE:CXX>:${_h}>")
    endforeach()
    target_precompile_headers(${_n} PRIVATE ${_hs})
    set_target_properties(${_n} PROPERTIES
        POSITION_INDEPENDENT_CODE ${_d_PIC}
        # Sin ccache (ver arriba): el .pch tiene las rutas de este árbol.
        CXX_COMPILER_LAUNCHER ""
        BA_PCH_ENLAZA "${_d_ENLAZA}"
        FOLDER pch)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
        # Sin la fecha adentro: el mismo PCH sale igual byte a byte, y ccache (que lo
        # hashea en cada objeto que lo usa) sigue acertando aunque se vuelva a armar.
        target_compile_options(${_n} PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
    endif()
    set_property(GLOBAL APPEND PROPERTY BA_PCH_DONANTES ${_n})
endfunction()

# Lo que les da sus banderas a <target>... (los nombres, también los que no son objetivos
# aquí: los importados de Qt no se ven desde la raíz). Como CMake: de los objetivos
# pedidos, todo lo que enlazan; de ahí para abajo, solo la interfaz, sin $<LINK_ONLY:…>
# (lo privado de una biblioteca estática se enlaza pero no le pasa sus banderas a nadie).
function(_ba_pch_cierre salida)
    set(_nombres "")
    set(_pila "")
    foreach(_t IN LISTS ARGN)
        list(APPEND _nombres "${_t}")
        if(TARGET "${_t}")
            get_target_property(_alias "${_t}" ALIASED_TARGET)
            if(_alias)
                list(APPEND _nombres "${_alias}")
                set(_t "${_alias}")
            endif()
            get_target_property(_tipo "${_t}" TYPE)
            if(_tipo STREQUAL "INTERFACE_LIBRARY")
                get_target_property(_l "${_t}" INTERFACE_LINK_LIBRARIES)
            else()
                get_target_property(_l "${_t}" LINK_LIBRARIES)
            endif()
            if(_l)
                list(APPEND _pila ${_l})
            endif()
        endif()
    endforeach()
    while(_pila)
        list(POP_BACK _pila _x)
        if(_x MATCHES "^\\$<LINK_ONLY:")
            continue()
        endif()
        if(_x MATCHES "^\\$<BUILD_INTERFACE:([^>]+)>$")
            set(_x "${CMAKE_MATCH_1}")
        endif()
        if(_x IN_LIST _nombres)
            continue()
        endif()
        list(APPEND _nombres "${_x}")
        if(NOT TARGET "${_x}")
            continue()
        endif()
        get_target_property(_alias "${_x}" ALIASED_TARGET)
        if(_alias)
            list(APPEND _pila "${_alias}")
            continue()
        endif()
        get_target_property(_li "${_x}" INTERFACE_LINK_LIBRARIES)
        if(_li)
            list(APPEND _pila ${_li})
        endif()
    endwhile()
    set(${salida} "${_nombres}" PARENT_SCOPE)
endfunction()

function(_ba_pch_enlaza_qt nombres salida)
    set(_si FALSE)
    foreach(_n IN LISTS ${nombres})
        if(_n MATCHES "^Qt6?::")
            set(_si TRUE)
            break()
        endif()
    endforeach()
    set(${salida} ${_si} PARENT_SCOPE)
endfunction()

# Por qué <target> no puede reusar el PCH de <donante> (vacío: puede).
function(_ba_pch_motivo target donante salida)
    set(_m "")
    foreach(_p POSITION_INDEPENDENT_CODE MSVC_RUNTIME_LIBRARY CXX_STANDARD CXX_EXTENSIONS)
        get_target_property(_a ${target} ${_p})
        get_target_property(_b ${donante} ${_p})
        if(NOT _a)
            set(_a "")
        endif()
        if(NOT _b)
            set(_b "")
        endif()
        if(_p STREQUAL "POSITION_INDEPENDENT_CODE")
            if(_a)
                set(_a 1)
            else()
                set(_a 0)
            endif()
            if(_b MATCHES "ON|1|TRUE")
                set(_b 1)
            else()
                set(_b 0)
            endif()
        endif()
        if(NOT "${_a}" STREQUAL "${_b}")
            set(${salida} "${_p}: ${_a} (el donante: ${_b})" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    get_target_property(_flags ${target} COMPILE_FLAGS)
    if(_flags)
        set(${salida} "COMPILE_FLAGS ${_flags}" PARENT_SCOPE)
        return()
    endif()
    _ba_pch_cierre(_nombres ${target})
    # Lo que enlaza el donante, también el objetivo (si no, faltan sus definiciones).
    get_target_property(_del_donante ${donante} BA_PCH_ENLAZA)
    _ba_pch_enlaza_qt(_nombres _con_qt)
    foreach(_e IN LISTS _del_donante)
        if(_e MATCHES "^Qt6?::")
            if(NOT _con_qt)
                set(${salida} "no enlaza Qt" PARENT_SCOPE)
                return()
            endif()
        elseif(NOT _e IN_LIST _nombres)
            set(${salida} "no enlaza ${_e}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    # Opciones y definiciones propias y heredadas (salvo las de lo que el donante enlaza,
    # que son las mismas para los dos).
    set(_opc "")
    set(_defs "")
    get_target_property(_o ${target} COMPILE_OPTIONS)
    get_target_property(_d ${target} COMPILE_DEFINITIONS)
    list(APPEND _opc ${_o})
    list(APPEND _defs ${_d})
    _ba_pch_cierre(_comunes ${_del_donante})
    foreach(_x IN LISTS _nombres)
        if(_x STREQUAL target OR _x IN_LIST _comunes OR NOT TARGET "${_x}")
            continue()
        endif()
        get_target_property(_o "${_x}" INTERFACE_COMPILE_OPTIONS)
        get_target_property(_d "${_x}" INTERFACE_COMPILE_DEFINITIONS)
        list(APPEND _opc ${_o})
        list(APPEND _defs ${_d})
    endforeach()
    # Las de la carpeta (add_compile_options/definitions: -g, -Wall, NOMINMAX en Windows) las
    # tiene también el donante.
    get_target_property(_del_d_o ${donante} COMPILE_OPTIONS)
    get_target_property(_del_d_d ${donante} COMPILE_DEFINITIONS)
    if(_del_d_o)
        list(REMOVE_ITEM _opc ${_del_d_o})
    endif()
    if(_del_d_d)
        list(REMOVE_ITEM _defs ${_del_d_d})
    endif()
    foreach(_o IN LISTS _opc)
        if(_o AND NOT _o MATCHES "-NOTFOUND$" AND NOT _o MATCHES "${_BA_PCH_OPCION_INOCUA}")
            set(${salida} "opción ${_o}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    foreach(_d IN LISTS _defs)
        if(_d AND NOT _d MATCHES "-NOTFOUND$" AND _d MATCHES "${_BA_PCH_DEF_PELIGROSA}")
            set(${salida} "definición ${_d}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${salida} "" PARENT_SCOPE)
endfunction()

# Las fuentes de <target> que pueden usar el PCH; a las otras, SKIP_PRECOMPILE_HEADERS.
function(_ba_pch_fuentes target salida)
    get_target_property(_dir ${target} SOURCE_DIR)
    get_target_property(_fs ${target} SOURCES)
    set(_n 0)
    foreach(_f IN LISTS _fs)
        if(_f MATCHES "^\\$<")
            continue()
        endif()
        get_filename_component(_f "${_f}" ABSOLUTE BASE_DIR "${_dir}")
        if(NOT _f MATCHES "\\.(cpp|cc|cxx|c|m|mm)$")
            continue()
        endif()
        string(FIND "${_f}" "${CMAKE_SOURCE_DIR}/src/" _en_src)
        string(FIND "${_f}" "${CMAKE_BINARY_DIR}/" _en_build)
        set(_no "")
        if(NOT _f MATCHES "\\.(cpp|cc|cxx)$")
            set(_no "no es C++")
        elseif(NOT _en_src EQUAL 0 AND NOT _en_build EQUAL 0)
            set(_no "de terceros")
        else()
            foreach(_p COMPILE_OPTIONS COMPILE_FLAGS)
                get_source_file_property(_v "${_f}" TARGET_DIRECTORY ${target} ${_p})
                if(_v AND NOT _v STREQUAL "NOTFOUND")
                    foreach(_o IN LISTS _v)
                        if(NOT _o MATCHES "${_BA_PCH_OPCION_INOCUA}")
                            set(_no "opción ${_o}")
                        endif()
                    endforeach()
                endif()
            endforeach()
            get_source_file_property(_v "${_f}" TARGET_DIRECTORY ${target} COMPILE_DEFINITIONS)
            if(_v AND NOT _v STREQUAL "NOTFOUND")
                foreach(_d IN LISTS _v)
                    if(_d MATCHES "${_BA_PCH_DEF_PELIGROSA}")
                        set(_no "definición ${_d}")
                    endif()
                endforeach()
            endif()
        endif()
        if(_no)
            set_source_files_properties("${_f}" TARGET_DIRECTORY ${target} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        else()
            math(EXPR _n "${_n} + 1")
        endif()
    endforeach()
    set(${salida} ${_n} PARENT_SCOPE)
endfunction()

function(_ba_pch_objetivos dir salida)
    get_property(_ts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s IN LISTS _subs)
        _ba_pch_objetivos("${_s}" _mas)
        list(APPEND _ts ${_mas})
    endforeach()
    set(${salida} "${_ts}" PARENT_SCOPE)
endfunction()

# Al final del CMakeLists raíz: los donantes base/gst y, a cada objetivo nuestro de
# C++, el PCH que le toca. -DBA_PCH_INFORME=ON lista qué recibió cada uno.
function(ba_pch_aplicar)
    if(NOT BA_PCH)
        message(STATUS "BA_PCH=OFF: sin headers precompilados")
        return()
    endif()
    if(TARGET nlohmann_json::nlohmann_json)
        ba_pch_hilos(_hilos)
        ba_pch_donante(base ENLAZA nlohmann_json::nlohmann_json ${_hilos}
                       CABECERAS ${BA_PCH_STD} ${BA_PCH_JSON})
        ba_pch_donante(base PIC ENLAZA nlohmann_json::nlohmann_json ${_hilos}
                       CABECERAS ${BA_PCH_STD} ${BA_PCH_JSON})
        if(TARGET ba::gstreamer)
            ba_pch_donante(gst ENLAZA nlohmann_json::nlohmann_json ${_hilos} ba::gstreamer
                           CABECERAS ${BA_PCH_STD} ${BA_PCH_JSON} ${BA_PCH_GST})
        endif()
    endif()
    get_property(_donantes GLOBAL PROPERTY BA_PCH_DONANTES)
    _ba_pch_objetivos("${CMAKE_SOURCE_DIR}" _ts)
    set(_cuantos 0)
    set(_sin 0)
    foreach(_t IN LISTS _ts)
        if(_t IN_LIST _donantes)
            continue()
        endif()
        get_target_property(_tipo ${_t} TYPE)
        if(NOT _tipo MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
            continue()
        endif()
        # Solo lo nuestro (src/): lo de terceros (FetchContent, extern/) va como viene.
        get_target_property(_dir ${_t} SOURCE_DIR)
        string(FIND "${_dir}/" "${CMAKE_SOURCE_DIR}/src/" _en_src)
        if(NOT _en_src EQUAL 0)
            continue()
        endif()
        get_target_property(_ya ${_t} PRECOMPILE_HEADERS)
        get_target_property(_ya2 ${_t} PRECOMPILE_HEADERS_REUSE_FROM)
        get_target_property(_no ${_t} DISABLE_PRECOMPILE_HEADERS)
        if(_ya OR _ya2 OR _no)
            continue()
        endif()
        # El sabor: el pedido (ba_pch), qt si enlaza Qt, si no base.
        get_target_property(_sabor ${_t} BA_PCH_SABOR)
        if(NOT _sabor)
            _ba_pch_cierre(_nombres ${_t})
            _ba_pch_enlaza_qt(_nombres _con_qt)
            if(_con_qt)
                set(_sabor qt)
            else()
                set(_sabor base)
            endif()
        endif()
        get_target_property(_pic ${_t} POSITION_INDEPENDENT_CODE)
        set(_donante ba_pch_${_sabor})
        if(_pic)
            set(_donante ba_pch_${_sabor}_pic)
        endif()
        if(NOT TARGET ${_donante})
            if(BA_PCH_INFORME)
                message(STATUS "pch: ${_t} sin PCH (no hay ${_donante})")
            endif()
            math(EXPR _sin "${_sin} + 1")
            continue()
        endif()
        _ba_pch_motivo(${_t} ${_donante} _motivo)
        if(_motivo)
            if(BA_PCH_INFORME)
                message(STATUS "pch: ${_t} sin PCH (${_motivo})")
            endif()
            math(EXPR _sin "${_sin} + 1")
            continue()
        endif()
        _ba_pch_fuentes(${_t} _n)
        if(_n EQUAL 0)
            continue()
        endif()
        target_precompile_headers(${_t} REUSE_FROM ${_donante})
        math(EXPR _cuantos "${_cuantos} + 1")
        if(BA_PCH_INFORME)
            message(STATUS "pch: ${_t} <- ${_donante} (${_n} fuentes)")
        endif()
    endforeach()
    list(JOIN _donantes ", " _lista)
    message(STATUS "PCH: ${_cuantos} objetivos reusan los de ${_lista}; ${_sin} sin PCH (-DBA_PCH_INFORME=ON: por qué)")
endfunction()
