// ============================================================================
// src/gateway/Carruseles.hpp — lo que rota en cada pantalla
//
// Un CARRUSEL es una lista de páginas con cuánto dura cada una: el QR de la
// sala 20 segundos, un mensaje 10, la hora 15, y vuelta a empezar. La arma A
// (que es quien sabe qué hay: los códigos guardados, el teleprompter, el
// programa) y la muestra un televisor, que solo necesita la lista.
//
// POR QUE UNA LISTA DE URLS Y NO DE CONTENIDOS. Todo lo que este aparato sabe
// proyectar ya es una página que sirve el gateway (ver /pantalla/). Si el
// carrusel guarda URLs, cualquier cosa que se pueda mostrar se puede poner en
// uno SIN tocar esto: un mensaje, un QR, el teleprompter, el programa en vivo,
// o una página de afuera. El carrusel no sabe de QR ni de nada: sabe rotar.
//
// POR QUE ACA Y NO EN LA URL. Una lista de diez pasos en la barra de
// direcciones sería enorme, y cambiar un paso obligaría a mandarle al
// televisor una URL nueva —o sea, a reiniciar la página y que parpadee—. Con
// el carrusel guardado acá, el televisor apunta a `/pantalla/carrusel/?id=X`
// una sola vez y lo que muestre se cambia sin tocarlo.
//
// La `version` sube con cada cambio: la página la mira y solo recarga su lista
// cuando de verdad cambió algo.
// ============================================================================
#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Carruseles {
   public:
    /// Tope de carruseles guardados. Más que esto es un error de quien
    /// publica, no un caso real: una sala tiene un puñado de pantallas.
    static constexpr std::size_t kMax = 32;

    /// A publica uno entero: {id, nombre, pasos:[{url, segundos, titulo}]}.
    /// Devuelve false si viene mal armado.
    bool publicar(const nlohmann::json& carrusel);
    /// Lo saca. true si estaba.
    bool borrar(const std::string& id);
    /// Uno, para la página que lo muestra. `null` si no existe.
    nlohmann::json uno(const std::string& id) const;
    /// Todos, para que el operador vea qué hay publicado.
    nlohmann::json todos() const;

   private:
    mutable std::mutex m_;
    /// id -> carrusel (con su `version`).
    nlohmann::json por_ = nlohmann::json::object();
    std::uint64_t version_ = 0;
};

} // namespace ba::gateway
