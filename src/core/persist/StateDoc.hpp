// StateDoc.hpp
//
// Proposito:
//   Define el UNICO modelo de estado del Broadcast Appliance (spec §11.1:
//   "Hay un unico modelo de estado, que es la fuente de verdad y tiene
//   schemaVersion. El snapshot es su serializacion."). StateDoc ES la fuente de
//   verdad; SnapshotStore (persist/SnapshotStore) solo lo serializa y lo
//   persiste. No depende de Qt ni de libobs. El encode/decode JSON usa
//   nlohmann/json (header-only) porque el esquema es fijo: un objeto con tres
//   campos escalares y un mapa de secciones (nombre -> blob JSON opaco).
//
// Modelo de threading:
//   StateDoc es un POD de datos plano y NO es thread-safe por si mismo: quien
//   lo muta debe serializarlo. En la practica lo produce un solo hilo (el de
//   control de A) y lo pasa por valor a SnapshotStore::scheduleWrite(); la
//   instancia es trivialmente copiable (mapa + escalares), por lo que no hay
//   estado compartido que proteger.
//
// Estrategia de errores:
//   serialize() no falla (no lanza, no devuelve estado): siempre produce un
//   string JSON valido para cualquier StateDoc. deserialize() NUNCA lanza:
//   devuelve false si el texto no es un JSON del esquema esperado (malformed,
//   campos ausentes, tipo incorrecto, seccion no-string, etc.) y deja `out` en
//   un estado indeterminado; el caller debe tratar false como "snapshot
//   invalido" (por eso SnapshotStore lo usa para descartar una generacion
//   corrupta).
//
// Formato del wire (JSON, UTF-8, esquema fijo):
//   {
//     "schemaVersion": <int>,
//     "sequence":      <uint64>,
//     "cleanShutdown": <bool>,
//     "sections":      { "<nombre>": "<blob JSON como string>", ... }
//   }
//   Cada seccion es un blob JSON opaco que se transporta como STRING (el
//   caller decide su contenido: devices, scenes, volumes, lower-third,
//   tunnel, live...). StateDoc no lo interpreta: solo lo escapa/descapa.
//   Los nombres de seccion y los blobs pueden contener comillas,
//   backslashes, saltos de linea y cualquier byte UTF-8.

#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace ba::persist {

/// Modelo unico de estado (fuente de verdad, spec §11.1).
///
/// `schemaVersion` permite a un futuro lector detectar que el esquema cambio
/// y decidir si puede interpretar el snapshot (o migrarlo). `sequence` es el
/// numero monotono que SnapshotStore usa para elegir la generacion mas nueva
/// al leer. `cleanShutdown` lo fija SOLO el caller en un cierre ordenado
/// (spec §11.2); el store persiste lo que le dan, sin tocarlo. `sections`
/// guarda cada area de estado como un blob JSON opaco por nombre.
struct StateDoc {
    int schemaVersion = 1;
    std::uint64_t sequence = 0;
    bool cleanShutdown = false;
    std::map<std::string, std::string> sections;

    /// Serializa el documento a JSON (UTF-8).
    ///
    /// No lanza ni falla: para cualquier StateDoc produce un objeto JSON
    /// valido del esquema fijo. El orden de las claves es fijo (schemaVersion,
    /// sequence, cleanShutdown, sections) y el orden de las secciones es el del
    /// std::map (alfabetico por clave), por lo que la salida es determinista
    /// byte a byte para un mismo documento (importante para tests y para que
    /// el checksum sea estable).
    ///
    /// :returns: el JSON serializado.
    std::string serialize() const;

    /// Deserializa un JSON del esquema fijo a `out`.
    ///
    /// No lanza. Devuelve false (y deja `out` indeterminado) si el texto no es
    /// un objeto JSON con los cuatro campos esperados y los tipos correctos, o
    /// si alguna seccion no es un string. `out` se reemplaza por completo en
    /// exito. Las claves desconocidas de nivel superior se ignoran
    /// (forward-compat).
    ///
    /// :param text: JSON a interpretar.
    /// :param out: recibe el documento en exito.
    /// :returns: true si se interpreto un documento valido.
    static bool deserialize(const std::string& text, StateDoc& out);
};

} // namespace ba::persist
