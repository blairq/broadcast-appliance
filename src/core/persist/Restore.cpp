// Restore.cpp
//
// Implementacion de la RESTORACION (spec §11.3) y los flujos de RECUPERACION.
// Dos ideas de diseno:
//
//   1) Reuso, no re-implantacion. La verificacion de integridad (checksum
//      SHA-256 + deserializacion) se delega en el oracle PUBLICO de
//      SnapshotStore, readLatest(): se copia el archivo a un store sintetico
//      (dir temporal, nombre de generacion fijo) y se lee. Asi el checksum que
//      se verifica es el MISMO que SnapshotStore escribio en el header, sin
//      re-implantarlo (sha256Hex es privado y SnapshotStore no se modifica). El
//      JSON del cuerpo se interpreta con StateDoc::deserialize (nlohmann,
//      esquema fijo) solo para DESAMBIGUAR el fallo (checksum vs. cuerpo no
//      interpretable). No se re-implanta JSON a mano.
//
//   2) Sin excepciones, con clasificacion. Ninguna funcion lanza. Los fallos se
//      mapean a RestoreResult::Status (Missing / ChecksumMismatch / Corrupt) y
//      `out` solo se rellena en Ok. En recover() los snapshots malos se SALTAN
//      (no son el resultado), de modo que la corrupcion de la generacion mas
//      nueva cae a la anterior: es el "plan B" de recuperacion (spec §11.3).
//
// El formato de archivo debe coincidir con SnapshotStore (§11.2):
//   Linea 1 (header):  #BA-SNAP v1 seq=<uint64> sha256=<hex64>
//   Linea 2..N (body):  el JSON serializado de StateDoc.
//   El checksum es SHA-256 sobre los bytes del BODY (sin el header).
//
// RT-safety: esto es I/O de archivo + hash (costoso, con asignaciones); se
// llama en el hilo de control / en el arranque, nunca en la callback de audio.

#include "Restore.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "SnapshotStore.hpp"

