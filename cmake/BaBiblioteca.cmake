# ============================================================================
# cmake/BaBiblioteca.cmake — una biblioteca del diseño físico, con su nivel
#
#   ba_biblioteca(<nombre> NIVEL <n> [DEPENDE <target>...] FUENTES <archivo>...)
#
# Una biblioteca ESTÁTICA con las FUENTES de un componente de tools/diag/niveles.txt:
# se compila una sola vez y la enlazan el programa y sus tests (antes cada test
# compilaba suelta su copia de las fuentes de la app: Evento.cpp, 14 veces).
#
#   NIVEL    el del componente en tools/diag/niveles.txt (0 ba_core → 1 ba_motor,
#            ba_gateway_* → 1.5 ba_app_base → 2 áreas de la app → 3 ba_app_ui →
#            4 programas). tools/diag/niveles.py lee esta llamada del CMakeLists
#            (por eso NIVEL y FUENTES van escritos acá, no en variables) y
#            test_niveles_enlaces revisa que coincida con el mapa y que DEPENDE no
#            suba ni cruce entre iguales.
#   DEPENDE  lo que la biblioteca usa, PUBLIC: quien la enlaza ve los mismos
#            headers y las mismas definiciones que vio ella al compilarse (no hay
#            un mismo .hpp leído con otras macros en el programa y en un test).
#            Solo hacia abajo. Lo que hoy se incluye entre iguales (la deuda de
#            test_niveles) NO se declara: lo resuelve quien enlaza las dos.
#   FUENTES  rutas relativas a la carpeta del CMakeLists, una por una (las lee
#            niveles.py). Los .hpp sin .cpp se pueden listar: documentan qué es
#            de la biblioteca y AUTOMOC los mira.
#
# Por qué estática y no OBJECT: un test que usa Evento.cpp no tiene por qué
# resolver lo que usan las demás fuentes del área (el enlazador trae de una .a
# solo los objetos que alguien nombra). Eso pide que nada se registre solo con
# un inicializador estático (PLAN-DISENO-FISICO.md, regla 6 del contrato): el
# objeto que nadie nombra no entra. docs/local/inventario-autorregistro.md.
#
# AUTOMOC propio: los moc de sus headers se compilan una vez, en la biblioteca
# (antes, en ba-app y otra vez en cada test con AUTOMOC que listaba la fuente).
# Cada X.cpp cuyo X.hpp declara Q_OBJECT/Q_GADGET/Q_NAMESPACE termina con
# `#include "moc_X.cpp"`: así el moc va en el objeto de X y no en el
# mocs_compilation.cpp de la biblioteca, que junta TODOS sus moc en un objeto. Con
# ese objeto único, un test que usa una sola clase (sala/QuienMira) se llevaba el
# moc de todas y, con él, todo lo que usan (ConferenceClient, el motor…). Lo
# revisa la configuración (abajo).
# ============================================================================

