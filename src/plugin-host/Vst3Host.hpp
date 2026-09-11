// ============================================================================
// src/plugin-host/Vst3Host.hpp — Host VST3 (spec D6/§7.5, LSP como componente
// CORE)
//
// Módulo `ba_plugin_host`: carga plugins VST3 (los bundles .vst3 de LSP que
// se despliegan en assets/audio-plugins/) y embebe su editor gráfico completo en una
// ventana hija.
//
// Modelo de hilos:
//   - CAMINO DE CONTROL: load(), unload(), reset(), attachEditor() y
//     scanDirectory() se invocan desde la UI / hilo principal. No hay
//     restricciones de tiempo real.
//   - CAMINO RT: el `process()` del IAudioProcessor del plugin corre en el
//     hilo de audio del host (Proceso A). Durante la vida del plugin, NUNCA
//     se tocan los punteros del processor desde otro hilo: load/unload solo
//     se hacen con el grafo de audio detenido.
//
// Política de errores:
//   - Ninguna función lanza excepciones. load()/attachEditor() devuelven
//     false ante cualquier fallo (dll ausente, entry point ausente, QI
//     fallido, editor sin crear) y dejan el host en un estado consistente
//     (sin punteros colgantes).
//   - scanDirectory() no lanza: ante un directorio inexistible o ilegible
//     devuelve una lista vacía.
//
// Seguridad RT (real-time safety):
//   - Este módulo solo gestiona el ciclo de vida del plugin (control path).
//     El callback de audio lo provee el host (Proceso A) invocando
//     IAudioProcessor::process(); ese callback ES el camino RT y no debe
//     llamar a malloc, IO ni locks. load/unload/attachEditor son control
//     path y pueden hacer lo que haga falta (LoadLibrary, QI, CreateView).
//
// Plataforma / feature flag:
//   - Windows + SDK presente (`_WIN32` && `BA_PLUGIN_HOST_HAVE_VST3`):
//     implementación REAL — LoadLibraryW del .dll, resolución del entry
//     point `ModuleEntry`, QueryInterface del IAudioProcessor +
//     IEditControllerEx1 y embebido del editor vía createView sobre el HWND
//     padre. Usa los headers del VST3 SDK (MIT), traído por FetchContent SOLO
//     en Windows. El flag se define en el CMakeLists solo cuando los targets
//     del SDK existen (mismo patrón que ba-app con BA_APP_HAVE_QT_OBS).
//   - Sin SDK (Linux/macOS, o un Windows donde no se trajo el SDK): fallback
//     HEADLESS — scanDirectory() es puro C++ (std::filesystem) y se testa en
//     Linux; load/unload/attachEditor loguean por stderr y devuelven
//     false/vacíos, para que el módulo compile OFFLINE (sin SDK, sin red).
// ============================================================================

#pragma once

#include <string>
#include <vector>

namespace ba {
namespace plugin {

/// Descripción de un bundle VST3 descubierto en disco.
///
/// Un `.vst3` es un DIRECTORIO (bundle), no un archivo:
///   Foo.vst3/
///   ├── Contents/
///   │   └── x86_64-win/Foo.dll   (Windows)
///   └── ...
///
/// :name: nombre corto del bundle (el archivo .vst3 sin extensión), p. ej.
///        "LSP-Pluck".
/// :bundlePath: ruta absoluta del directorio del bundle.
/// :dllPath: ruta absoluta del binario que contiene el entry point VST3
///        (vacío si no se pudo resolver; en Linux el stub no lo garantiza).
struct BundleInfo {
    std::string name;
    std::string bundlePath;
    std::string dllPath;
};

/// Escanea `root` UN NIVEL y devuelve los bundles VST3 reales que contiene.
///
/// Puro C++ (std::filesystem), cross-platform y testeable en Linux:
///   - Un bundle "real" es un DIRECTORIO cuyo nombre termina en `.vst3` y
///     que contiene un subdirectorio `Contents/` (layout estándar VST3).
///     Archivos sueltos o directorios sin `Contents/` se descartan.
///   - En Windows, `dllPath` se resuelve a
///     `<bundle>/Contents/x86_64-win/<Name>.dll` si existe; si no, se busca
///     el .dll más cercano bajo el bundle; si tampoco, queda vacío.
///   - Fuera de Windows, `dllPath` se resuelve al .dll/.so/.dylib más
///     cercano bajo el bundle (o vacío si no hay ninguno).
///
/// No lanza. Si `root` no existe o no es un directorio, devuelve lista vacía.
///
/// :param root: directorio a escanear (p. ej. assets/audio-plugins/).
/// :returns: lista de bundles reales hallados (vacía si no hay ninguno).
std::vector<BundleInfo> scanDirectory(const std::string& root);

/// Host de plugins VST3: ciclo de vida + embebido del editor gráfico.
///
/// Uso típico (todo en control path, con el grafo de audio detenido):
///   Vst3Host host;
///   auto bundles = scanDirectory("assets/audio-plugins");
///   if (host.load(bundles[0]) && host.attachEditor(hwnd)) { /* editor visible */ }
///   // ... más tarde, con audio detenido:
///   host.reset();
///
/// RT-safety: el `process()` del IAudioProcessor es el camino RT (lo invoca
/// el Proceso A); load/unload/reset/attachEditor son control path y solo se
/// llaman con el plugin sin procesar.
class Vst3Host {
   public:
    Vst3Host() = default;
    ~Vst3Host();

    Vst3Host(const Vst3Host&) = delete;
    Vst3Host& operator=(const Vst3Host&) = delete;

    /// Carga el bundle: abre el .dll, resuelve `ModuleEntry`, crea el
    /// IAudioProcessor y el IEditControllerEx1 (el editor gráfico completo).
    ///
    /// Control path. Con `_WIN32` + SDK (`BA_PLUGIN_HOST_HAVE_VST3`) es la
    /// implementación real (LoadLibraryW + QI); sin SDK es un fallback
    /// headless que loguea y devuelve false. Si ya hay un plugin cargado, se
    /// descarta antes de cargar el nuevo.
    ///
    /// :param bundle: bundle a cargar (dllPath debe ser válido).
    /// :returns: true si el processor Y el controller quedaron creados.
    bool load(const BundleInfo& bundle);

    /// Descarga el plugin (Release de controller/processor, FreeLibrary) y
    /// deja el host vacío. Idempotente: si no hay nada cargado, no hace nada.
    /// Control path: llamar con el grafo de audio detenido.
    void unload();

    /// Cierra el editor (si estaba abierto) y descarga el plugin. Es
    /// unload() + limpieza del estado de editor; idempotente.
    void reset();

    /// Embebe el editor gráfico del plugin dentro de `parentHwnd`
    /// (requisito "toda la gráfica": el editor VST3 completo, no un subset).
    ///
    /// Control path. Solo tiene sentido tras un load() exitoso y con un
    /// controller presente. Con `_WIN32` + SDK (`BA_PLUGIN_HOST_HAVE_VST3`)
    /// usa IEditControllerEx1::createView; sin SDK es un fallback headless
    /// que loguea y devuelve false.
    ///
    /// :param parentHwnd: HWND del contenedor donde se embebe el editor.
    /// :returns: true si la vista del editor se creó y quedó embebida.
    bool attachEditor(void* parentHwnd);

    /// true si hay un plugin cargado (processor != nullptr).
    bool isLoaded() const;

   private:
    // Estado del plugin cargado. En el stub no-Windows estos punteros no se
    // usan (los tipos del SDK no se incluyen fuera de _WIN32).
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace plugin
} // namespace ba
