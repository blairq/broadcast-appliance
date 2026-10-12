# ============================================================================
# src/web — `ba-web`: el servidor web público, adelante del túnel
#
# C11, con la arquitectura de nginx (master + workers con su bucle de eventos:
# epoll / kqueue / WSAPoll). Sirve el sitio del evento desde memoria con gzip,
# guarda un microcaché de la API y le pasa el resto al gateway. Lo lanza y lo
# vigila el gateway; si se cae, el gateway lo vuelve a lanzar. Ver bw.h.
# Sin nada del sistema: solo libdeflate (FetchContent / extern/libdeflate).
# ============================================================================

set(BA_WEB_SOURCES main.c http.c conf.c ev.c gz.c plat.c)
if(MSVC)
    add_executable(ba-web WIN32 ${BA_WEB_SOURCES})
    target_link_options(ba-web PRIVATE /ENTRY:mainCRTStartup)
else()
    add_executable(ba-web ${BA_WEB_SOURCES})
endif()
set_target_properties(ba-web PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(ba-web PRIVATE libdeflate::libdeflate_static)
if(WIN32)
    target_link_libraries(ba-web PRIVATE ws2_32)
    target_compile_definitions(ba-web PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
elseif(NOT APPLE)
    target_compile_definitions(ba-web PRIVATE _GNU_SOURCE)
endif()
if(NOT MSVC)
    target_compile_options(ba-web PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
install(TARGETS ba-web RUNTIME DESTINATION bin/64bit COMPONENT bundle)

# La prueba: arranca ba-web con un gateway de mentira y le pega como un navegador.
if(BUILD_TESTING OR TRUE)
    add_executable(test_ba_web tests/test_ba_web.c gz.c plat.c conf.c)
    set_target_properties(test_ba_web PROPERTIES C_STANDARD 11)
    target_include_directories(test_ba_web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_ba_web PRIVATE libdeflate::libdeflate_static)
    if(WIN32)
        target_link_libraries(test_ba_web PRIVATE ws2_32)
    elseif(NOT APPLE)
        target_compile_definitions(test_ba_web PRIVATE _GNU_SOURCE)
        target_link_libraries(test_ba_web PRIVATE pthread)
    endif()
    target_compile_definitions(test_ba_web PRIVATE BA_WEB_EXE="$<TARGET_FILE:ba-web>")
    add_dependencies(test_ba_web ba-web)
    add_test(NAME test_ba_web COMMAND test_ba_web)
endif()
