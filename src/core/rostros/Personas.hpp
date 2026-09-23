// ============================================================================
// src/core/rostros/Personas.hpp — la libreta de quién es quién
//
// Una ficha por persona: cómo se llama, qué cargo poner en el rótulo, notas, y
// los vectores de su cara (ver Rostros.hpp). Es lo que convierte «acá hay una
// cara» en «llegó Ana Pérez, que habla tercera».
//
// VARIOS VECTORES POR PERSONA, a propósito. Una sola foto reconoce a esa
// persona con esa luz, ese peinado y ese ángulo. Guardando unos cuantos —de
// frente, de costado, con anteojos y sin— el reconocimiento aguanta el día
// real. Se compara contra el MEJOR de sus vectores, no contra un promedio:
// promediar una cara de frente con una de perfil da un vector que no se parece
// a ninguna de las dos.
//
// NO GUARDA FOTOS. Solo números y datos que alguien escribió. Quien se lleve
// el archivo no se lleva las caras de nadie. Si se quiere una foto para la
// ficha, se guarda aparte y porque se pidió.
//
// ESTO NO DECIDE NADA SOLO. Devuelve a quién se parece y cuánto; poner un
// rótulo al aire con ese nombre es decisión de quien llama, y en vivo tiene
// que pasar por una persona. Un sistema que rotula solo se equivoca en
// público.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::rostros {

struct Persona {
    std::string id;       ///< estable; no cambia aunque se renombre
    std::string nombre;
    std::string cargo;    ///< lo que va en el rótulo ("Ministra de Salud")
    std::string nota;     ///< para el operador ("entra por la puerta lateral")
    /// Cuándo se la vio por última vez (ms desde la época, 0 = nunca).
    std::int64_t vistaMs = 0;
    /// Sus vectores de cara. Cada uno de 128 números.
    std::vector<std::vector<float>> caras;
};

/// A quién se parece una cara.
struct Coincidencia {
    std::string id;
    std::string nombre;
    float parecido = 0;   ///< 0..1; ver kMismaPersona en Rostros.hpp
};

/// La libreta. Se guarda en un JSON al lado de la configuración.
///
/// No lanza: `leer`/`guardar` devuelven false y dejan el motivo en `error()`.
class Personas {
  public:
    /// Lee el archivo. Si no existe, queda vacía y devuelve true: no tener
    /// libreta todavía no es un error.
    bool leer(const std::string& archivo);
    bool guardar(const std::string& archivo) const;
    const std::string& error() const { return error_; }

    const std::vector<Persona>& todas() const { return personas_; }
    const Persona* porId(const std::string& id) const;

    /// Crea o actualiza. Con `id` vacío crea una y devuelve el id nuevo.
    std::string guardarFicha(const Persona& p);
    bool borrar(const std::string& id);

    /// Le suma un vector de cara a alguien. Devuelve false si no existe.
    ///
    /// Descarta los CASI IGUALES a uno que ya tiene: guardar treinta capturas
    /// del mismo segundo no mejora el reconocimiento y hace más lenta cada
    /// búsqueda. Lo que sirve es variedad, no cantidad.
    bool sumarCara(const std::string& id, const std::vector<float>& vector);

    /// A quién se parece más, de todas. Devuelve `parecido` 0 si no hay nadie.
    ///
    /// Compara contra el mejor vector de cada persona. No aplica el umbral:
    /// eso lo decide quien llama, que es el que sabe si está poniendo un
    /// rótulo al aire o llenando una lista para que alguien la mire.
    Coincidencia buscar(const std::vector<float>& vector) const;

    /// Marca que se la acaba de ver.
    void vistaAhora(const std::string& id, std::int64_t ahoraMs);

    /// Cuántos vectores guarda una persona como máximo.
    ///
    /// Con más, cada búsqueda cuesta más y no reconoce mejor: los de más son
    /// casi siempre repeticiones del mismo momento.
    static constexpr std::size_t kMaxCarasPorPersona = 12;

  private:
    std::vector<Persona> personas_;
    mutable std::string error_;
};

} // namespace ba::rostros
