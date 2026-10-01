# ============================================================================
# cmake/BaMacInfoPlist.cmake — Info.plist embebido en un ejecutable suelto
#
# macOS (TCC) mata a un proceso que abre la camara o el microfono sin la
# explicacion de uso en su Info.plist. Los ejecutables del bundle portable no
# son .app, asi que el Info.plist se embebe en el binario (seccion
# __TEXT,__info_plist), que es donde TCC lo busca para una herramienta suelta.
#
# ba_mac_embed_info_plist(<target> <bundle-id> <nombre>)
# ============================================================================
function(ba_mac_embed_info_plist target bundle_id name)
    if(NOT APPLE)
        return()
    endif()
    set(BA_MAC_BUNDLE_ID "${bundle_id}")
    set(BA_MAC_NAME "${name}")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${target}-Info.plist")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in" "${_out}" @ONLY)
    target_link_options(${target} PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${_out}")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_out}")
endfunction()