function(ba_biblioteca nombre)
    cmake_parse_arguments(PARSE_ARGV 1 _bb "" "NIVEL" "DEPENDE;FUENTES")
    if(_bb_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "biblioteca ${nombre}: no entiendo ${_bb_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT DEFINED _bb_NIVEL OR NOT _bb_NIVEL MATCHES "^[0-9]+(\\.[0-9]+)?$")
        message(FATAL_ERROR "biblioteca ${nombre}: falta NIVEL <n> (el de tools/diag/niveles.txt)")
    endif()
    if(NOT _bb_FUENTES)
        message(FATAL_ERROR "biblioteca ${nombre}: sin FUENTES")
    endif()
    add_library(${nombre} STATIC ${_bb_FUENTES})
    target_link_libraries(${nombre} PUBLIC ${_bb_DEPENDE})
    set_target_properties(${nombre} PROPERTIES AUTOMOC ON BA_NIVEL "${_bb_NIVEL}")
    # Las fuentes de la biblioteca, absolutas: ba_biblioteca_fuentes() las saca de las
    # listas de quien antes las compilaba (ba-app).
    set(_abs "")
    set(_sin_moc "")
    foreach(_f IN LISTS _bb_FUENTES)
        get_filename_component(_f "${_f}" ABSOLUTE)
        list(APPEND _abs "${_f}")
        # El moc de su header, dentro del .cpp (ver arriba).
        if(_f MATCHES "\\.cpp$")
            string(REGEX REPLACE "\\.cpp$" ".hpp" _h "${_f}")
            if(EXISTS "${_h}")
                file(STRINGS "${_h}" _qobj REGEX "^[ \t]*(Q_OBJECT|Q_GADGET|Q_NAMESPACE)([ \t]|$)")
                if(_qobj)
                    get_filename_component(_base "${_f}" NAME_WE)
                    file(STRINGS "${_f}" _inc REGEX "^#include \"moc_${_base}\\.cpp\"")
                    if(NOT _inc)
                        list(APPEND _sin_moc "${_f}")
                    endif()
                endif()
            endif()
        endif()
    endforeach()
    if(_sin_moc)
        list(JOIN _sin_moc "\n  " _lista)
        message(FATAL_ERROR "biblioteca ${nombre}: a estas fuentes les falta, al final, el "
                            "#include \"moc_<nombre>.cpp\" de su header con Q_OBJECT (cmake/BaBiblioteca.cmake):\n  ${_lista}")
    endif()
    set_property(GLOBAL APPEND PROPERTY BA_BIBLIOTECAS ${nombre})
    set_property(GLOBAL PROPERTY BA_BIBLIOTECA_${nombre}_FUENTES ${_abs})
endfunction()

# ba_biblioteca_fuentes(<variable> <biblioteca>...): saca de la lista <variable> (rutas
# absolutas) las fuentes que ya compilan esas bibliotecas.
function(ba_biblioteca_fuentes variable)
    set(_lista ${${variable}})
    foreach(_b IN LISTS ARGN)
        get_property(_fs GLOBAL PROPERTY BA_BIBLIOTECA_${_b}_FUENTES)
        if(_fs)
            list(REMOVE_ITEM _lista ${_fs})
        endif()
    endforeach()
    set(${variable} ${_lista} PARENT_SCOPE)
endfunction()

# ba_biblioteca_completa(<biblioteca> <carpeta> [<regex de exclusión>]): que todo .cpp de
# <carpeta> (relativa a la carpeta del CMakeLists) esté en la biblioteca. Un archivo nuevo
# que no se agregó a FUENTES frena la configuración (si no, ba-app lo compilaría solo por su
# GLOB y los tests no lo verían en la biblioteca).
function(ba_biblioteca_completa nombre carpeta)
    file(GLOB _cpps CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${carpeta}/*.cpp")
    if(ARGC GREATER 2)
        list(FILTER _cpps EXCLUDE REGEX "${ARGV2}")
    endif()
    get_property(_fs GLOBAL PROPERTY BA_BIBLIOTECA_${nombre}_FUENTES)
    list(REMOVE_ITEM _cpps ${_fs})
    if(_cpps)
        list(JOIN _cpps "\n  " _faltan)
        message(FATAL_ERROR "biblioteca ${nombre}: estas fuentes de ${carpeta}/ no están en sus FUENTES "
                            "(agregalas a la llamada en ${CMAKE_CURRENT_LIST_FILE}):\n  ${_faltan}")
    endif()
endfunction()

# ba_biblioteca_grupos_linux(): en Linux, a cada target de la carpeta que enlaza dos o más
# bibliotecas de ba_biblioteca, se las pone en un -Wl,--start-group … --end-group. Las áreas
# de nivel 2 todavía se usan entre sí (la deuda de test_niveles: audio usa agent/Vocabulario
# y agent usa audio/…) y el enlazador de Linux resuelve una .a solo con las que vienen
# después; la Mac y Windows ya vuelven a mirar solos. Llamarla al final del CMakeLists.
function(ba_biblioteca_grupos_linux)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()
    get_property(_bibs GLOBAL PROPERTY BA_BIBLIOTECAS)
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_tipo ${_t} TYPE)
        if(NOT _tipo STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(_libs ${_t} LINK_LIBRARIES)
        if(NOT _libs)
            continue()
        endif()
        set(_grupo "")
        set(_resto "")
        set(_pos -1)
        set(_i 0)
        foreach(_l IN LISTS _libs)
            if(_l IN_LIST _bibs)
                if(_pos EQUAL -1)
                    set(_pos ${_i})
                endif()
                list(APPEND _grupo ${_l})
            else()
                list(APPEND _resto "${_l}")
                math(EXPR _i "${_i} + 1")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _grupo)
        list(LENGTH _grupo _n)
        if(_n LESS 2)
            continue()
        endif()
        set(_nuevo -Wl,--start-group ${_grupo} -Wl,--end-group)
        list(LENGTH _resto _nr)
        if(_pos GREATER_EQUAL _nr)
            list(APPEND _resto ${_nuevo})
        else()
            list(INSERT _resto ${_pos} ${_nuevo})
        endif()
        set_property(TARGET ${_t} PROPERTY LINK_LIBRARIES "${_resto}")
    endforeach()
endfunction()
