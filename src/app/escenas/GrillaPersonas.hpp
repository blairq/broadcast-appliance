// ============================================================================
// src/app/escenas/GrillaPersonas.hpp — la «Grilla de personas»: quién va dónde
//
// Una fuente de escena (tipo ba_grilla) que muestra a la gente conectada —los
// admitidos de la Sala y los integrantes habilitados de la Asamblea— en un
// mosaico que crece y se acomoda solo. Esta es la LÓGICA, en C++ puro (sin Qt ni
// video, la prueban test_grilla_personas): con la gente y las grillas decide qué
// persona va en qué grilla y en qué rectángulo. El mosaico lo pinta el gateway
// (src/gateway/Grillas.hpp, canal BA.Grilla.<k>) y por qué allá está explicado
// en ese archivo; lo conecta escenas/ControlGrillas.
//
// Por instancia (los ajustes de la fuente, ver Config):
//   - origen: sala, asamblea o las dos;
//   - grupo de reparto (opcional): las grillas con el mismo grupo se reparten la
//     gente SIN REPETIR. «En orden» llena la primera hasta su tope y sigue con la
//     siguiente; «parejo» pone la misma cantidad en cada una. El orden de las
//     grillas en el grupo es «orden_en_grupo» (y el nombre, a igualdad); el orden
//     de la gente y el modo los manda la PRIMERA del grupo;
//   - tope de celdas: si no entran todos, la última celda (de la última grilla
//     del grupo) es un cuadro «+N» con los que quedaron afuera;
//   - orden: llegada, quién habla (el que habló hace menos, primero) o prioridad
//     (el campo «prioridad» del integrante en Asamblea; en la Sala, el
//     presentador fijo va primero). El orden decide QUIÉNES entran cuando no
//     entran todos y dónde va el que llega; NO reordena a los que ya están;
//   - filtros: solo los que tienen la cámara prendida; sin los que ya están al
//     aire en otra fuente (el invitado que ocupa un lugar del programa, el
//     integrante con la palabra cuando su canal o la banca están al aire);
//   - resaltar a quien habla (un borde).
//
// LUGARES ESTABLES: cada grilla recuerda en qué lugar está cada uno. El que llega
// va al final (o al hueco de alguien que se fue en la misma vuelta); el que se va
// deja su hueco al ÚLTIMO de esa grilla (se mueve uno solo, no todos). Solo
// cuando cambia la cantidad de columnas se reacomoda el dibujo, y eso el gateway
// lo anima (cada cara viaja a su lugar nuevo).
//
// La misma fuente en dos escenas es LA MISMA IMAGEN: para repartir gente entre
// escenas se usan instancias distintas con el mismo grupo.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ba::app::grilla {

using json = nlohmann::json;

/// El tipo de fuente.
constexpr const char* kTipo = "ba_grilla";
/// Grillas a la vez (canales BA.Grilla.1..8; el mismo techo que el gateway).
constexpr int kMaxGrillas = 8;
/// Celdas por grilla como mucho (el techo del gateway).
constexpr int kMaxCeldas = 64;

enum class Origen { Sala, Asamblea, Ambas };
enum class Orden { Llegada, Habla, Prioridad };
enum class Reparto { EnOrden, Parejo };

struct Persona {
    std::string clave; ///< "sala:<id del participante>" | "asamblea:<id del integrante>"
    std::string nombre;
    bool deSala = true;
    std::int64_t llegadaMs = 0; ///< cuándo entró (admitido / habilitado)
    int prioridad = 0;          ///< mayor = antes
    bool conCamara = true;
    bool hablando = false;
    std::int64_t vozMs = 0; ///< la última vez que habló (0 = todavía no)
    bool alAire = false;    ///< ya se ve en otra fuente del programa
};

struct Config {
    std::string fuente; ///< el nombre de la fuente (la identifica)
    int canal = 0;      ///< BA.Grilla.<canal> (1..kMaxGrillas; 0 = sin asignar todavía)
    Origen origen = Origen::Ambas;
    std::string grupo; ///< "" = sola
    int ordenEnGrupo = 1;
    Reparto reparto = Reparto::EnOrden;
    int tope = 16; ///< celdas (con la del «+N»); 0 = hasta kMaxCeldas
    Orden orden = Orden::Llegada;
    bool soloConCamara = false;
    bool sinAlAire = true;
    bool resaltar = true;
    int ancho = 1280, alto = 720;
    int separacion = 8;
    int margen = 0;
    double aspecto = 16.0 / 9.0; ///< de cada celda; 0 = llenar el lugar (el gateway recorta)
    int transicionMs = 400;
    std::string fondo = "#00000000";         ///< "#AARRGGBB" (transparente de fábrica)
    std::string colorResaltar = "#FFFFC107"; ///< el borde de quien habla

    /// Desde los ajustes guardados de la fuente (los de fábrica, ver defectos()).
    static Config desdeAjustes(const std::string& fuente, const json& ajustes);
    /// Los ajustes de fábrica del tipo (los mismos nombres que lee desdeAjustes).
    static json defectos();
    /// Un ajuste que no se entiende: "" o por qué (para el MCP).
    static std::string validar(const json& cambios);
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

struct Celda {
    std::string clave;
    Rect r;
    bool resaltar = false;
};

/// Lo que muestra una grilla en esta vuelta.
struct Resultado {
    std::string fuente;
    int canal = 0;
    std::vector<std::string> personas; ///< en orden de lugar
    int mas = 0;                       ///< los que quedaron afuera (el cuadro «+N»)
    std::vector<Celda> celdas;
    Rect rMas;
};

/// ¿Esta grilla acepta a esta persona? (origen y filtros)
bool acepta(const Config& c, const Persona& p);
/// ¿`a` va antes que `b` con este orden?
bool antes(Orden o, const Persona& a, const Persona& b);

/// Los `n` rectángulos de una grilla (en orden de lectura; la última fila, centrada):
/// la cantidad de columnas que deja las celdas más grandes. `cols` (opcional) la dice.
std::vector<Rect> disponer(int n, int ancho, int alto, int separacion, int margen, double aspecto, int* cols = nullptr);

/// El reparto con memoria (los lugares estables): una instancia vive lo que vive el control.
class Repartidor {
   public:
    /// Una vuelta: todas las grillas y toda la gente → lo que muestra cada una (en el orden de `grillas`).
    std::vector<Resultado> calcular(const std::vector<Config>& grillas, const std::vector<Persona>& gente);
    /// Olvidar los lugares (la próxima vuelta reparte desde cero).
    void olvidar() { lugares_.clear(); }

   private:
    std::map<std::string, std::vector<std::string>> lugares_; // fuente → claves por lugar
};

/// El estado entero para el gateway (POST /api/v1/grillas), ver src/gateway/Grillas.hpp.
json pedidoGateway(const std::vector<Config>& grillas, const std::vector<Resultado>& res);

/// Color de OBS (entero 0xAABBGGRR, como guarda la ventana Propiedades) → "#AARRGGBB".
std::string colorDeObs(std::uint32_t abgr);

} // namespace ba::app::grilla
