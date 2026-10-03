# ============================================================================
# cmake/BaGStreamer.cmake — GStreamer vendoreado (opcional)
#
# Si está extern/gstreamer-macos/GStreamer.framework (make deps-gstreamer-macos,
# tools/deps/gstreamer-macos.sh), se define BA_HAVE_GST y el objetivo
# `ba::gstreamer` (cabeceras y bibliotecas de GStreamer y GLib), y el
# framework entero se instala en bin/64bit (como libobs.framework). En
# ejecución, ba::platform::prepararGst hace que se usen SOLO esos plugins.
#
# Sin el framework, todo compila igual sin GStreamer (BA_HAVE_GST apagado).
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
    foreach(_l gstreamer-1.0 gstbase-1.0 gstapp-1.0 gstvideo-1.0 gstaudio-1.0 gstnet-1.0 gstpbutils-1.0 glib-2.0 gobject-2.0)
        list(APPEND _libs "${_g}/lib/lib${_l}.dylib")
    endforeach()
    target_link_libraries(ba::gstreamer INTERFACE ${_libs})
    target_compile_definitions(ba::gstreamer INTERFACE BA_HAVE_GST=1)
    # El framework entero, con sus enlaces simbólicos, al lado de libobs.
    install(DIRECTORY "${BA_GST_FW}" DESTINATION bin/64bit USE_SOURCE_PERMISSIONS
            PATTERN "include" EXCLUDE PATTERN "Headers" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
    message(STATUS "GStreamer: ${BA_GST_FW}")
else()
    message(STATUS "GStreamer: no está (make deps-gstreamer-macos); se compila sin él")
endif()

# Que un binario encuentre las bibliotecas de GStreamer: en el árbol de build
# (extern/) y en el bundle (bin/64bit/GStreamer.framework/Versions/1.0/lib),
# relativo a donde queda instalado (`rel`: de su carpeta a bin/64bit).
function(ba_gst_rpath target rel)
    if(NOT BA_HAVE_GST)
        return()
    endif()
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${BA_GST_LIBDIR}")
    set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path/${rel}GStreamer.framework/Versions/1.0/lib")
endfunction()
