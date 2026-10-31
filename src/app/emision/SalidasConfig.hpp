// ============================================================================
// src/app/emision/SalidasConfig.hpp — las salidas guardadas y su aire (modelo, sin GStreamer)
//
// docs/planes/PLAN-SALIDAS.md, etapa 2. Lo que se guarda de las salidas, en JSON
// puro (lo usan AppRuntime y BroadcastEngine; los tests lo prueban sin motor):
//
//   Config/salidas.json   {"tope": 4, "salidas": [{id, nombre, color, orden, codificador?,
//                         mezcla?: {<fuente>: {volumen, mute}}, grupos?: {<grupo>: ganancia}}]}
//                         La por omisión («principal») está siempre, aunque el
//                         archivo no exista o no la nombre: es la de siempre. Su
//                         mezcla, sus grupos (VCA) y su codificador son los de siempre
//                         (la escena y app.json); los de las demás van acá y vuelven al
//                         arrancar.
//   Config/destinos.json  cada destino con `salida` (sin campo = la por omisión).
//   Runtime/al-aire.json  EL DOCUMENTO DE AIRE (F3-04): lo único que dice qué está al
//                         aire. Lo de siempre (streaming, recording, escena, cerrado)
//                         es de la por omisión, y `salidas` trae cada una:
//                         {"salidas": {"<id>": {escena, streaming, recording, destinos,
//                         placa: {ftb, ftb_audio, placa, placa_texto, placa_imagen}}}}
//                         (`destinos`: los ids de destinos.json que transmitían) y
//                         `linea`: la parte viva de la línea de tiempo (el paso de la
//                         corrida, los HOLD; Corrida::aJsonVivo con `evento`). Un
//                         archivo viejo (sin `salidas`) se lee igual: es la por omisión.
//                         En el disco va en dos generaciones (al-aire.A.json y
//                         al-aire.B.json, core/persist/Generaciones.hpp); lo escribe un
//                         solo dueño (emision/DocumentoAire.hpp).
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::salidas {

/// La que existe desde el arranque (la misma que motor::Motor::kSalidaPorOmision).
inline constexpr const char* kPorOmision = "principal";
/// Cuántas salidas se pueden tener a la vez si salidas.json no dice otra cosa:
/// cada una es un programa, un preview y una mezcla más (ver sistema_recursos).
inline constexpr int kTopeDeFabrica = 4;
inline constexpr int kTopeMaximo = 16;

/// El fader (lineal) y el mute de una fuente en la mezcla de una salida.
struct Mando {
    double volumen = 1.0;
    bool mute = false;
    bool operator==(const Mando&) const = default;
};

struct Salida {
    std::string id;
    std::string nombre;
    std::string color; ///< "#RRGGBB": el del tally, el multiview y las superficies
    int orden = 0;
    /// El codificador de video que pide (un alias de EncoderConfig::videoEncoder: auto,
    /// nvenc, qsv, amf, vaapi, videotoolbox, x264); "" = el de la configuración.
    std::string codificador;
    /// Lo que se movió en su mezcla (las demás; la por omisión lo guarda en la escena).
    std::map<std::string, Mando> mezcla;
    /// El volumen general (VCA, lineal) de los grupos de su mezcla que no están en 1.
    std::map<std::string, double> grupos;
};

struct Lista {
    std::vector<Salida> salidas; ///< en orden; la por omisión siempre está
    int tope = kTopeDeFabrica;
    const Salida* buscar(const std::string& id) const;
};

/// De salidas.json (o {} si no hay): siempre con la por omisión, ordenada por `orden`.
Lista leer(const nlohmann::json& j);
nlohmann::json aJson(const Lista& l);
/// "" si sirve como id de una salida; si no, por qué no (solo letras, números, «-» y «_»:
/// va en los nombres de los productores del motor, de los canales del gateway y de los archivos).
std::string validarId(const std::string& id);
/// Suma una salida (con su color si no trae, y al final). "" o por qué no (repetida, tope).
std::string agregar(Lista& l, Salida s);
/// La saca (la por omisión no). "" o por qué no.
std::string quitar(Lista& l, const std::string& id);
/// Cambia nombre, color y codificador (lo que traiga `cambios`; `codificador` "" = el de la
/// configuración, y la por omisión no lleva: el suyo es el de la configuración). "" o por qué no.
std::string editar(Lista& l, const std::string& id, const nlohmann::json& cambios);
/// Anota el fader y el mute de `fuente` en la mezcla de la salida `id`. false si no está
/// (o es la por omisión) o si ya estaba así (no hay nada que guardar).
bool ponerMando(Lista& l, const std::string& id, const std::string& fuente, const Mando& m);
/// Lo mismo con el VCA de `grupo` (1 lo saca). false si no está, es la por omisión o ya estaba.
bool ponerGrupo(Lista& l, const std::string& id, const std::string& grupo, double ganancia);
/// El color que le toca a la salida número `n` (0 = la por omisión) si no eligió uno.
std::string colorDeFabrica(int n);

/// La salida de un destino de destinos.json (sin campo, o vacío = la por omisión).
std::string salidaDeDestino(const nlohmann::json& destino);
/// Cuántos destinos de `destinos` (destinos.json) salen con `salida`.
int destinosDe(const nlohmann::json& destinos, const std::string& salida);
/// Qué pasa con los destinos de la salida `salida` que se borra: una decisión, nunca
/// basura. `aDonde` = "borrar" los saca de `destinos` (sus ids van a `borrados`, para
/// sacar sus claves); el id de otra salida de `l` los pasa a ella (a la por omisión:
/// sin campo). Vacío solo si no tiene ninguno. "" o por qué no (sin tocar `destinos`).
std::string destinosDeBorrada(nlohmann::json& destinos, const Lista& l, const std::string& salida,
                              const std::string& aDonde, std::vector<std::string>* borrados);

/// El negro (FTB) y la placa de seguridad de una salida, encima de todo lo que sale
/// (emision/SeguridadAire). Las claves en JSON son las de BroadcastEngine::estadoPlaca.
struct Placa {
    bool ftb = false;
    bool ftbAudio = false; ///< el audio bajó con el negro
    bool placa = false;
    std::string texto, imagen;
    bool puesta() const { return ftb || placa; }
};
Placa placaDeJson(const nlohmann::json& j);
nlohmann::json aJson(const Placa& p);

/// Lo que tenía al aire cada salida, para volver tras un corte (Runtime/al-aire.json).
struct Aire {
    std::string escena;
    bool transmitiendo = false;           ///< su transmisión principal
    bool grabando = false;
    std::vector<std::string> destinos;    ///< los destinos (destinos.json) que transmitían su programa
    Placa placa;                          ///< el negro y la placa (antes, Runtime/placa.json)
};
/// La versión del documento: 2 = con la placa y la línea adentro (F3-04). Un arranque
/// lee la 1 (al-aire.json sin secciones, con placa.json y linea-aire.json aparte) una
/// versión más: ver DocumentoAire::leerAnterior.
inline constexpr int kVersionAire = 2;
struct AlAire {
    int version = kVersionAire; ///< la del archivo leído (1: de antes de las secciones)
    bool cerrado = true; ///< la corrida que lo escribió se cerró en orden
    std::map<std::string, Aire> salidas;
    /// La parte viva de la línea de tiempo (Corrida::aJsonVivo, con `evento`); null si
    /// no corre ninguna (o es un ensayo, que no se retoma).
    nlohmann::json linea;
    const Aire& de(const std::string& id) const;
};
/// Lee el viejo (campos sueltos = la por omisión) y el nuevo (con `salidas`).
AlAire leerAlAire(const nlohmann::json& j);
/// Escribe los campos sueltos de la por omisión (un arranque viejo, y el launcher, lo
/// siguen entendiendo) y `salidas` con todas; `linea` si corre.
nlohmann::json aJson(const AlAire& a);
/// Los dos archivos sueltos de la versión 1, como secciones de `a` (si `a` no las
/// trae): `placa` es Runtime/placa.json (lo de siempre de la por omisión y
/// `salidas: {id: {...}}` las demás); `linea`, el cuerpo de Runtime/linea-aire.json.
/// Lo que no es un objeto no suma nada.
void sumarSeccionesViejas(AlAire& a, const nlohmann::json& placa, const nlohmann::json& linea);

} // namespace ba::app::salidas
