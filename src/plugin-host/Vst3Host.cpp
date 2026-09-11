// ============================================================================
// src/plugin-host/Vst3Host.cpp — Host VST3 (spec D6/§7.5, LSP como componente
// CORE)
//
// Implementación de `ba::plugin`:
//   - scanDirectory(): puro C++ (std::filesystem), cross-platform, testeable
//     en Linux. Un `.vst3` es un DIRECTORIO con subdirectorio `Contents/`.
//   - Vst3Host: ciclo de vida del plugin + embebido del editor gráfico.
//     * Windows (_WIN32): REAL — LoadLibraryW, `ModuleEntry`, QI del
//       IAudioProcessor + IEditControllerEx1, createView sobre el HWND padre.
//       Usa los headers del VST3 SDK (MIT), traído por FetchContent SOLO en
//       Windows (if(WIN32) en el CMakeLists raíz).
//     * Linux/macOS: STUB funcional — loguea por stderr y devuelve
//       false/vacíos, para que el módulo compile OFFLINE (sin SDK, sin red).
//
// Modelo de hilos / RT-safety:
//   - load/unload/reset/attachEditor = CONTROL PATH (UI, con audio detenido).
//   - IAudioProcessor::process() = CAMINO RT (lo invoca el Proceso A). Este
//     módulo no toca el processor desde otro hilo una vez cargado.
//
// Política de errores: sin excepciones; load/attachEditor devuelven false y
// dejan el host consistente; scanDirectory devuelve vacío ante fallo.
// ============================================================================

#include "Vst3Host.hpp"

#include <cstdio>
#include <filesystem>

#if defined(_WIN32) && defined(BA_PLUGIN_HOST_HAVE_VST3)
// --- VST3 SDK (MIT) — solo cuando el SDK está presente --------------------
// Estos headers solo existen cuando el SDK se trajo por FetchContent
// (if(WIN32) en el CMakeLists raíz) Y el target definió
// BA_PLUGIN_HOST_HAVE_VST3. En Linux (o en un Windows sin SDK) no se
// incluyen: el módulo compila offline con el fallback headless.
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <windows.h>
#endif // defined(_WIN32) && defined(BA_PLUGIN_HOST_HAVE_VST3)

