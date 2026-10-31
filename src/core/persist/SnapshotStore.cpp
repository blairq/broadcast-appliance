// SnapshotStore.cpp
//
// Implementacion del writer/reader del snapshot con doble generacion
// alternada (spec §11.2). Tres ideas de diseno:
//
//   1) Atomicidad y durabilidad: las del escritor unico (persist/Escritor.hpp:
//      temporal + fsync/F_FULLFSYNC/FlushFileBuffers + rename + la carpeta), con
//      el formato de cabecera de persist/Generaciones.hpp. Un lector nunca ve un
//      archivo a medio escribir: o es la generacion anterior completa o la nueva.
//
//   2) Debounce con steady_clock: maximo una escritura fisica cada 500 ms
//      (kDebounceMs). steady_clock es inmune a cambios de hora del sistema
//      (a diferencia de system_clock), por lo que el debounce no se ve
//      afectado si el usuario cambia la hora. Los eventos criticos
//      (immediate=true) bypasean el debounce.
//
//   3) No se pierde estado: ante un fallo de disco (o disco "ausente" por
//      fault-injection) el doc queda en memoria (pendingDoc_) y se marca un
//      reintento pendiente; no se pierde. El caller lo resuelve con flushNow()
//      cuando el disco vuelve.
//
// Ninguna funcion publica lanza: init()/readLatest() devuelven Status;
// scheduleWrite()/flushNow() son no-fallibles sobre un store inicializado.

#include "SnapshotStore.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "persist/Escritor.hpp"
#include "persist/Generaciones.hpp"

