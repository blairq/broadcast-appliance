# ============================================================================
# cmake/BaGStreamer.cmake — GStreamer vendoreado (opcional)
#
# Si está extern/gstreamer-macos/GStreamer.framework (make deps-gstreamer-macos,
# tools/deps/gstreamer-macos.sh), se define BA_HAVE_GST y el objetivo
# `ba::gstreamer` (cabeceras y bibliotecas de GStreamer y GLib), y el
# framework entero se instala en bin/64bit (como libobs.framework). En
# ejecución, ba::platform::prepararGst hace que se usen SOLO esos plugins.
#
# En Linux, extern/gstreamer-linux (make deps-gstreamer-linux: compilado desde
# fuente, tools/deps/gstreamer-linux.sh) va a bin/64bit/gstreamer; glib es la
# del sistema (base, como para Qt).
#
# Sin GStreamer, todo compila igual (BA_HAVE_GST apagado).
# ============================================================================
set(BA_HAVE_GST OFF)
set(BA_GST_FW "${CMAKE_SOURCE_DIR}/extern/gstreamer-macos/GStreamer.framework")
if(APPLE AND EXISTS "${BA_GST_FW}/Versions/1.0/lib/libgstreamer-1.0.dylib")
    set(BA_HAVE_GST ON)
    set(_g "${BA_GST_FW}/Versions/1.0")
    set(BA_GST_LIBDIR "${_g}/lib")
    add_library(ba::gstreamer INTERFACE IMPORTED)
    # Headers/: las cabeceras del framework, aplanadas (gst/, glib.h, glibconfig.h por arquitectura).
    target_include_directories(ba::gstreamer INTERFACE "${_g}/Headers")
    set(_libs "")
    foreach(_l gstreamer-1.0 gstbase-1.0 gstapp-1.0 gstvideo-1.0 gstaudio-1.0 gstnet-1.0 gstpbutils-1.0 gstgl-1.0 glib-2.0 gobject-2.0)
        list(APPEND _libs "${_g}/lib/lib${_l}.dylib")
    endforeach()
    target_link_libraries(ba::gstreamer INTERFACE ${_libs})
    target_compile_definitions(ba::gstreamer INTERFACE BA_HAVE_GST=1)
    # El framework entero, con sus enlaces simbólicos, al lado de libobs.
    install(DIRECTORY "${BA_GST_FW}" DESTINATION bin/64bit USE_SOURCE_PERMISSIONS
            PATTERN "include" EXCLUDE PATTERN "Headers" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
    # libgstpython para el Python 3.12 del bundle (tools/deps/gst-python-mac.sh):
    # elementos de GStreamer escritos en Python. Va con los plugins propios.
    if(EXISTS "${CMAKE_SOURCE_DIR}/extern/gst-python-mac/libgstpython.dylib")
        install(FILES "${CMAKE_SOURCE_DIR}/extern/gst-python-mac/libgstpython.dylib" DESTINATION gst-plugins COMPONENT bundle)
    endif()
    message(STATUS "GStreamer: ${BA_GST_FW}")
elseif(UNIX AND NOT APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/extern/gstreamer-linux/lib/libgstreamer-1.0.so")
    set(BA_HAVE_GST ON)
    set(_g "${CMAKE_SOURCE_DIR}/extern/gstreamer-linux")
    set(BA_GST_LIBDIR "${_g}/lib")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BA_GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0)
    add_library(ba::gstreamer INTERFACE IMPORTED)
    target_include_directories(ba::gstreamer INTERFACE "${_g}/include/gstreamer-1.0" "${_g}/lib/gstreamer-1.0/include")
    set(_libs "")
    foreach(_l gstreamer-1.0 gstbase-1.0 gstapp-1.0 gstvideo-1.0 gstaudio-1.0 gstnet-1.0 gstpbutils-1.0 gstgl-1.0)
        list(APPEND _libs "${_g}/lib/lib${_l}.so")
    endforeach()
    target_link_libraries(ba::gstreamer INTERFACE ${_libs} PkgConfig::BA_GLIB)
    target_compile_definitions(ba::gstreamer INTERFACE BA_HAVE_GST=1)
    install(DIRECTORY "${_g}/" DESTINATION bin/64bit/gstreamer USE_SOURCE_PERMISSIONS
            PATTERN "include" EXCLUDE PATTERN "pkgconfig" EXCLUDE PATTERN ".version" EXCLUDE PATTERN "*.a" EXCLUDE
            PATTERN "python3.*" EXCLUDE)
    # Los bindings de Python (gi con los overrides de Gst), al Python de los plugins.
    if(EXISTS "${_g}/lib/python3.12/site-packages")
        install(DIRECTORY "${_g}/lib/python3.12/site-packages/" DESTINATION python/lib/python3.12/site-packages
                USE_SOURCE_PERMISSIONS PATTERN "__pycache__" EXCLUDE)
    endif()
    message(STATUS "GStreamer: ${_g}")
elseif(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/extern/gstreamer-win/lib/gstreamer-1.0.lib")
    # Windows: el oficial de MSVC (make deps-gstreamer-win). Las DLL van junto
    # al .exe (bin/64bit: Windows busca primero ahí) y el resto, a
    # bin/64bit/gstreamer (plugins, escáner, TLS de GIO).
    set(BA_HAVE_GST ON)
    set(_g "${CMAKE_SOURCE_DIR}/extern/gstreamer-win")
    set(BA_GST_LIBDIR "${_g}/bin")
    add_library(ba::gstreamer INTERFACE IMPORTED)
    target_include_directories(ba::gstreamer INTERFACE "${_g}/include/gstreamer-1.0" "${_g}/include/glib-2.0"
                                                       "${_g}/lib/glib-2.0/include" "${_g}/lib/gstreamer-1.0/include")
    set(_libs "")
    foreach(_l gstreamer-1.0 gstbase-1.0 gstapp-1.0 gstvideo-1.0 gstaudio-1.0 gstnet-1.0 gstpbutils-1.0 gstgl-1.0 glib-2.0 gobject-2.0)
        list(APPEND _libs "${_g}/lib/${_l}.lib")
    endforeach()
    target_link_libraries(ba::gstreamer INTERFACE ${_libs})
    target_compile_definitions(ba::gstreamer INTERFACE BA_HAVE_GST=1)
    # Las DLL que también trae el OBS (su FFmpeg 7.0; GStreamer trae la 7.1, la
    # misma versión mayor: el OBS anda con la nueva, gst-libav no con la vieja):
    # va la de GStreamer. Windows carga una sola por nombre.
    set(_ba_obs_libs "")
    foreach(_l IN LISTS BA_LIBOBS_RUNTIME_LIBS)
        get_filename_component(_n "${_l}" NAME)
        if(EXISTS "${_g}/bin/${_n}")
            message(STATUS "GStreamer: ${_n} (la de GStreamer, no la del OBS)")
        else()
            list(APPEND _ba_obs_libs "${_l}")
        endif()
    endforeach()
    set(BA_LIBOBS_RUNTIME_LIBS ${_ba_obs_libs})
    install(DIRECTORY "${_g}/bin/" DESTINATION bin/64bit COMPONENT bundle PATTERN "*.pdb" EXCLUDE)
    # Todo lo demás (plugins, escáner, Python de los plugins, GIO, presets),
    # menos lo que es solo para compilar.
    install(DIRECTORY "${_g}/" DESTINATION bin/64bit/gstreamer COMPONENT bundle
            PATTERN "bin" EXCLUDE PATTERN "include" EXCLUDE PATTERN "*.lib" EXCLUDE PATTERN "*.def" EXCLUDE
            PATTERN "*.a" EXCLUDE PATTERN "pkgconfig" EXCLUDE PATTERN ".version" EXCLUDE PATTERN "duplicadas.txt" EXCLUDE
            PATTERN "*.pdb" EXCLUDE) # símbolos de depuración (231 MB): no hacen nada en el aparato
    # gstpython.dll para el Python 3.12 del bundle (tools/deps/gst-python-win.sh).
    file(GLOB _ba_gstpy "${CMAKE_SOURCE_DIR}/extern/gst-python-win/gstpython*.dll")
    if(_ba_gstpy)
        install(FILES ${_ba_gstpy} DESTINATION gst-plugins COMPONENT bundle)
    endif()
    message(STATUS "GStreamer: ${_g}")
else()
    message(STATUS "GStreamer: no está (make deps-gstreamer-macos / -linux / -win); se compila sin él")
endif()

# Que un binario encuentre las bibliotecas de GStreamer: en el árbol de build
# (extern/) y en el bundle (bin/64bit/GStreamer.framework/Versions/1.0/lib),
# relativo a donde queda instalado (`rel`: de su carpeta a bin/64bit).
function(ba_gst_rpath target rel)
    if(NOT BA_HAVE_GST OR WIN32)
        return()
    endif()
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${BA_GST_LIBDIR}")
    if(APPLE)
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path/${rel}GStreamer.framework/Versions/1.0/lib")
    else()
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN/${rel}gstreamer/lib")
    endif()
endfunction()
