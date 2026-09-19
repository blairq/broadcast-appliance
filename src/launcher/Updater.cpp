// ============================================================================
// src/launcher/Updater.cpp — ver Updater.hpp
// ============================================================================

#include "Updater.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <system_error>
#include <thread>

#include "Integrity.hpp"
#include "Log.hpp"
#include "platform/SingleInstance.hpp"

namespace ba::launcher {

namespace fs = std::filesystem;

namespace {

constexpr const char* kStage = ".actualizacion-nueva";
constexpr const char* kPrevious = ".actualizacion-anterior";
constexpr int kKeepBackups = 5;

#ifdef _WIN32
constexpr const char* kLauncherName = "BroadcastAppliance.exe";
constexpr const char* kAppRel = "bin/64bit/ba-app.exe";
#else
constexpr const char* kLauncherName = "BroadcastAppliance";
constexpr const char* kAppRel = "bin/64bit/ba-app";
#endif

void log(Log::Level lvl, const std::string& msg) { Log::write(lvl, "actualizacion", 0, msg); }

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

bool copyTree(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) {
        fs::create_directories(to, ec);
        fs::copy(from, to,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks |
                     fs::copy_options::overwrite_existing,
                 ec);
    } else {
        fs::create_directories(to.parent_path(), ec);
        fs::copy(from, to, fs::copy_options::copy_symlinks | fs::copy_options::overwrite_existing, ec);
    }
    if (ec) err = from.string() + ": " + ec.message();
    return !ec;
}

/// Respaldo de Credenciales/ y Config/; deja los ultimos kKeepBackups.
bool backupData(const fs::path& root, fs::path& out, std::string& err) {
    const fs::path base = root / "Respaldos";
    out = base / stamp();
    for (const char* d : {"Credenciales", "Config"}) {
        if (fs::exists(root / d) && !copyTree(root / d, out / d, err)) return false;
    }
    std::error_code ec;
    std::vector<fs::path> all;
    for (const auto& e : fs::directory_iterator(base, ec))
        if (e.is_directory()) all.push_back(e.path());
    std::sort(all.begin(), all.end());
    for (std::size_t i = 0; i + kKeepBackups < all.size(); ++i)
        fs::remove_all(all[i], ec);
    return true;
}

/// Intercambia `names` entre la raiz y `incoming` (la version anterior queda en
/// `previous`). Todo o nada.
bool swapIn(const fs::path& root, const fs::path& incoming, const fs::path& previous,
            const std::vector<std::string>& names, std::string& err) {
    std::error_code ec;
    fs::remove_all(previous, ec);
    fs::create_directories(previous, ec);
    std::vector<std::string> movedOld;
    std::vector<std::string> movedNew;
    const auto undo = [&]() {
        std::error_code e2;
        for (const auto& n : movedNew)
            fs::rename(root / n, incoming / n, e2);
        for (const auto& n : movedOld)
            fs::rename(previous / n, root / n, e2);
    };
    for (const auto& n : names) {
        if (fs::exists(root / n, ec) || fs::is_symlink(root / n, ec)) {
            fs::rename(root / n, previous / n, ec);
            if (ec) {
                err = "no se pudo apartar " + n + ": " + ec.message();
                undo();
                return false;
            }
            movedOld.push_back(n);
        }
        fs::rename(incoming / n, root / n, ec);
        if (ec) {
            err = "no se pudo instalar " + n + ": " + ec.message();
            undo();
            return false;
        }
        movedNew.push_back(n);
    }
    return true;
}

} // namespace

bool isDataEntry(const std::string& name) {
    // Agentes/: los agentes de IA que la persona instalo (cientos de MB): una
    // version nueva del sistema trae sus propias versiones fijadas y la pestaña
    // Asistente ofrece actualizarlos; no se borran al actualizar.
    static const char* kData[] = {"Config",  "Credenciales", "Logs",      "Recordings", "Shares",
                                  "Runtime", "Cache",        "Respaldos", "Agentes",    "Media"};
    if (name.rfind(".actualizacion", 0) == 0) return true;
    return std::any_of(std::begin(kData), std::end(kData), [&](const char* d) { return name == d; });
}

