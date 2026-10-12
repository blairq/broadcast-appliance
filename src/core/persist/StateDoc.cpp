// StateDoc.cpp
//
// Implementacion del modelo unico de estado (spec §11.1) y de su
// serializacion JSON con nlohmann/json. Dos ideas de diseno:
//
//   1) El esquema es FIJO y plano (tres escalares + un mapa de secciones
//      string->string). Los blobs de seccion son opacos: se transportan como
//      string y StateDoc nunca los interpreta. nlohmann se encarga del escape
//      de comillas/backslashes/caracteres de control y del UTF-8.
//
//   2) Determinismo: serialize() construye un nlohmann::ordered_json e
//      inserta las claves en un orden fijo (schemaVersion, sequence,
//      cleanShutdown, sections) y las secciones en el orden del std::map
//      (alfabetico por clave). Asi un mismo StateDoc siempre produce el mismo
//      byte a byte. Eso hace estable el checksum que SnapshotStore calcula
//      sobre el cuerpo serializado y hace los tests reproducibles.
//
// Ninguna funcion publica lanza: serialize() siempre produce un string valido
// y deserialize() devuelve bool (false = malformado, `out` indeterminado).

#include "StateDoc.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>

namespace ba::persist {

// --- serialize --------------------------------------------------------------

std::string StateDoc::serialize() const {
    // ordered_json preserva el orden de insercion: claves fijas + secciones en
    // el orden del std::map (alfabetico) -> salida determinista byte a byte.
    nlohmann::ordered_json j;
    j["schemaVersion"] = schemaVersion;
    j["sequence"] = sequence;
    j["cleanShutdown"] = cleanShutdown;

    nlohmann::ordered_json sectionsObj = nlohmann::ordered_json::object();
    for (const auto& [name, blob] : sections) {
        sectionsObj[name] = blob;
    }
    j["sections"] = std::move(sectionsObj);
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// --- deserialize ------------------------------------------------------------

bool StateDoc::deserialize(const std::string& text, StateDoc& out) {
    nlohmann::ordered_json root;
    try {
        root = nlohmann::ordered_json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return false; // texto malformado (no es un JSON valido)
    }
    // El raiz debe ser un objeto (un snapshot no es un array ni un escalar).
    if (!root.is_object()) {
        return false;
    }

    const auto sv = root.find("schemaVersion");
    const auto seq = root.find("sequence");
    const auto cs = root.find("cleanShutdown");
    const auto sec = root.find("sections");
    if (sv == root.end() || seq == root.end() || cs == root.end() || sec == root.end()) {
        return false; // falta un campo obligatorio
    }
    if (!sv->is_number_integer()) {
        return false; // schemaVersion debe ser un entero
    }
    if (!seq->is_number_integer()) {
        return false; // sequence debe ser un entero
    }
    if (!cs->is_boolean()) {
        return false; // cleanShutdown debe ser bool
    }
    if (!sec->is_object()) {
        return false; // sections debe ser un objeto
    }

    // sequence es uint64: se acepta unsigned directo o signed no negativo.
    std::uint64_t seqVal = 0;
    if (seq->is_number_unsigned()) {
        seqVal = seq->get<std::uint64_t>();
    } else {
        const auto signedSeq = seq->get<std::int64_t>();
        if (signedSeq < 0) {
            return false; // sequence es unsigned
        }
        seqVal = static_cast<std::uint64_t>(signedSeq);
    }

    StateDoc doc;
    doc.schemaVersion = sv->get<int>();
    doc.sequence = seqVal;
    doc.cleanShutdown = cs->get<bool>();
    for (const auto& [name, value] : sec->items()) {
        if (!value.is_string()) {
            return false; // cada seccion debe ser un string (blob opaco)
        }
        doc.sections.emplace(name, value.get<std::string>());
    }

    out = std::move(doc);
    return true;
}

} // namespace ba::persist
