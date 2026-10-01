// ============================================================================
// src/gateway/Preguntas.hpp — las preguntas del público
//
// Hay chat de sala y hay chat de YouTube, Twitch y Facebook. CHAT NO ES COLA DE
// PREGUNTAS: no se modera, no se elige, no se ordena, no hay «la que sigue» y
// no sale al aire con el nombre de quien preguntó. En un evento con público, lo
// que hace falta es esto.
//
// UNA SOLA COLA. La pregunta del que está en la butaca y la del que mira por
// YouTube entran a la misma lista y se moderan en el mismo lugar. Dos colas
// convierten al público remoto en público de segunda, que es exactamente lo que
// hace que un evento híbrido se sienta como dos eventos mal pegados.
//
// VIVE EN B Y NO EN A, porque llega de teléfonos y B es lo que los teléfonos
// alcanzan. A (la app) las viene a buscar, las modera y las saca al aire.
//
// EL RECORRIDO de una pregunta:
//
//     nueva ──► elegida ──► alAire ──► respondida
//       └────────────────────────────► descartada
//
// `elegida` es la cola de verdad: lo que el conductor va a preguntar, en orden.
// Separarla de `nueva` es lo que permite moderar antes y no leer en vivo lo
// primero que llegó.
//
// LOS VOTOS son para ordenar, no para decidir. Se cuentan por navegador, que no
// es identidad: alcanza para que no sume diez veces el que toca diez veces, no
// para frenar a alguien decidido. Se dice acá para que nadie construya encima
// creyendo que es una urna — la urna, cuando exista, es otra cosa.
//
// EL TEXTO ES DE AFUERA. Lo escribe cualquiera con el enlace: se recorta, se
// limita en cantidad, y quien lo muestre NO lo trata como HTML.
// ============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Preguntas {
   public:
    /// Cuántas se guardan. Pasado el tope entran igual, y se van las más
    /// viejas QUE YA NO IMPORTAN (respondidas y descartadas) antes que ninguna
    /// que todavía esté esperando: perder una pregunta sin contestar porque
    /// alguien llenó la lista sería el peor de los dos males.
    static constexpr std::size_t kMax = 400;
    /// Largo máximo del texto. Una pregunta que no entra en un renglón y medio
    /// no se puede leer al aire.
    static constexpr std::size_t kMaxTexto = 400;
    static constexpr std::size_t kMaxNombre = 60;

    enum class Estado { Nueva, Elegida, AlAire, Respondida, Descartada };
    static const char* nombreDeEstado(Estado e);
    static bool estadoDeNombre(const std::string& s, Estado& out);

    struct Pregunta {
        std::string id;
        std::string nombre; ///< como se presentó; puede venir vacío
        std::string texto;
        std::string deDonde; ///< "publico", "sala", "youtube", "twitch", "facebook"
        Estado estado = Estado::Nueva;
        std::int64_t cuandoMs = 0;
        int votos = 0;
        /// Orden dentro de la cola de elegidas (0 = no está en la cola).
        int turno = 0;
    };

    /// Alguien pregunta. Devuelve el id, o vacío si el texto no dice nada.
    /// `quien` es el navegador que la manda: sirve para el tope por persona.
    std::string preguntar(const std::string& nombre, const std::string& texto, const std::string& deDonde,
                          const std::string& quien, std::int64_t ahoraMs);

    /// Suma un voto. Un mismo `quien` no suma dos veces la misma pregunta.
    /// :returns: false si no existe, o si ese navegador ya la había votado.
    bool votar(const std::string& id, const std::string& quien);

    /// La manda a la cola de las que se van a preguntar, al final.
    bool elegir(const std::string& id);
    /// La saca de la cola y la deja como estaba antes de elegirla.
    bool desElegir(const std::string& id);
    /// La pone al aire (y saca del aire a la que estuviera).
    bool alAire(const std::string& id);
    bool responder(const std::string& id);
    bool descartar(const std::string& id);
    /// La primera de la cola que todavía no se preguntó, o vacío.
    std::string proxima() const;

    /// Todas, ordenadas: primero la que está al aire, después la cola por
    /// turno, después las nuevas por votos y por hora.
    nlohmann::json lista() const;
    /// Lo que ve un teléfono del público: las suyas y el recuento. NO ve las
    /// descartadas de otros —enterarse de que a alguien se la bocharon no le
    /// sirve a nadie— ni el estado de moderación de las ajenas.
    nlohmann::json listaPublica(const std::string& quien) const;
    /// Cuántas hay en cada estado.
    nlohmann::json recuento() const;

    /// Borra las respondidas y descartadas. Devuelve cuántas se fueron.
    std::size_t limpiar();
    /// Borra TODAS. Es para empezar un evento nuevo.
    void vaciar();

   private:
    mutable std::mutex m_;
    std::vector<Pregunta> ps_;
    /// Quién votó qué, para no sumar dos veces.
    std::vector<std::pair<std::string, std::string>> votos_;
    int siguienteTurno_ = 0;
    std::int64_t n_ = 0;

    Pregunta* buscar(const std::string& id);
    void hacerLugar();
};

} // namespace ba::gateway