UpdateResult runUpdate(const UpdateOptions& opt) {
    UpdateResult r;
    const fs::path root = fs::weakly_canonical(opt.root);
    std::error_code ec;

    // 1) La app tiene que estar cerrada.
    platform::SingleInstance lock;
    std::string lockErr;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opt.waitLockMs);
    while (!platform::SingleInstance::acquire(opt.lockName, lock, &lockErr)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            r.message = "El sistema sigue abierto: cerralo y volve a intentar (" + lockErr + ").";
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    const fs::path previous = root / kPrevious;
    fs::path incoming;
    std::vector<std::string> names;
    if (opt.rollback) {
        incoming = previous;
        if (!fs::is_directory(previous, ec)) {
            r.message = "No hay una version anterior guardada para volver atras.";
            return r;
        }
        for (const auto& e : fs::directory_iterator(previous, ec))
            names.push_back(e.path().filename().string());
    } else {
        // 2) Validar la version nueva.
        const fs::path src = fs::weakly_canonical(opt.source);
        if (!fs::exists(src / kLauncherName) || !fs::exists(src / kAppRel)) {
            r.message = "Esa carpeta no parece una version de Broadcast Appliance (falta " +
                        std::string(kLauncherName) + " o " + kAppRel + ").";
            return r;
        }
        const auto rel = src.lexically_relative(root);
        if (src == root || (!rel.empty() && *rel.begin() != "..")) {
            r.message = "La version nueva tiene que estar FUERA de la carpeta del sistema.";
            return r;
        }
        if (fs::exists(src / "manifest.json")) {
            const IntegrityReport rep = Integrity::verify(src.wstring());
            if (!rep.ok) {
                r.message = "La version nueva esta danada (" + std::to_string(rep.missing) +
                            " archivos faltan, " + std::to_string(rep.mismatch) +
                            " no coinciden con manifest.json). No se instalo nada.";
                return r;
            }
        }
        for (const auto& e : fs::directory_iterator(src, ec)) {
            const std::string n = e.path().filename().string();
            if (!isDataEntry(n)) names.push_back(n);
        }
        std::sort(names.begin(), names.end());
        // Copia completa ANTES de tocar nada.
        incoming = root / kStage;
        fs::remove_all(incoming, ec);
        fs::create_directories(incoming, ec);
        for (const auto& n : names) {
            std::string err;
            if (!copyTree(src / n, incoming / n, err)) {
                fs::remove_all(incoming, ec);
                r.message = "No se pudo copiar la version nueva (" + err + "). No se instalo nada.";
                return r;
            }
        }
    }

    // 3) Respaldo de los datos.
    std::string err;
    if (!backupData(root, r.backup, err)) {
        if (!opt.rollback) fs::remove_all(incoming, ec);
        r.message = "No se pudo respaldar Credenciales/ y Config/ (" + err + "). No se instalo nada.";
        return r;
    }

    // 4) Intercambio todo-o-nada.
    fs::path stash = opt.rollback ? root / kStage : previous;
    if (opt.rollback) fs::remove_all(stash, ec);
    if (!swapIn(root, incoming, stash, names, err)) {
        r.message = "No se pudo reemplazar el programa (" + err + "). Quedo la version anterior.";
        return r;
    }
    if (opt.rollback) {
        // La que se deshizo pasa a ser "anterior" (se puede volver a cambiar).
        fs::remove_all(previous, ec);
        fs::rename(stash, previous, ec);
    } else {
        fs::remove_all(incoming, ec);
    }

    // 6) Caches de la version vieja.
    fs::remove_all(root / "Cache", ec);
    fs::create_directories(root / "Cache", ec);

    r.ok = true;
    r.replaced = names;
    r.message = std::string(opt.rollback ? "Se volvio a la version anterior" : "Actualizado") + " (" +
                std::to_string(names.size()) + " elementos del programa). Datos intactos; respaldo en " +
                r.backup.lexically_relative(root).string() + ".";
    log(Log::Level::Info, r.message);
    return r;
}

} // namespace ba::launcher
