// ============================================================================
// src/core/persist/ArchivoConfig.cpp — ver ArchivoConfig.hpp
// ============================================================================
#include "persist/ArchivoConfig.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

#include <mutex>
#include <set>

namespace ba::persist {

namespace {
using json = nlohmann::json;

/// Un archivo que era una lista suelta pasa a ser {clave: [...]}: un archivo de
/// Config/ es siempre un objeto (lleva su esquema adentro).
PasoMigracion listaEn(const char* clave) {
    return [clave](json& j) {
        if (j.is_array()) j = json{{clave, std::move(j)}};
    };
}
/// Un mapa cuyas claves son datos (un nombre de fuente, un id de plugin…) pasa a
/// {clave: {...}}: así `esquema` no puede chocar con un dato.
PasoMigracion mapaEn(const char* clave) {
    return [clave](json& j) {
        if (j.is_object()) j = json{{clave, std::move(j)}};
    };
}
} // namespace

const std::vector<EsquemaConfig>& esquemasConfig() {
    // Un archivo por fila. Para cambiar el formato de uno: subir `actual` y agregar el
    // paso (el i lleva del esquema i+1 al i+2). app.json, gateway.json y launcher.json
    // no están: los lee y guarda arranque/AppSettings (con su copia .prev y el paso de
    // app.json, que necesita la traducción de tipos de la app).
    static const std::vector<EsquemaConfig> tabla = {
        {"alarmas.json"},
        {"alertas.json"},
        {"asistente.json"},
        {"asistente-automatizacion.json"},
        {"atajos.json", 2, {listaEn("atajos")}},
        {"atencion.json"},
        {"audio-escucha.json"},
        {"audio-matriz.json"},
        {"audio-plantillas.json", 2, {mapaEn("plantillas")}},
        {"audio-racks.json", 2, {mapaEn("racks")}},
        {"botonera-dispositivos.json"},
        {"botonera-satellite.json"},
        {"botonera.json"},
        {"camaras.json"},
        {"carruseles.json"},
        {"cc.json"},
        {"conferencia.json"},
        {"control-sistema.json"},
        {"control-windows.json"}, // el nombre de antes de control-sistema.json
        {"control-web.json"},
        {"correccion-subtitulos.json"},
        {"cuerpo.json"},
        {"debate.json"},
        {"destinos.json", 2, {listaEn("destinos")}},
        {"disco.json"},
        {"dispositivos.json"},
        {"drivers.json"},
        {"dsk.json"},
        {"escenas.json"},
        {"glosario-traduccion.json"},
        {"historico.json"},
        {"interfaz.json"},
        {"loudness.json"},
        {"mcp-aire.json"},
        {"mezclador-vista.json"},
        {"modelo-propio.json"},
        {"modo-vivo.json"},
        {"monitores.json"},
        {"multiview.json"},
        {"notificadores.json", 2, {listaEn("notificadores")}},
        {"orden-del-dia.json"},
        {"pantallas.json", 2, {listaEn("pantallas")}}, // antes, la lista de pantallas a secas
        {"personas-opciones.json"},
        {"personas.json", 2, {listaEn("personas")}},
        {"plantillas.json", 2, {listaEn("plantillas")}}, // informes/
        {"playlist.json"},
        {"plugins-aprobados.json", 2, {mapaEn("aprobados")}},
        {"plugins.json"},
        {"ptz-interlock.json"},
        {"qr.json", 2, {listaEn("qr")}}, // también QR/qr.json del evento
        {"quorum.json"},
        {"recetas.json", 2, {listaEn("recetas")}},
        {"reconocedores.json"},
        {"respaldo-entradas.json", 2, {mapaEn("entradas")}},
        {"retornos.json"},
        {"riel.json"},
        {"roles.json"},
        {"rotulo-traduccion.json"},
        {"salidas.json"},
        {"seguir-microfono.json"},
        {"seguridad.json"},
        {"sesion-remota.json"},
        {"superres-banco.json"},
        {"superres-programador.json"},
        {"supervisor.json"},
        {"tiempos-analisis.json", 2, {listaEn("analisis")}},
        {"traducciones.json"},
        {"transiciones.json"},
        {"ui.json"},
        {"ventanas.json"},
        {"video-grafo.json"},
        {"video-grafos.json"}, // el de antes de video-grafo.json (un grafo por entrada)
        {"video-reloj.json"},
        {"vinculos.json"},
        {"vocabulario.json"},
        {"voz-teleprompter.json"},
        {"votaciones-oficiales.json", 2, {listaEn("votaciones")}},
    };
    return tabla;
}

const EsquemaConfig& esquemaConfig(const std::string& nombre) {
    for (const auto& e : esquemasConfig())
        if (e.nombre == nombre) return e;
    static const EsquemaConfig sinDeclarar{};
    return sinDeclarar;
}

namespace {
/// Los archivos que, al leerlos, resultaron de una versión más nueva: no se pisan.
/// Se anota al leer (todos los dueños leen antes de guardar), así guardar no toca el
/// disco desde el hilo del que guarda (el de la UI, casi siempre).
struct MasNuevos {
    std::mutex mu;
    std::set<std::string> archivos;
    std::set<std::string> avisados;
};
MasNuevos& masNuevos() {
    static MasNuevos m;
    return m;
}
} // namespace

json leerConfig(const std::filesystem::path& archivo, bool* habia) {
    // Lo último: si hay algo encolado que todavía no está en el disco, eso.
    std::string bytes;
    const bool esta = cola().leer(archivo, bytes);
    if (habia != nullptr) *habia = esta;
    if (!esta) return json::object();
    json j = json::parse(bytes, nullptr, false);
    if (j.is_discarded()) return json::object();
    const std::string nombre = archivo.filename().string();
    const EsquemaConfig& e = esquemaConfig(nombre);
    if (migrar(j, e.actual, e.pasos, nombre) > e.actual) {
        MasNuevos& m = masNuevos();
        std::lock_guard<std::mutex> l(m.mu);
        m.archivos.insert(archivo.lexically_normal().string());
    }
    return j.is_object() ? j : json::object();
}

namespace {
/// Lo común de guardar: false si el archivo es de una versión más nueva (no se pisa);
/// si no, lo deja marcado con el esquema actual y en `bytes`.
bool preparar(const std::filesystem::path& archivo, json& j, std::string& bytes) {
    const std::string nombre = archivo.filename().string();
    const EsquemaConfig& e = esquemaConfig(nombre);
    {
        MasNuevos& m = masNuevos();
        std::lock_guard<std::mutex> l(m.mu);
        const std::string clave = archivo.lexically_normal().string();
        if (m.archivos.count(clave) != 0 || esquemaDe(j) > e.actual) {
            // Una vez por archivo: hay dueños que guardan muy seguido (un fader).
            if (m.avisados.insert(clave).second)
                ba::diag::registro().linea(
                    ba::diag::Nivel::Aviso, "config", 0,
                    ba::i18n::llenar(ba::i18n::traducir("config", "%1: lo escribió una versión más nueva (esta escribe "
                                                                  "el esquema %2): no se pisa, el cambio queda sin guardar"),
                                     {nombre, std::to_string(e.actual)}));
            return false;
        }
    }
    if (j.is_object()) j[kClaveEsquema] = e.actual;
    bytes = j.dump(1, ' ', false, json::error_handler_t::replace);
    return true;
}
} // namespace

bool guardarConfig(const std::filesystem::path& archivo, json j, Pedido p) {
    std::string bytes;
    if (!preparar(archivo, j, bytes)) return false;
    cola().escribir(archivo, std::move(bytes), std::move(p));
    return true;
}

bool guardarConfigYa(const std::filesystem::path& archivo, json j) {
    std::string bytes;
    return preparar(archivo, j, bytes) && escribir(archivo, bytes);
}

} // namespace ba::persist
