// ============================================================================
// src/app/mcp/McpIndice.hpp — el buscador de herramientas, indexado
//
// QUE ESTABA MAL. Antes la búsqueda recorría las 241 herramientas por consulta
// y contaba si cada palabra aparecía en el título (5 puntos) o en el cuerpo
// (1). Tiene dos problemas y los dos se ven enseguida:
//
//   - TODAS LAS PALABRAS VALEN IGUAL. "poner música" traía `scene_switch`,
//     `guest_source_add` y `share_take`, porque "poner" está en media docena
//     de títulos. La palabra que llevaba la intención —"música"— no pesaba más
//     que el verbo genérico, y `media_use`, que es la respuesta, ni aparecía.
//   - SE BUSCA EN LO QUE ALGUIEN ESCRIBIO. `audio_set` no dice "silenciar" en
//     ningún lado, porque quien la escribió ya sabía cómo se llamaba. Ninguna
//     búsqueda por sinónimos la iba a encontrar.
//
// LO QUE HACE ESTE. Un índice invertido (término -> en qué herramientas está,
// y con qué peso) armado una sola vez al registrar, con dos cosas que el
// anterior no tenía:
//
//   1. IDF. Un término que está en muchas herramientas vale menos que uno que
//      está en pocas. Eso es lo que hace que "música" mande sobre "poner" sin
//      tener que listar verbos prohibidos a mano: la lista de palabras vacías
//      se arma sola de los datos, y sigue siendo correcta cuando se agregan
//      herramientas nuevas.
//   2. ETIQUETAS. Cada herramienta lleva los términos con los que una persona
//      la pediría, que casi nunca son los que usa su documentación. Pesan casi
//      como el nombre: son lo que se escribió PARA que se la encuentre.
//
// Y prefijos: "silenci" encuentra "silenciar" y "silencio". Quien busca no
// sabe cómo conjugamos.
//
// Sin dependencias fuera del núcleo: se prueba sin levantar la app.
// ============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace ba::app::mcp {

/// Un documento a indexar. Los campos están separados porque pesan distinto.
struct Documento {
    std::string id;    ///< el nombre de la herramienta o del tema
    std::string clase; ///< "herramienta" | "tema"
    std::string titulo;
    std::string nombre; ///< el identificador, que suele decir mucho
    std::vector<std::string> etiquetas;
    std::string cuerpo; ///< descripción + detalle
};

struct Resultado {
    std::size_t doc = 0;
    double puntaje = 0;
    /// Por qué salió: los términos de la consulta que engancharon. Va en la
    /// respuesta a propósito — una búsqueda que no dice por qué eligió lo que
    /// eligió no se puede corregir, ni por una persona ni por un modelo.
    std::vector<std::string> porque;
};

class Indice {
   public:
    /// Agrega un documento. Devuelve su posición (la que traen los Resultado).
    std::size_t agregar(Documento d);
    /// Arma el índice invertido. Hay que llamarlo antes de buscar; agregar
    /// después lo invalida y hay que volver a armarlo.
    void armar();

    /// Las mejores coincidencias, de mayor a menor.
    std::vector<Resultado> buscar(const std::string& consulta, std::size_t tope = 12) const;

    const Documento& doc(std::size_t i) const { return docs_.at(i); }
    std::size_t tamano() const { return docs_.size(); }

    /// Separa en términos: minúsculas, sin tildes, sin puntuación. Pública
    /// porque el que etiqueta necesita ver exactamente lo mismo que el índice.
    static std::vector<std::string> terminos(const std::string& s);

   private:
    struct Aparicion {
        std::size_t doc;
        double peso;
    };
    std::vector<Documento> docs_;
    std::map<std::string, std::vector<Aparicion>> invertido_;
    bool armado_ = false;
};

} // namespace ba::app::mcp
