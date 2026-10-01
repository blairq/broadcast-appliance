// ============================================================================
// src/gateway/Rotulador.hpp — los rotulos, desde el celular
//
// La pantalla de Rotulos de la app (escribir el nombre del que habla y tirarlo
// al aire) tambien tiene que poder usarse desde un telefono: el que rotula
// muchas veces no es el que opera, y no siempre esta sentado en la cabina.
//
// B no sabe nada de graficos: los dibuja A. Asi que esto es un BUZON de dos
// direcciones y nada mas:
//
//   A -> B   el estado: que fuentes hay en la escena al aire, con que campos
//            y que valores. A lo empuja cuando cambia.
//   B -> A   los encargos: "en la fuente 7 poné estos valores y sacalo al
//            aire". Se encolan hasta que A los viene a buscar.
//
// Por que encolar y no ejecutar: el que dibuja es A, y A puede estar ocupado o
// recien arrancando. Un encargo encolado se ejecuta tarde; una llamada directa
// a un proceso que no esta se pierde.
//
// LA CLAVE. A diferencia del teleprompter —donde el texto es publico y lo que
// se protege es cambiarlo— aca la clave hace falta TAMBIEN PARA MIRAR: los
// rotulos suelen tener el nombre del invitado que todavia no se anuncio. La
// clave viaja en el fragmento del enlace (y del QR), que no llega al servidor
// ni queda en los registros.
// ============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Rotulador {
   public:
    /// Cuantos encargos se guardan si A no viene a buscarlos. Mas que esto es
    /// que A no esta: encolar cien no lo va a resucitar, y el ultimo es
    /// siempre el que importa.
    static constexpr std::size_t kMaxEncargos = 16;

    /// A publica lo que hay. Reemplaza lo anterior entero.
    void publicar(const nlohmann::json& estado);

    /// Lo que ve el celular: las fuentes con sus campos y valores, y cuando
    /// fue la ultima vez que A hablo (para poder avisar «la app no responde»).
    nlohmann::json estado() const;

    /// El celular encarga algo. `orden` lleva `id` (la fuente), `grafico`
    /// opcional, `valores` opcional y `accion` (aire | bucle | sacar).
    /// :returns: false si la orden no dice sobre que fuente actuar.
    bool encargar(const nlohmann::json& orden);

    /// A se lleva los encargos pendientes y la cola queda vacia.
    nlohmann::json tomarEncargos();

    /// Cuantos hay esperando (para la vista, que muestra «mandado»).
    std::size_t pendientes() const;

   private:
    mutable std::mutex m_;
    nlohmann::json estado_ = nlohmann::json::object();
    std::int64_t publicadoMs_ = 0;
    std::vector<nlohmann::json> encargos_;
};

} // namespace ba::gateway
