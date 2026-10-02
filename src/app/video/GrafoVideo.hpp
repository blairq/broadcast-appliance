// src/app/video/GrafoVideo.hpp — el grafo de tarjetas de video de una entrada (lógica pura)
//
// Como el rack de audio (audio/AudioRack), pero de video y sin hilo de
// tiempo real: lo ejecuta en la GPU el filtro `ba_video_grafo`
// (src/obs-module/ba_video_grafo.cpp), que recibe `paraFiltro()`.
//
// Tarjetas: `in` (la entrada, fija), `out` (lo que ve la escena, fija),
// `color`, `croma`, `recorte` (con espejo) y `salida` (publica ese punto como
// una fuente con nombre). Cada tarjeta tiene UNA entrada y una salida; de una
// salida pueden salir varios cables (el grafo se ramifica). Una entrada recibe
// un solo cable: conectar otro reemplaza al anterior. Sin ciclos.
//
// Se guarda (json()) y se describe para la interfaz y MCP (describir()).

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app::video {

class GrafoVideo {
   public:
    struct Nodo {
        std::string id, tipo, etiqueta;
        int x = 0, y = 0;
        nlohmann::json params = nlohmann::json::object();
    };
    struct Cable {
        std::string desde, hacia;
        bool operator==(const Cable& o) const { return desde == o.desde && hacia == o.hacia; }
    };

    /// in -> out.
    GrafoVideo();

    /// Los tipos de tarjeta que se pueden agregar, con su título y sus
    /// parámetros de fábrica: [{tipo, titulo, descripcion, params}].
    static nlohmann::json catalogo();
    static bool tipoValido(const std::string& tipo);

    /// Agrega una tarjeta. `despues`: el nodo del que cuelga (vacío = el que
    /// hoy llega a `out`). Una `salida` se agrega como RAMA (no corta la
    /// cadena); las demás se intercalan (lo que salía de `despues` sale ahora
    /// de la nueva). Devuelve el id nuevo, o "" con `error`.
    std::string agregar(const std::string& tipo, const std::string& despues, const nlohmann::json& params,
                        std::string* error);
    /// Quita una tarjeta (no `in` ni `out`): lo de antes se une con lo de después.
    bool quitar(const std::string& id, std::string* error);
    /// Un cable de `desde` a `hacia` (reemplaza el que llegaba a `hacia`).
    bool conectar(const std::string& desde, const std::string& hacia, std::string* error);
    bool desconectar(const std::string& desde, const std::string& hacia);
    /// Cambia (mezcla) parámetros y/o la etiqueta.
    bool ajustar(const std::string& id, const nlohmann::json& params, std::string* error);
    bool mover(const std::string& id, int x, int y);

    const Nodo* nodo(const std::string& id) const;
    const std::vector<Nodo>& nodos() const { return nodos_; }
    const std::vector<Cable>& cables() const { return cables_; }
    /// Solo in -> out: no hace falta el filtro.
    bool trivial() const;
    /// Los nombres de las salidas.
    std::vector<std::string> salidas() const;

    nlohmann::json aJson() const;
    /// Lee lo guardado; lo que no cierra (tipos raros, cables a nodos que no
    /// están, ciclos) se descarta. Siempre quedan `in` y `out`.
    static GrafoVideo desde(const nlohmann::json& j);
    /// Lo que recibe el filtro: {nodos:[{id,tipo,params}], cables:[{desde,hacia}]}.
    nlohmann::json paraFiltro() const;
    /// Para la interfaz y MCP: nodos con su título, entradas/salidas (0/1) y
    /// los cables.
    nlohmann::json describir() const;

   private:
    std::string idNuevo(const std::string& tipo) const;
    bool llega(const std::string& desde, const std::string& hasta) const; ///< hay camino desde -> hasta
    std::vector<Nodo> nodos_;
    std::vector<Cable> cables_;
};

} // namespace ba::app::video
