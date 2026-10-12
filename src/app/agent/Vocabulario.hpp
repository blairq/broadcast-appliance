// SPDX-License-Identifier: GPL-2.0-or-later
//
// Vocabulario: lo que el reconocedor de voz tiene que escribir BIEN aunque no
// lo conozca: nombres y apellidos del cuerpo, localidades, términos
// de asamblea, lo que agregue el operador.
//
// Los modelos de voz a texto que usamos (transducers con greedy search,
// Parakeet) no aceptan "hotwords", así que se corrige DESPUÉS: cada palabra (o
// grupo de hasta 3) se compara con los términos por cómo SUENA en castellano
// (b/v, c/s/z, ll/y, h muda, qu/k, g/j ante e/i…) y, si la distancia es chica,
// se reemplaza por el término bien escrito. Solo con confianza alta; cada
// reemplazo queda registrado.
//
// Config/vocabulario.json: {grupos: {<nombre>: {activo, terminos: [{termino, variantes[]}]}}}.
// Se usa desde cualquier hilo (el del reconocedor): todo bajo un mutex.
#pragma once

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace ba::app::asamblea {
class Cuerpo;
}

namespace ba::app::agent {

class Vocabulario {
   public:
    using json = nlohmann::json;
    /// El de la app (lo usa el reconocedor). nullptr hasta que se crea.
    static Vocabulario* global();
    explicit Vocabulario(std::filesystem::path config);
    ~Vocabulario();

    /// El texto corregido (y los reemplazos hechos, si se pide).
    std::string corregir(const std::string& texto, json* reemplazos = nullptr);

    json todo() const;
    /// Agrega (o cambia las variantes de) un término en un grupo ("operador" si no se dice).
    std::string agregar(const std::string& termino, const std::vector<std::string>& variantes, const std::string& grupo);
    bool quitar(const std::string& termino);
    void activarGrupo(const std::string& grupo, bool activo);
    /// Reemplaza el grupo «cuerpo» con estos nombres (nombres y apellidos).
    void ponerGrupo(const std::string& grupo, const std::vector<std::string>& terminos);
    /// Pone en el grupo «cuerpo» los nombres de los integrantes y, aparte, el
    /// apellido solo (se dice «el consejero Pérez»). Devuelve cuántos términos quedaron.
    int cargarCuerpo(const asamblea::Cuerpo& cuerpo);
    /// Los últimos reemplazos hechos al aire ({original, reemplazo, ms}).
    json ultimosReemplazos() const;

    /// La clave fonética en castellano (pública para las pruebas).
    static std::string fonetica(const std::string& s);

   private:
    void sembrar();
    void guardar() const;
    void reindexar();
    std::filesystem::path archivo_;
    mutable std::mutex mu_;
    json datos_;
    struct Entrada {
        std::string termino, clave;
        int palabras = 1;
    };
    std::vector<Entrada> indice_;
    std::deque<json> reemplazos_;
};

} // namespace ba::app::agent