namespace ba {
namespace plugin {

namespace {

// --- scanDirectory: helpers puros (std::filesystem) -------------------------

/// Devuelve true si `p` es un directorio cuyo nombre termina en `.vst3` y
/// que contiene un subdirectorio `Contents/` (layout estándar VST3).
/// No lanza: ante cualquier error de filesystem devuelve false.
bool isRealBundle(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_directory(p, ec) || ec) {
        return false;
    }
    if (!p.filename().string().ends_with(".vst3")) {
        return false;
    }
    return std::filesystem::is_directory(p / "Contents", ec);
}

/// Busca el binario más cercano bajo `bundle` con una de las extensiones
/// dadas (recursivo). Devuelve la ruta o path vacío si no hay ninguno.
/// No lanza.
std::filesystem::path findNearestBinary(const std::filesystem::path& bundle,
                                        const std::vector<std::string>& exts) {
    std::error_code ec;
    std::filesystem::path best;
    size_t bestDepth = 0;
    if (!std::filesystem::exists(bundle, ec)) {
        return {};
    }
    // Nota: sin `skip_missing_status` (C++23); el error_code por iteración
    // cubre los elementos cuyo estado no se pudo resolver (C++20).
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(bundle, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code fec;
        if (!entry.is_regular_file(fec) || fec) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        for (const auto& want : exts) {
            if (ext == want) {
                const size_t depth =
                    static_cast<size_t>(std::distance(
                        bundle.begin(), entry.path().begin()));
                if (best.empty() || depth < bestDepth) {
                    best = entry.path();
                    bestDepth = depth;
                }
                break;
            }
        }
    }
    return best;
}

} // namespace

// --- scanDirectory ----------------------------------------------------------

std::vector<BundleInfo> scanDirectory(const std::string& root) {
    std::vector<BundleInfo> result;
    std::error_code ec;
    const std::filesystem::path rootPath(root);
    if (!std::filesystem::is_directory(rootPath, ec) || ec) {
        return result; // root inexistible/ilegible: lista vacía.
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(rootPath, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path p = entry.path();
        if (!isRealBundle(p)) {
            continue;
        }

        BundleInfo info;
        info.name = p.filename().string();
        // Quitar la extensión ".vst3" del nombre corto.
        if (info.name.size() > 5 && info.name.ends_with(".vst3")) {
            info.name = info.name.substr(0, info.name.size() - 5);
        }
        info.bundlePath = p.string();

#ifdef _WIN32
        // Layout estándar Windows: Contents/x86_64-win/<Name>.dll.
        const std::filesystem::path expected =
            p / "Contents" / "x86_64-win" / (info.name + ".dll");
        if (std::filesystem::is_regular_file(expected, ec)) {
            info.dllPath = expected.string();
        } else {
            const auto nearest = findNearestBinary(p, {".dll"});
            info.dllPath = nearest.empty() ? std::string{} : nearest.string();
        }
#else
        // Fuera de Windows: el binario más cercano (.dll/.so/.dylib) o vacío.
        const auto nearest = findNearestBinary(p, {".dll", ".so", ".dylib"});
        info.dllPath = nearest.empty() ? std::string{} : nearest.string();
#endif

        result.push_back(std::move(info));
    }
    return result;
}

// --- Vst3Host ---------------------------------------------------------------

#if defined(_WIN32) && defined(BA_PLUGIN_HOST_HAVE_VST3)

namespace {

// Punto de entrada estándar VST3: el .dll exporta `ModuleEntry`.
using ModuleEntryFn = Steinberg::FUnknown* (*)(Steinberg::FUnknown*, void*);

// Tipos del SDK v3.7.9 (MIT). Los interfaces viven en `Steinberg::Vst`
// (IAudioProcessor, IEditController) y `Steinberg` (IPlugView, FUnknown,
// TUID, kResultOk). Se traen a este scope para no ensuciar el código.
using Steinberg::FUnknown;
using Steinberg::IPlugView;
using Steinberg::TUID;
using Steinberg::kResultOk;
using Steinberg::Vst::IAudioProcessor;
using Steinberg::Vst::IEditController;

} // namespace

struct Vst3Host::Impl {
    HMODULE module = nullptr;
    FUnknown* moduleUnknown = nullptr; // el FUnknown raíz del módulo.
    FUnknown* processor = nullptr;     // IAudioProcessor (camino RT).
    FUnknown* controller = nullptr;    // IEditController (editor).
    IPlugView* view = nullptr;         // vista embebida (si está abierta).
};

Vst3Host::~Vst3Host() { reset(); }

bool Vst3Host::load(const BundleInfo& bundle) {
    // Si ya hay algo, lo descartamos para no dejar punteros colgantes.
    if (impl_) {
        reset();
    }
    if (bundle.dllPath.empty()) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load: bundle sin dllPath resuelto\n");
        return false;
    }

    const std::wstring wpath(bundle.dllPath.begin(), bundle.dllPath.end());
    HMODULE module = ::LoadLibraryW(wpath.c_str());
    if (!module) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load: LoadLibraryW falló para %s\n",
                     bundle.dllPath.c_str());
        return false;
    }

    auto entry =
        reinterpret_cast<ModuleEntryFn>(::GetProcAddress(module, "ModuleEntry"));
    if (!entry) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load: entry point `ModuleEntry` ausente\n");
        ::FreeLibrary(module);
        return false;
    }

    FUnknown* moduleUnknown = entry(nullptr, nullptr);
    if (!moduleUnknown) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load: ModuleEntry devolvió nullptr\n");
        ::FreeLibrary(module);
        return false;
    }

    // QI del IAudioProcessor (camino RT) y del IEditController (editor).
    // En el SDK v3.7.9 el IID de cada interface es su miembro estático `iid`
    // (no existe `FUID_IAudioProcessor`/`FUID_IEditControllerEx1`).
    IAudioProcessor* processor = nullptr;
    if (moduleUnknown->queryInterface(
            IAudioProcessor::iid, reinterpret_cast<void**>(&processor)) !=
        kResultOk) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load: QI IAudioProcessor falló\n");
        moduleUnknown->release();
        ::FreeLibrary(module);
        return false;
    }

    IEditController* controller = nullptr;
    if (moduleUnknown->queryInterface(
            IEditController::iid, reinterpret_cast<void**>(&controller)) !=
        kResultOk) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load: QI IEditController falló "
                     "(sin editor gráfico)\n");
        processor->release();
        moduleUnknown->release();
        ::FreeLibrary(module);
        return false;
    }

    impl_ = new Impl();
    impl_->module = module;
    impl_->moduleUnknown = moduleUnknown;
    impl_->processor = processor;
    impl_->controller = controller;
    return true;
}

