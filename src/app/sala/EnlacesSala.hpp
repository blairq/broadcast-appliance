// ============================================================================
// src/app/sala/EnlacesSala.hpp — los enlaces (y su QR) de las páginas de la sala (modelo)
//
// Cada página que sirve la sala tiene su enlace, de la red local o de afuera
// (el túnel), y la sala lo devuelve con la matriz de su QR: el ingreso de los
// invitados (con o sin PIN), el tablero de oradores, el puesto del corrector de
// subtítulos, la página del público, el teleprompter, los rótulos y lo que
// se le manda a un televisor (el programa o una escena). La GUI (QrDialog, la
// página Sala, Público, Teleprompter, los códigos QR, las pantallas, las
// invitaciones del evento) y el MCP (room_link, subtitulos_puesto,
// publico_pagina) los piden por acá.
//
// Sin enlace, `mensaje` dice por qué (sin red, solo esta máquina…): lo dice la
// sala y, si no contesta, «La sala no responde».
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

struct Enlace {
    bool ok = false;
    std::string url;
    std::vector<std::string> filas; ///< el QR: filas de '0'/'1'
    std::string mensaje;            ///< por qué no hay enlace, o un aviso
    std::string clave;              ///< la del teleprompter, para dictarla
    nlohmann::json respuesta;       ///< lo que contestó la sala, entero
};

class EnlacesSala {
   public:
    using Listo = std::function<void(const Enlace&)>;

    explicit EnlacesSala(ConferenceClient& sala);

    /// El de los invitados; con `conPin` el PIN va adentro.
    void ingreso(bool remoto, bool conPin, Listo listo);
    void tablero(bool remoto, Listo listo);
    /// El del corrector de subtítulos (siempre con su clave: sin ella no corrige).
    void correccion(bool remoto, Listo listo);
    /// El de la página del público, con la clave adentro (se proyecta o se imprime).
    void publico(bool remoto, Listo listo);
    /// `vista`: "control" u "orador".
    /// `id`: cuál teleprompter ("" = el principal).
    void prompter(bool remoto, const std::string& vista, bool conClave, Listo listo, const std::string& id = {});
    /// El de los rótulos para el celular; con `conClave` la clave va adentro.
    void rotulos(bool remoto, bool conClave, Listo listo);
    /// Lo que sale al aire (o, con `canal` >= 0, la escena de ese canal) para
    /// un televisor de la red local, que entra a la sala como `nombre`. Lleva
    /// un permiso de un solo uso: se pide cada vez que se lo manda.
    void pantalla(const std::string& nombre, int canal, Listo listo);

   private:
    void pedirEnlace(const std::string& ruta, Listo listo);

    ConferenceClient& sala_;
};

} // namespace ba::app::sala
