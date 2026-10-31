// ============================================================================
// src/core/platform/Paths.hpp — Rutas del proceso y del bundle portable
//
// Proposito:
//   Resolver, igual en las tres plataformas, donde esta el ejecutable, la raiz
//   del bundle portable (layout §5.1) y el directorio de runtime del usuario
//   (sockets de control, lockfiles).
//
//   Es la primera pieza de `ba::platform`: la capa unica donde viven los
//   `#ifdef` de sistema. La implementacion se elige por archivo en CMake
//   (`Paths_win.cpp`, `Paths_linux.cpp`, `Paths_mac.cpp`); la logica comun
//   (resolver la raiz del bundle) esta en `Paths.cpp` y no depende del SO.
//
// Layout que se reconoce para la raiz del bundle:
//   - `<root>/bin/64bit/<exe>`          -> ba-app, ba-gateway (Windows/Linux)
//   - `<root>/<exe>`                    -> el launcher (vive en la raiz, §5.1)
//   - `<X>.app/Contents/MacOS/<exe>`    -> launcher en macOS; root = Contents/Resources
//   - `<X>.app/Contents/Resources/bin/64bit/<exe>` -> A y B en macOS (la regla de
//     bin/64bit); root = Contents/Resources
//
// Estrategia de errores:
//   No lanza. Si el SO no permite resolver el ejecutable se devuelve una ruta
//   vacia y los llamadores caen al directorio de trabajo.
// ============================================================================

#pragma once

#include <filesystem>