void Vst3Host::unload() {
    if (!impl_) {
        return;
    }
    if (impl_->view) {
        impl_->view->release();
        impl_->view = nullptr;
    }
    if (impl_->controller) {
        impl_->controller->release();
        impl_->controller = nullptr;
    }
    if (impl_->processor) {
        impl_->processor->release();
        impl_->processor = nullptr;
    }
    if (impl_->moduleUnknown) {
        impl_->moduleUnknown->release();
        impl_->moduleUnknown = nullptr;
    }
    if (impl_->module) {
        ::FreeLibrary(impl_->module);
        impl_->module = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

void Vst3Host::reset() { unload(); }

bool Vst3Host::attachEditor(void* parentHwnd) {
    if (!impl_ || !impl_->controller || parentHwnd == nullptr) {
        std::fprintf(stderr,
                     "[ba_plugin_host] attachEditor: sin controller o sin HWND\n");
        return false;
    }
    // Embebe el editor completo del plugin dentro del HWND padre. En el SDK
    // v3.7.9 `IEditController::createView` solo recibe el tipo de vista
    // ("editor"); el HWND padre se pasa después vía `IPlugView::attached`.
    IPlugView* view =
        static_cast<IEditController*>(impl_->controller)->createView(
            Steinberg::Vst::ViewType::kEditor);
    if (!view) {
        std::fprintf(stderr,
                     "[ba_plugin_host] attachEditor: createView falló\n");
        return false;
    }
    if (view->attached(parentHwnd, "win32") != kResultOk) {
        std::fprintf(stderr,
                     "[ba_plugin_host] attachEditor: attached falló\n");
        view->release();
        return false;
    }
    impl_->view = view;
    return true;
}

bool Vst3Host::isLoaded() const {
    return impl_ != nullptr && impl_->processor != nullptr;
}

#else // fallback HEADLESS: no _WIN32, o Windows sin SDK (offline)

struct Vst3Host::Impl {
    bool loaded = false;
};

Vst3Host::~Vst3Host() { reset(); }

bool Vst3Host::load(const BundleInfo& bundle) {
    if (impl_) {
        reset();
    }
    if (bundle.dllPath.empty()) {
        std::fprintf(stderr,
                     "[ba_plugin_host] load (stub): bundle sin dllPath\n");
        return false;
    }
    std::fprintf(stderr,
                 "[ba_plugin_host] load (headless): VST3 real solo con SDK "
                 "(Windows); bundle=%s\n",
                 bundle.bundlePath.c_str());
    impl_ = new Impl();
    impl_->loaded = true;
    return false; // el stub no carga el processor de verdad.
}

void Vst3Host::unload() {
    if (impl_) {
        std::fprintf(stderr, "[ba_plugin_host] unload (headless)\n");
        delete impl_;
        impl_ = nullptr;
    }
}

void Vst3Host::reset() { unload(); }

bool Vst3Host::attachEditor(void* /*parentHwnd*/) {
    std::fprintf(stderr,
                 "[ba_plugin_host] attachEditor (headless): VST3 real solo "
                 "con SDK (Windows)\n");
    return false;
}

bool Vst3Host::isLoaded() const {
    return impl_ != nullptr && impl_->loaded;
}

#endif // defined(_WIN32) && defined(BA_PLUGIN_HOST_HAVE_VST3)

} // namespace plugin
} // namespace ba
