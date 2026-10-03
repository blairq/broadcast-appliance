# ============================================================================
# cmake/BaHerramientas.cmake — ffmpeg, ffprobe, yt-dlp y deno en el bundle
#
# Si está extern/herramientas/<plataforma> (make deps-herramientas,
# tools/deps/herramientas.sh), se instala en herramientas/ y se define
# BA_HAVE_HERRAMIENTAS. El programa las pone primeras en el PATH de lo que
# lanza (plugins, terminal del asistente): nadie tiene que bajarlas.
# ============================================================================
set(BA_HAVE_HERRAMIENTAS OFF)
if(APPLE)
    set(_ba_herr_plat macos-arm64)
elseif(WIN32)
    set(_ba_herr_plat win-x64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_ba_herr_plat linux-arm64)
else()
    set(_ba_herr_plat linux-x64)
endif()
set(BA_HERRAMIENTAS_DIR "${CMAKE_SOURCE_DIR}/extern/herramientas/${_ba_herr_plat}")
if(EXISTS "${BA_HERRAMIENTAS_DIR}/LICENCIAS.txt")
    set(BA_HAVE_HERRAMIENTAS ON)
    install(DIRECTORY "${BA_HERRAMIENTAS_DIR}/" DESTINATION herramientas USE_SOURCE_PERMISSIONS COMPONENT bundle)
    message(STATUS "Herramientas de medios: ${BA_HERRAMIENTAS_DIR}")
else()
    message(STATUS "Herramientas de medios: no están (make deps-herramientas); sin ffmpeg/yt-dlp en el programa")
endif()
