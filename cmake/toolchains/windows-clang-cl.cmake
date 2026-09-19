# ============================================================================
# cmake/toolchains/windows-clang-cl.cmake — Windows x64 (ABI MSVC) desde Linux
#
# clang-cl + lld-link de extern/toolchain/llvm y el CRT/SDK de Microsoft en
# extern/msvc (layout /winsysroot). Lo arma tools/deps/win-toolchain.sh; lo usa
# `make win`. El resultado es el mismo que compilar con Visual Studio: ABI y
# STL de MSVC, compatible con libwebrtc (Shiguredo), OBS y Qt msvc2019_64.
# ============================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

get_filename_component(_ba_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(BA_WIN_LLVM "${_ba_root}/extern/toolchain/llvm/bin" CACHE PATH "LLVM (clang-cl, lld-link)")
set(BA_WINSYSROOT "${_ba_root}/extern/msvc" CACHE PATH "CRT + SDK de MSVC (layout /winsysroot)")
if(NOT EXISTS "${BA_WINSYSROOT}/VC" OR NOT EXISTS "${BA_WIN_LLVM}/clang-cl")
    message(FATAL_ERROR "Falta el toolchain de Windows: correr tools/deps/win-toolchain.sh (make win)")
endif()

set(CMAKE_C_COMPILER "${BA_WIN_LLVM}/clang-cl")
set(CMAKE_CXX_COMPILER "${BA_WIN_LLVM}/clang-cl")
set(CMAKE_LINKER "${BA_WIN_LLVM}/lld-link")
set(CMAKE_AR "${BA_WIN_LLVM}/llvm-lib")
set(CMAKE_RC_COMPILER "${BA_WIN_LLVM}/llvm-rc")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# CRT de release siempre (el de debug no es redistribuible y xwin no lo baja).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)

# El CRT y el SDK salen de extern/msvc, nunca de otro lado.
set(_ba_flags "/winsysroot \"${BA_WINSYSROOT}\" -fuse-ld=lld-link")
set(CMAKE_C_FLAGS_INIT "${_ba_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_ba_flags} /EHsc")
set(CMAKE_RC_FLAGS_INIT "-I \"${BA_WINSYSROOT}/Windows Kits/10/Include/10.0.26100/um\" -I \"${BA_WINSYSROOT}/Windows Kits/10/Include/10.0.26100/shared\"")
foreach(_k EXE SHARED MODULE)
    set(CMAKE_${_k}_LINKER_FLAGS_INIT "/winsysroot:\"${BA_WINSYSROOT}\" /machine:x64")
endforeach()

# Nada del Linux anfitrion (headers de Vulkan, pkg-config...) puede colarse en
# un binario de Windows.
set(CMAKE_IGNORE_PREFIX_PATH /usr /usr/local)
set(CMAKE_IGNORE_PATH /usr/include /usr/local/include /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
set(ENV{PKG_CONFIG_LIBDIR} "/nonexistent")

# Buscar bibliotecas/paquetes solo en los prefijos que se indiquen (extern/),
# herramientas (moc, rcc...) en el host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