namespace ba::persist {

namespace {

// --- formato de archivo (debe coincidir con SnapshotStore) ------------------

constexpr const char* kHeaderPrefix = "#BA-SNAP v1 seq=";
constexpr const char* kShaKey = "sha256=";

// Header extraido de un snapshot: secuencia + checksum esperado (hex, 64).
struct HeaderInfo {
    std::uint64_t seq = 0;
    std::string shaHex;
};

// Convierte a minuscula (solo ASCII; basta para el patron de nombre).
std::string toLowerAscii(const std::string& s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

// Un nombre de generacion retenida sigue el patron `snapshot.*.json`
// (snapshot.A.json / snapshot.B.json por defecto, y cualquier generacion
// personalizada con la misma convencion). Excluye los temporales
// (`snapshot.A.json.tmp`) y los no-snapshot.
bool isSnapshotName(const std::string& name) {
    const std::string lower = toLowerAscii(name);
    constexpr const char* kPrefix = "snapshot.";
    constexpr const char* kSuffix = ".json";
    const std::size_t minLen =
        std::char_traits<char>::length(kPrefix) + std::char_traits<char>::length(kSuffix);
    if (lower.size() < minLen) {
        return false;
    }
    return startsWith(lower, kPrefix) && endsWith(lower, kSuffix);
}

// Convierte un hex (mayuscula o minuscula) a uint64; false si no es hex.
bool hexToU64(const std::string& s, std::uint64_t& out) {
    if (s.empty() || s.size() > 20) {
        return false;
    }
    std::uint64_t v = 0;
    for (char c : s) {
        const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const int digit = (lc >= '0' && lc <= '9')   ? (lc - '0')
                          : (lc >= 'a' && lc <= 'f') ? (lc - 'a' + 10)
                                                     : -1;
        if (digit < 0) {
            return false;
        }
        v = (v << 4) | static_cast<std::uint64_t>(digit);
    }
    out = v;
    return true;
}

// Extrae el header (seq + sha256) del contenido. False si la primera linea no
// es un header valido (mismo formato que el de SnapshotStore).
bool parseHeader(const std::string& content, HeaderInfo& out) {
    if (content.rfind(kHeaderPrefix, 0) != 0) {
        return false;
    }
    const std::size_t nl = content.find('\n');
    if (nl == std::string::npos) {
        return false;
    }
    const std::string line = content.substr(0, nl);
    const std::size_t seqStart = std::char_traits<char>::length(kHeaderPrefix);
    const std::size_t spacePos = line.find(' ', seqStart);
    if (spacePos == std::string::npos) {
        return false;
    }
    const std::string seqStr = line.substr(seqStart, spacePos - seqStart);
    if (!hexToU64(seqStr, out.seq)) {
        return false;
    }
    const std::string rest = line.substr(spacePos + 1);
    if (rest.rfind(kShaKey, 0) != 0) {
        return false;
    }
    out.shaHex = rest.substr(std::char_traits<char>::length(kShaKey));
    if (out.shaHex.size() != 64) {
        return false;
    }
    return true;
}

// Lee un archivo completo a memoria; false si no existe / no se lee.
bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Lista los candidatos de snapshot retenidos en `dir` (orden alfabetico,
// determinista). Vacio si `dir` no existe / no es directorio.
std::vector<std::string> listCandidates(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (isSnapshotName(name)) {
            out.push_back(entry.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// --- verificacion de UN archivo (compartida por restore y recover) ----------
//
// Devuelve Ok / ChecksumMismatch / Corrupt (o Missing si no se pudo leer; el
// caller lo traduce: restore -> Missing, recover -> saltea). `out` solo se
// rellena en Ok.
RestoreResult validateFile(const std::string& path, StateDoc& out) {
    std::string content;
    if (!readFileAll(path, content)) {
        return RestoreResult{RestoreResult::Status::Missing, ba::i18n::llenar(ba::i18n::traducir("persist", "no se pudo leer: %1"), {path})};
    }

    HeaderInfo hdr;
    if (!parseHeader(content, hdr)) {
        return RestoreResult{RestoreResult::Status::Corrupt, ba::i18n::llenar(ba::i18n::traducir("persist", "header malformado: %1"), {path})};
    }

    // El body es todo lo despues del primer '\n' (sin el '\n' final).
    const std::size_t nl = content.find('\n');
    std::string body = content.substr(nl + 1);
    if (!body.empty() && body.back() == '\n') {
        body.pop_back();
    }

    // Oracle de checksum (reuso del API PUBLICO de SnapshotStore): se arma un
    // store sintetico cuyo fileA es el archivo a validar (fileB es un nombre
    // que no existe) y se lee con readLatest(), que valida el checksum SHA-256
    // del cuerpo contra el del header. Asi el checksum verificado es el MISMO
    // que SnapshotStore escribio, sin re-implantarlo (sha256Hex es privado).
    SnapshotStore store;
    const std::filesystem::path p(path);
    const std::string dir = p.has_parent_path() ? p.parent_path().string() : ".";
    const std::string fileName = p.filename().string();
    const Status st = store.init(dir, fileName, fileName + ".other");
    if (!st.ok) {
        return RestoreResult{RestoreResult::Status::Missing, ba::i18n::llenar(ba::i18n::traducir("persist", "no se pudo inicializar el store: %1"), {path})};
    }
    StateDoc doc;
    const Status r = store.readLatest(doc);
    if (r.ok) {
        out = std::move(doc);
        return RestoreResult{RestoreResult::Status::Ok, ba::i18n::llenar(ba::i18n::traducir("persist", "ok: %1"), {path})};
    }

    // Oracle fallo con header valido: es checksum o cuerpo. Si el cuerpo SI es
    // un StateDoc valido, el fallo es del checksum (bytes alterados). Si el
    // cuerpo no se interpreta, es Corrupt.
    StateDoc probe;
    if (StateDoc::deserialize(body, probe)) {
        return RestoreResult{RestoreResult::Status::ChecksumMismatch,
                             ba::i18n::llenar(ba::i18n::traducir("persist", "checksum no coincide en %1 (esperado %2)"), {path, hdr.shaHex})};
    }
    return RestoreResult{RestoreResult::Status::Corrupt, ba::i18n::llenar(ba::i18n::traducir("persist", "cuerpo no es un StateDoc JSON valido: %1"), {path})};
}

} // namespace

// --- restore ----------------------------------------------------------------

RestoreResult Restore::restore(const std::string& snapshotPath, StateDoc& out) {
    if (snapshotPath.empty()) {
        return RestoreResult{RestoreResult::Status::Missing, ba::i18n::traducir("persist", "ruta vacia")};
    }
    return validateFile(snapshotPath, out);
}

// --- recover ----------------------------------------------------------------

RestoreResult Restore::recover(const std::string& dir, StateDoc& out) {
    if (dir.empty()) {
        return RestoreResult{RestoreResult::Status::Missing, ba::i18n::traducir("persist", "dir vacio")};
    }

    const std::vector<std::string> candidates = listCandidates(dir);
    if (candidates.empty()) {
        return RestoreResult{RestoreResult::Status::Missing, ba::i18n::llenar(ba::i18n::traducir("persist", "sin snapshots en %1"), {dir})};
    }

    bool found = false;
    std::uint64_t bestSeq = 0;
    for (const std::string& path : candidates) {
        StateDoc doc;
        const RestoreResult r = validateFile(path, doc);
        if (r.status != RestoreResult::Status::Ok) {
            continue; // corrupto / checksum: se saltea, no es el resultado.
        }
        const std::uint64_t seq = doc.sequence;
        // Mayor secuencia gana (el mas nuevo). Empate: el primero en orden
        // alfabetico de ruta (los candidatos van ordenados) se queda.
        if (!found || seq > bestSeq) {
            bestSeq = seq;
            out = std::move(doc);
            found = true;
        }
    }

    if (!found) {
        return RestoreResult{RestoreResult::Status::Missing, ba::i18n::llenar(ba::i18n::traducir("persist", "ningun snapshot bueno en %1"), {dir})};
    }
    return RestoreResult{RestoreResult::Status::Ok,
                         ba::i18n::llenar(ba::i18n::traducir("persist", "recuperado seq=%1 en %2"), {std::to_string(bestSeq), dir})};
}

} // namespace ba::persist
