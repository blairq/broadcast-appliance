// SPDX-License-Identifier: GPL-2.0-or-later
//
// Cuerpo: los integrantes de un cuerpo deliberativo (o cualquier grupo con
// bancas): la FUENTE ÚNICA de nombre, cargo, bloque y banca para los zócalos,
// la fila de oradores (bancas presenciales), el PTZ de cada banca, el micrófono
// de cada banca y el sistema de conferencia del recinto.
//
// Config/cuerpo.json: {nombre, integrantes: [{id, persona_id?, nombre,
// cargo_oficial, bloque, distrito, banca, microfono: {fuente}, foto,
// dcn_unidad (la unidad de la central Bosch DCN de esa banca), validado_por,
// validado_en}], recinto: {filas, columnas, bancas: [{banca, fila, columna}]},
// tomas: {"<banca>": {camara, posicion}}}.
//
// LA TOMA ES DE LA BANCA, no de la persona: la cámara y la posición (de
// ControlCamaras) que la enfocan. Si alguien cambia de banca, la toma se queda
// en la banca; una banca vacía también tiene la suya. Para quien ya lo leía, un
// integrante sigue trayendo `preset_ptz` ({camara, posicion}): es la toma de su
// banca, calculada. Guardar un `preset_ptz` en un integrante (la planilla, el
// panel, el MCP) cambia la toma de su banca. Los cuerpo.json de antes (con el
// preset en la persona) se migran solos al abrir.
//
// EL RECINTO es la grilla de bancas como se ven desde la cabina: tocar una
// banca la lleva al preset de su cámara (la reusan la página PTZ, la fila de
// oradores y la botonera). Fila 0 es la de adelante (la más cerca de la
// presidencia), columna 0 la de la izquierda vista desde la cabina.
//
// Lo que sale al aire de acá está VALIDADO por una persona (validado_por):
// un integrante sin validar se marca y los zócalos lo avisan.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app::asamblea {

class Cuerpo {
   public:
    using json = nlohmann::json;
    explicit Cuerpo(std::filesystem::path config);
    /// Minúsculas, sin tildes ni espacios a los costados: como se comparan bancas y nombres.
    static std::string normalizar(const std::string& s);

    json todo() const { return datos_; }
    /// Cada uno con `preset_ptz`: la toma de su banca.
    json integrantes() const;
    std::string nombre() const { return datos_.value("nombre", std::string()); }
    /// Crea (o renombra) el cuerpo. No borra integrantes.
    void setNombre(const std::string& n);

    /// Busca por id, banca o nombre (sin distinguir mayúsculas). {} si no está.
    json buscar(const std::string& idBancaONombre) const;
    /// Crea o cambia un integrante (con `id` existente lo cambia; sin `id`, lo
    /// crea). Solo los campos conocidos. Devuelve el id, o "" con el error en `error`.
    std::string guardar(const json& campos, std::string* error = nullptr);
    bool borrar(const std::string& id);
    /// Marca a un integrante como validado por `quien` (ahora).
    bool validar(const std::string& id, const std::string& quien);

    /// La toma de una banca: {camara, posicion}, o {} si no tiene.
    json toma(const std::string& banca) const;
    /// Todas: {"<banca>": {camara, posicion}}.
    json tomas() const { return datos_.value("tomas", json::object()); }
    /// Cambia la toma de una banca (`camara` vacía la saca). "" o el error.
    std::string setToma(const std::string& banca, const std::string& camara, const std::string& posicion);

    /// El recinto. Con `completo`, cada lugar trae su integrante ({id, nombre,
    /// bloque, preset_ptz, validado}) y `sin_lugar` las bancas del cuerpo que
    /// todavía no están en la grilla. `presidencia` ({banca, toma?, integrante?},
    /// solo si hay) es la banca de la presidencia: va en la mesa, fuera de la grilla.
    /// Una banca con `x`, `y` se ubicó a mano en el hemiciclo: en anchos de banca
    /// desde el centro de la presidencia (y negativa hacia arriba), las mismas
    /// unidades que los arcos (radio de adentro max(1,4·columnas/π, 1,4), 1,32 por fila).
    json recinto(bool completo = true) const;
    /// Cambia el recinto: {filas?, columnas?, bancas?: [{banca, fila, columna, x?, y?}]
    /// (reemplaza la lista), mover?: {banca, fila, columna} (una sola; fila
    /// -1 la saca; vuelve a su arco), auto?: true (acomoda todas las bancas en orden,
    /// de adelante para atrás), presidencia?: banca ("" la saca), posicion?: {banca,
    /// x, y} (en el hemiciclo; x null la vuelve a su arco), posiciones_auto?: true
    /// (todas a su arco)}. "" o el error (y no cambia nada).
    std::string setRecinto(const json& cambios);

    /// Importa una planilla CSV (separada por coma o punto y coma, con
    /// encabezado: banca, nombre, cargo, bloque, distrito, camara, posicion,
    /// microfono; los nombres de columna se reconocen sin tildes ni mayúsculas).
    /// `reemplazar`: borra los de antes. Devuelve {importados, actualizados, errores[]}.
    json importarCsv(const std::filesystem::path& archivo, bool reemplazar);
    /// Escribe una planilla de ejemplo con el encabezado y dos filas.
    static bool escribirPlantilla(const std::filesystem::path& archivo);

    /// Quién se entera de cada cambio (nombre, integrantes, recinto): una vez
    /// por operación, venga de la interfaz, del MCP o de la sesión remota. Así
    /// la vista no depende de que cada llamador se acuerde de avisar.
    void alCambiar(std::function<void()> fn) { alCambiar_ = std::move(fn); }

   private:
    void guardarArchivo() const;
    /// Cambia la toma sin guardar (setToma y guardar guardan, y avisan, una vez).
    std::string ponerToma(const std::string& banca, const std::string& camara, const std::string& posicion);
    /// El integrante con `preset_ptz` = la toma de su banca.
    json conToma(json integrante) const;
    /// La clave de `tomas` de esa banca (la escrita, comparada sin tildes ni mayúsculas).
    std::string claveToma(const std::string& banca) const;
    std::filesystem::path archivo_;
    json datos_;
    std::function<void()> alCambiar_;
    bool importando_ = false; ///< importarCsv avisa una vez al final, no por fila
};

} // namespace ba::app::asamblea