namespace ba::persist {

// --- RAII -------------------------------------------------------------------

SnapshotStore::~SnapshotStore() = default;

SnapshotStore::SnapshotStore(SnapshotStore&& other) noexcept
    : dir_(std::move(other.dir_)), pathA_(std::move(other.pathA_)), pathB_(std::move(other.pathB_)),
      initialized_(other.initialized_), nextIsA_(other.nextIsA_), lastWriteTime_(other.lastWriteTime_),
      pendingDoc_(std::move(other.pendingDoc_)), hasPending_(other.hasPending_),
      diskPresent_(other.diskPresent_), faultStep_(other.faultStep_), physicalWrites_(other.physicalWrites_) {
    other.initialized_ = false;
    other.hasPending_ = false;
}

SnapshotStore& SnapshotStore::operator=(SnapshotStore&& other) noexcept {
    if (this != &other) {
        dir_ = std::move(other.dir_);
        pathA_ = std::move(other.pathA_);
        pathB_ = std::move(other.pathB_);
        initialized_ = other.initialized_;
        nextIsA_ = other.nextIsA_;
        lastWriteTime_ = other.lastWriteTime_;
        pendingDoc_ = std::move(other.pendingDoc_);
        hasPending_ = other.hasPending_;
        diskPresent_ = other.diskPresent_;
        faultStep_ = other.faultStep_;
        physicalWrites_ = other.physicalWrites_;
        other.initialized_ = false;
        other.hasPending_ = false;
    }
    return *this;
}

// --- init -------------------------------------------------------------------

Status SnapshotStore::init(const std::string& dir, const std::string& fileA, const std::string& fileB) {
    if (dir.empty()) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (fileA.empty() || fileB.empty() || fileA == fileB) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    dir_ = dir;
    pathA_ = (std::filesystem::path(dir) / fileA).string();
    pathB_ = (std::filesystem::path(dir) / fileB).string();
    initialized_ = true;
    nextIsA_ = true;
    lastWriteTime_ = std::chrono::steady_clock::time_point{};
    hasPending_ = false;
    physicalWrites_ = 0;
    return Status::success();
}

// --- helpers ----------------------------------------------------------------

std::chrono::steady_clock::time_point SnapshotStore::now() const { return std::chrono::steady_clock::now(); }

bool SnapshotStore::debounceAllowsWrite() const {
    // Sin escritura previa: siempre permite (lastWriteTime_ == epoch).
    if (lastWriteTime_ == std::chrono::steady_clock::time_point{}) {
        return true;
    }
    const auto elapsed = now() - lastWriteTime_;
    return elapsed >= kDebounceMs;
}

// --- physicalWrite ----------------------------------------------------------

Status SnapshotStore::physicalWrite(const StateDoc& doc, bool targetIsA) {
    // Disco ausente (fault-injection): no toca el disco; el doc queda en
    // memoria (lo gestiona el caller) y se marca reintento pendiente.
    if (!diskPresent_) {
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }

    const std::string targetPath = targetIsA ? pathA_ : pathB_;
    Status st = atomicWrite(doc, targetPath);
    if (!st.ok) {
        // Fallo en Temp/Flush/Rename (o fault-injection en ese paso).
        // La generacion destino NO se actualizo; no se alterna.
        return st;
    }

    // Exito: alterna la generacion para la proxima escritura.
    nextIsA_ = !targetIsA;
    lastWriteTime_ = now();
    ++physicalWrites_;
    return Status::success();
}

// --- atomicWrite ------------------------------------------------------------

Status SnapshotStore::atomicWrite(const StateDoc& doc, const std::string& targetPath) {
    // Los pasos que fallan a propósito (tests): la generación destino no se toca.
    if (faultStep_ != Step::None) {
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }
    // El formato de las generaciones y el escritor durable son los de todo el
    // estado (persist/Generaciones.hpp, persist/Escritor.hpp).
    if (!escribir(targetPath, conCabecera(doc.sequence, doc.serialize()), Durabilidad::Critica)) {
        return Status::fail(static_cast<int>(ErrorCode::kIo));
    }
    return Status::success();
}

// --- scheduleWrite / flushNow -----------------------------------------------

void SnapshotStore::scheduleWrite(const StateDoc& doc, bool immediate) {
    if (!initialized_) {
        return;
    }
    // Siempre actualiza el doc pendiente en memoria (no se pierde estado).
    pendingDoc_ = doc;
    hasPending_ = true;

    const bool shouldWrite = immediate || debounceAllowsWrite();
    if (!shouldWrite) {
        // Debounced: queda pendiente para la proxima oportunidad.
        return;
    }

    const bool targetIsA = nextIsA_;
    Status st = physicalWrite(doc, targetIsA);
    if (st.ok) {
        hasPending_ = false;
    }
    // Si fallo (disco ausente / fault), el doc sigue en memoria (hasPending_
    // sigue true) y se reintenta con flushNow() o el proximo scheduleWrite.
}

void SnapshotStore::flushNow() {
    if (!initialized_ || !hasPending_) {
        return;
    }
    const bool targetIsA = nextIsA_;
    Status st = physicalWrite(pendingDoc_, targetIsA);
    if (st.ok) {
        hasPending_ = false;
    }
}

// --- readLatest -------------------------------------------------------------

Status SnapshotStore::readLatest(StateDoc& out) {
    if (!initialized_) {
        return Status::fail(static_cast<int>(ErrorCode::kNotInitialized));
    }

    StateDoc docA, docB;
    const bool aValid = readGeneration(pathA_, docA);
    const bool bValid = readGeneration(pathB_, docB);

    if (!aValid && !bValid) {
        return Status::fail(static_cast<int>(ErrorCode::kNotFound));
    }

    // Toma la mas nueva (mayor secuencia) entre las validas.
    if (aValid && bValid) {
        out = (docA.sequence >= docB.sequence) ? docA : docB;
    } else if (aValid) {
        out = docA;
    } else {
        out = docB;
    }
    return Status::success();
}

bool SnapshotStore::readGeneration(const std::string& path, StateDoc& out) const {
    std::string content;
    std::string body;
    std::uint64_t seq = 0;
    if (!leerArchivo(path, content) || !sinCabecera(content, seq, body)) {
        return false; // no existe, cabecera malformada o checksum que no coincide
    }
    return StateDoc::deserialize(body, out);
}

// --- sha256Hex --------------------------------------------------------------

std::string SnapshotStore::sha256Hex(const std::string& body) { return persist::sha256Hex(body); }

// --- fault-injection --------------------------------------------------------

void SnapshotStore::setDiskPresent(bool present) { diskPresent_ = present; }

void SnapshotStore::setFaultAtStep(Step step) { faultStep_ = step; }

std::uint64_t SnapshotStore::physicalWrites() const { return physicalWrites_; }

} // namespace ba::persist