namespace ba::platform {

/// Ruta absoluta del ejecutable del proceso actual (vacia si no se pudo).
std::filesystem::path executablePath();

/// Directorio que contiene al ejecutable (vacio si no se pudo).
std::filesystem::path executableDir();

/// Raiz del bundle portable a partir de un directorio de ejecutable.
///
/// Funcion pura (no toca el disco): aplica las reglas de layout de arriba.
/// Separada de `bundleRoot()` para poder testearla en cualquier plataforma.
///
/// :param exeDir: directorio del ejecutable.
/// :returns: la raiz del bundle; `exeDir` si no coincide ningun layout.
std::filesystem::path bundleRootFor(const std::filesystem::path& exeDir);

/// Raiz del bundle portable del proceso actual (`bundleRootFor(executableDir())`).
///
/// Es de donde salen los BINARIOS (bin/, obs-plugins/, data/). Con copia
/// sombra (spec 3.1.5) apunta a la copia, no al disco.
std::filesystem::path bundleRoot();

/// Raiz de los datos para una raiz de bundle (funcion pura, testeable).
///
/// En Windows y Linux es la misma carpeta. En macOS, si el bundle es
/// `<X>.app/Contents/Resources`, los datos van AL LADO de la .app:
/// `<dir>/<X> Datos/`. Lo que se escribe adentro de una .app rompe el sello
/// de su firma (macOS la vuelve a verificar entera y no reconoce su identidad
/// para los permisos de camara y microfono). Sigue siendo portable: se lleva
/// la carpeta con las dos cosas.
std::filesystem::path dataRootFor(const std::filesystem::path& bundleRoot);
/// Variable de entorno con la que el launcher le indica a A y B la raiz REAL.
inline constexpr const char* kDataRootEnv = "BA_DATA_ROOT";

/// Raiz de los DATOS del usuario (Config/, Logs/, Recordings/, Web/).
///
/// Cuando A y B corren desde la copia sombra, sus binarios estan en una cache
/// pero los datos viven en el disco real: el launcher lo informa por
/// `BA_DATA_ROOT`. Sin esa variable (proceso lanzado a mano) es la raiz del
/// bundle (`dataRootFor(bundleRoot())`: en macOS, la carpeta al lado de la .app).
std::filesystem::path dataRoot();

// --- Configuraciones (cada una con lo suyo) -----------------------------------
//
// La carpeta de datos es de la MÁQUINA (Modelos/, Herramientas/, Agentes/, Cache/,
// Runtime/, Logs/, Credenciales/ y launcher.json); cada CONFIGURACIÓN es una carpeta propia,
// `<datos>/Configuraciones/<nombre>/`, con todo lo suyo: Config/, Media/,
// Graficos/, Plugins/, Fotos/, Miniaturas/, Eventos/, Recordings/, Shares/,
// Exportado/ y Extensiones/. Credenciales/ es de la máquina (su clave de cifrado, el
// token del MCP, el certificado TLS, las claves de las cámaras). Nada más se comparte (un cambio en una no
// toca a otra) y cambiar de configuración no copia nada: se arranca con otra
// carpeta. La activa la dice `<datos>/configuraciones.json` ({"activa": nombre});
// el launcher se la pasa a los hijos en BA_CONFIG_ROOT. Sin Configuraciones/
// (una instalación vieja, antes de mudarla) es la carpeta de datos misma.
inline constexpr const char* kConfigRootEnv = "BA_CONFIG_ROOT";
/// Las carpetas que son de cada configuración (no de la máquina).
inline constexpr const char* kCarpetasDeConfiguracion[] = {"Config",  "Media",      "Graficos",  "Plugins",
                                                           "Fotos",   "Miniaturas", "Eventos",   "Recordings",
                                                           "Shares",  "Exportado",  "Extensiones"};
/// launcher.json, los ajustes de la MÁQUINA (los escribe la interfaz, los leen el launcher y
/// cada proceso al arrancar: el idioma, la copia sombra…): `<datos>/launcher.json`; una
/// instalación vieja todavía sin mudar lo tiene en `Config/`.
std::filesystem::path launcherJsonFor(const std::filesystem::path& datos);

/// La carpeta de la configuración activa para esa carpeta de datos (función pura de lo que hay en disco).
std::filesystem::path configRootFor(const std::filesystem::path& datos);
/// La configuración activa: BA_CONFIG_ROOT si está; si no, configRootFor(dataRoot()).
std::filesystem::path configRoot();
/// El nombre de la activa ("" en una instalación sin configuraciones).
std::string configActiva(const std::filesystem::path& datos);

/// Una ruta para GUARDAR en la configuración: si está dentro de los datos, relativa
/// a ellos («Modelos/superres/x.onnx»); si no, tal cual. Así la carpeta se puede
/// mover (o copiar a otra máquina) y lo guardado sigue apuntando bien.
std::string rutaGuardable(const std::filesystem::path& p);
/// Lo guardado, de vuelta a una ruta para usar: relativa = dentro de los datos;
/// absoluta que existe, tal cual; absoluta que ya no existe (la carpeta se movió)
/// pero pasa por una carpeta de los datos (Modelos/, Media/…), rearmada desde los
/// datos de ahora.
std::filesystem::path rutaDeDatos(const std::string& guardada);

/// Estado de ejecucion DENTRO del bundle portable: `<datos>/Runtime/`
/// (archivos de encuentro con tokens: gateway-admin.json, mcp.json). Nada de
/// esto va al perfil del usuario ni a la maquina: el appliance corre desde
/// un pen drive y todo queda ahi. Creado 0700 donde el sistema de archivos
/// lo permite (FAT/exFAT no tiene permisos Unix).
std::filesystem::path stateDir();

/// Caches de librerias DENTRO del bundle: `<datos>/Cache/`.
std::filesystem::path cacheDir();

/// Credenciales del equipo DENTRO del bundle: `<datos>/Credenciales/` (de la máquina,
/// no de cada configuración: ahí está su clave de cifrado).
/// Carpeta propia, aparte de Config/ y del programa: una actualizacion
/// (`BroadcastAppliance --update`) nunca la toca y la respalda antes. Ver
/// security/CredentialStore.hpp.
std::filesystem::path credentialsDir();

/// Redirige al bundle lo que librerias de terceros escriben en el perfil
/// del usuario: XDG_CACHE_HOME (Qt, fontconfig, Mesa), QML_DISK_CACHE_PATH,
/// caches de shaders (MESA_SHADER_CACHE_DIR, __GL_SHADER_DISK_CACHE_PATH) y
/// BA_CACHE_DIR (copia sombra). Llamar al arrancar cada proceso, ANTES de
/// crear la aplicacion Qt o iniciar GStreamer. Se heredan a los hijos.
void redirectUserDirs();

/// Directorio de runtime para sockets locales y lockfiles.
///
/// Windows: `<datos>\Runtime` (los pipes y mutex con nombre no usan disco).
/// Linux: `$XDG_RUNTIME_DIR/broadcast-appliance` (RAM, se borra al cerrar
/// la sesion: los sockets Unix no se pueden crear en FAT/exFAT y su ruta
/// tiene un limite de 108 caracteres, asi que no van al pen drive). macOS:
/// `$TMPDIR/broadcast-appliance` (TMPDIR es por usuario en macOS). Si la
/// variable no esta, `/tmp/broadcast-appliance-<uid>`. Se crea con permisos
/// 0700 en POSIX: el canal de control no debe ser accesible por otros usuarios.
///
/// :returns: el directorio (creado), o vacio si no se pudo crear.
std::filesystem::path runtimeDir();

} // namespace ba::platform
