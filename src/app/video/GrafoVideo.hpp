// src/app/video/GrafoVideo.hpp — el grafo de video del estudio (lógica pura)
//
// UN SOLO GRAFO para todo el estudio, como un pipeline de GStreamer con sus
// bins: cada entrada de video es un SUB-GRAFO (su `grupo`, el nombre de la
// entrada) con su nodo `entrada` (la cámara, ya encuadrada por el PTZ) y su
// nodo `escena` (lo que ven las escenas que usan esa entrada). Los nodos
// sin grupo son del estudio (un compositor, más adelante controladores y el
// reloj). Los cables van de PUERTO a PUERTO y pueden cruzar entre
// sub-grafos.
//
// Nodos, por sus puertos:
//   fuente    0 -> 1   entrada
//   filtro    1 -> 1   color, croma, recorte, obs (cualquier filtro de OBS),
//                      gst (cualquier módulo de video de GStreamer)
//   varias    N -> 1   compositor: puertos de entrada A PEDIDO (in_0, in_1…;
//                      siempre queda uno libre, como los sink_%u de GStreamer)
//   destino   1 -> 0   escena, salida (publica una fuente con nombre)
//   control   0 -> ctl controlador: el molde de una tarjeta (tipo y params).
//                      Cada filtro tiene una entrada de control `ctl`; el que
//                      recibe un cable de un controlador usa los params del
//                      controlador, con lo suyo (`local`) encima. Así UNA
//                      corrección maneja las cámaras de tres marcas, y cada
//                      una puede retocar lo suyo.
// Los puertos tienen tipo (video o control) y un cable une puertos del mismo tipo.
// A cada puerto de entrada le llega UN cable (conectar otro reemplaza al
// anterior); de una salida pueden salir varios (el grafo se ramifica, el
// `tee`). Sin ciclos en todo el grafo.
//
// Lo ejecuta en la GPU el filtro `ba_video_grafo` de cada entrada
// (src/obs-module/ba_video_grafo.cpp), que recibe `paraFiltro(entrada)`: lo
// que se alcanza desde esa entrada, sin pasar por nodos de varias entradas
// (esos los ejecuta el estudio; ver docs/funciones/VIDEO-GRAFO.md).

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app::video {

/// Cuántas cámaras une como mucho una Panorámica (ba360: hasta 8 lentes).
constexpr int kMaxLentes360 = 8;

class GrafoVideo {
   public:
    struct Nodo {
        std::string id, tipo, etiqueta;
        std::string grupo; ///< la entrada a la que pertenece ("" = del estudio)
        int x = 0, y = 0;
        nlohmann::json params = nlohmann::json::object();
    };
    struct Cable {
        std::string desde, salida = "out", hacia, entrada = "in";
        bool operator==(const Cable& o) const {
            return desde == o.desde && salida == o.salida && hacia == o.hacia && entrada == o.entrada;
        }
    };
    struct Puerto {
        std::string id, tipo = "video";
        bool pedido = false; ///< libre, de los que se crean al conectar
    };

    GrafoVideo() = default;

    /// Las tarjetas que se pueden agregar (las propias; las de OBS las lista
    /// VideoGrafos): [{tipo, titulo, descripcion, params, ambito}]
    static nlohmann::json catalogo();
    static bool tipoValido(const std::string& tipo);
    /// El id del nodo `entrada` / `escena` de una entrada.
    /// Los parámetros de un tipo de tarjeta, documentados: [{clave, tipo,
    /// descripcion, min?, max?, opciones?}]. Los ve la tarjeta y el MCP.
    static nlohmann::json parametrosDe(const std::string& tipo);
    /// Lo que manda y escucha una tarjeta (eventos de GStreamer): {manda, escucha}.
    static nlohmann::json eventosDe(const std::string& tipo);
    /// Cómo se arma el texto del overlay de Rostros (la plantilla).
    static std::string ayudaTextoOverlay();
    static std::string idEntrada(const std::string& fuente);
    static std::string idEscena(const std::string& fuente);

    // --- Entradas (sub-grafos) -------------------------------------------------
    /// Se asegura de que la entrada tenga su sub-grafo (entrada -> escena).
    /// true si lo creó.
    bool asegurarEntrada(const std::string& fuente);
    /// Que la entrada tenga su tarjeta PTZ (justo después de la entrada si no
    /// tiene). true si la puso.
    bool asegurarPtz(const std::string& fuente);
    /// Lleva una tarjeta de la cadena a otro lugar de la misma entrada: después
    /// de `despues` (vacío = justo antes de la escena). La saca de donde está
    /// uniendo lo de antes con lo de después.
    bool reubicar(const std::string& id, const std::string& despues, std::string* error);
    std::vector<std::string> entradas() const;
    /// Saca el sub-grafo de una entrada (sus nodos y sus cables).
    void quitarEntrada(const std::string& fuente);

    // --- Editar ------------------------------------------------------------------
    /// Agrega una tarjeta. En una entrada (`grupo`), después de `despues`
    /// (vacío = lo que hoy llega a su escena): un filtro se intercala; una
    /// salida se cuelga como rama. Un compositor va al estudio (grupo ""),
    /// suelto. Devuelve el id nuevo, o "" con `error`.
    std::string agregar(const std::string& tipo, const std::string& grupo, const std::string& despues,
                        const nlohmann::json& params, std::string* error);
    /// Quita una tarjeta (no `entrada` ni `escena`): un filtro une lo de antes
    /// con lo de después.
    bool quitar(const std::string& id, std::string* error);
    /// Un cable de (desde, salida) a (hacia, entrada). `entrada` vacía = el
    /// primer puerto (en un compositor, uno nuevo). Reemplaza el cable que
    /// llegaba a ese puerto. Devuelve el puerto usado (o "" con `error`).
    std::string conectar(const std::string& desde, const std::string& salida, const std::string& hacia,
                         const std::string& entrada, std::string* error);
    bool desconectar(const std::string& desde, const std::string& salida, const std::string& hacia,
                     const std::string& entrada);
    /// Cambia (mezcla) parámetros y/o la etiqueta. `ajustes` (de una tarjeta
    /// de OBS) y `capas` (de un compositor) se mezclan clave por clave.
    bool ajustar(const std::string& id, const nlohmann::json& params, std::string* error);
    bool mover(const std::string& id, int x, int y);
    /// Le cambia el id a un nodo del estudio por uno legible a partir de
    /// `nombre` («Color cámaras» -> estudio.color-camaras), cables incluidos.
    /// Devuelve el id nuevo (o el mismo si no se pudo).
    std::string renombrar(const std::string& id, const std::string& nombre);
    /// Hace un controlador con los params de la tarjeta `id` y la conecta a él
    /// (si ya tenía uno, devuelve ese). El id del controlador, o "".
    std::string controlarDesde(const std::string& id, std::string* error);
    /// Suelta la tarjeta de su controlador: se queda con los params que usaba.
    bool soltar(const std::string& id, std::string* error);
    /// Agrega en `grupo` una tarjeta del tipo del controlador, manejada por él.
    std::string agregarControlada(const std::string& control, const std::string& grupo,
                                  const std::string& despues, std::string* error);
    /// Saca todas las tarjetas de una entrada (queda entrada -> escena).
    void limpiar(const std::string& grupo);

    // --- Leer ----------------------------------------------------------------------
    const Nodo* nodo(const std::string& id) const;
    const std::vector<Nodo>& nodos() const { return nodos_; }
    const std::vector<Cable>& cables() const { return cables_; }
    std::vector<Puerto> puertosEntrada(const Nodo& n) const;
    std::vector<Puerto> puertosSalida(const Nodo& n) const;
    /// El controlador que maneja la tarjeta `id` (o nullptr).
    const Nodo* controlador(const std::string& id) const;
    /// Los params con los que corre: los suyos, o los de su controlador con
    /// lo propio (`local`) encima.
    nlohmann::json efectivos(const Nodo& n) const;
    /// Solo entrada -> escena: no hace falta el filtro.
    bool trivial(const std::string& grupo) const;
    /// Los nombres de las salidas (todo el estudio).
    std::vector<std::string> salidas() const;

    nlohmann::json aJson() const;
    /// Lee lo guardado (versión 2), o migra el formato de antes (un grafo por
    /// entrada: {"<entrada>": {nodos, cables}}). Lo que no cierra se descarta.
    static GrafoVideo desde(const nlohmann::json& j);
    /// Lo que recibe el filtro de `fuente` (el formato de ba_video_grafo):
    /// {nodos:[{id,tipo,params}], cables:[{desde,hacia}]}, con su entrada como
    /// `in` y su escena como `out`.
    /// Lo que cruza entradas va en nodos que el filtro sabe correr: `toma`
    /// (publica el punto de un cable que sale de esta entrada, con el id del
    /// nodo) y `traer` (lo que llega a su escena desde afuera: un compositor,
    /// con su configuración, o la toma de otra entrada).
    nlohmann::json paraFiltro(const std::string& fuente) const;
    /// La configuración del compositor `id` para ba_video_compositor:
    /// {ancho, alto, fondo, capas:[{toma, fuente, x, y, ancho, alto,
    /// opacidad, ajuste}]} de abajo hacia arriba. Las capas sin posición van
    /// en mosaico.
    nlohmann::json configCompositor(const std::string& id) const;
    /// Los parámetros de una Vista 360 o una Panorámica, como propiedades de ba360
    /// (`lentes` > 0: la Panorámica, con esa cantidad de cámaras lado a lado).
    static nlohmann::json propiedades360(const nlohmann::json& params, int lentes);
    /// Cuántas cámaras tiene una Panorámica (hasta el último puerto conectado).
    int lentesConectadas(const std::string& id) const;
    /// Para la interfaz y MCP. Con `grupo`: ese sub-grafo y, marcados
    /// `externo`, los nodos de afuera que se conectan con él. Sin grupo: todo.
    nlohmann::json describir(const std::string& grupo, bool soloGrupo) const;

   private:
    std::string idNuevo(const std::string& grupo, const std::string& tipo) const;
    bool llega(const std::string& desde, const std::string& hasta) const;
    std::vector<Cable> entrantes(const std::string& id) const;
    std::vector<Cable> salientes(const std::string& id) const;
    std::vector<Nodo> nodos_;
    std::vector<Cable> cables_;
};

} // namespace ba::app::video
