// ============================================================================
// src/app/FuentesApp.hpp — tipos de fuente: sus propiedades y las que pinta la app
//
// Dos cosas que antes daba libobs:
//
//  1. Las PROPIEDADES de cada tipo (lo que la ventana «Propiedades» muestra y
//     edita): cada tipo registra la lista de filas (bool, int, float, text,
//     textarea, path, list, color, font, button, info, header) y sus valores
//     de fábrica. Un tipo sin registrar se edita igual: las filas salen de los
//     ajustes que tiene (ver PropertiesModel).
//
//  2. Las fuentes que PINTA LA APP (chat, gráficos, subtítulos...): el motor
//     las tiene como entradas «app» que esperan cuadros BGRA. El motor de la
//     app (BroadcastEngine) da de alta cada una acá con sus ajustes; quien
//     dibuja ese tipo (Qt/QPainter) pide las instancias y les entrega cuadros
//     por id. Todo bajo un mutex, por id (nunca punteros sueltos).
//
// Colores en los ajustes: entero 0xAABBGGRR (como los guardaba OBS, así los
// app.json viejos siguen valiendo).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ba::app {
class BroadcastEngine;
}

namespace ba::app::fuentes {

/// El motor de la app andando (nullptr antes de arrancar y después de apagar),
/// para las piezas que no lo reciben (la matriz, los racks). Hilo de la UI.
BroadcastEngine* motorApp();
void ponerMotorApp(BroadcastEngine* e);

struct Prop {
    std::string nombre;
    std::string etiqueta;
    std::string tipo; ///< bool, int, float, text, textarea, password, path, list, color, font, button, info, header
    double min = 0, max = 0, paso = 1;
    std::string sufijo;
    std::string ayuda;
    /// Para «list»: [{"value": ..., "label": "..."}].
    nlohmann::json opciones = nlohmann::json::array();
};

struct Tipo {
    std::string id;
    std::string nombre; ///< para «Agregar fuente»
    bool video = true, audio = false;
    bool deApp = false; ///< la pinta la app (entrada «app» del motor)
    nlohmann::json defectos = nlohmann::json::object();
    /// Las filas (pueden depender de los ajustes actuales).
    std::function<std::vector<Prop>(const nlohmann::json& ajustes)> props;
    /// Botones: nombre de la propiedad → qué hace con la fuente (nombre).
    std::map<std::string, std::function<void(const std::string& fuente)>> botones;
};

void registrar(Tipo t);
/// El tipo `id` (copia), o false si no está registrado.
bool tipo(const std::string& id, Tipo& out);
std::vector<Tipo> tipos();
/// Los ajustes con los valores de fábrica del tipo debajo.
nlohmann::json conDefectos(const std::string& id, const nlohmann::json& ajustes);

// --- Fuentes que pinta la app ----------------------------------------------------

struct Instancia {
    std::uint64_t id = 0;
    std::string nombre; ///< nombre de la fuente (y de la entrada del motor)
    std::string tipo;
    nlohmann::json ajustes; ///< ya con los de fábrica
    std::uint64_t version = 0; ///< cambia con cada alta/cambio de ajustes
};

/// Alta o cambio de ajustes (lo llama BroadcastEngine).
void alta(const std::string& nombre, const std::string& tipo, const nlohmann::json& ajustes);
void baja(const std::string& nombre);
void bajaTodas();
/// Las vivas de un tipo (copia).
std::vector<Instancia> de(const std::string& tipo);

/// A dónde van los cuadros (el motor): nombre, BGRA, ancho, alto, stride.
using Salida = std::function<void(const std::string&, const std::uint8_t*, int, int, int)>;
void ponerSalida(Salida s);
/// Un cuadro BGRA (alfa recto) para la instancia `id` (si sigue viva).
void empujar(std::uint64_t id, const std::uint8_t* bgra, int ancho, int alto, int stride = 0);

/// Cambiar ajustes de CUALQUIER fuente por nombre (se mezclan con los que
/// tiene y quedan guardados con la escena). Lo conecta BroadcastEngine; sirve
/// a piezas que no tienen el motor a mano (rótulos, gráficos). Hilo de la UI.
using Cambiar = std::function<bool(const std::string& nombre, const nlohmann::json& cambios)>;
void ponerCambiar(Cambiar c);
bool cambiarAjustes(const std::string& nombre, const nlohmann::json& cambios);

/// Filtros de video que otras piezas le agregan a la cadena de una entrada
/// (mejorar imagen, PTZ digital), por orden (menor = más cerca de la fuente).
/// Los junta el grafo de video (VideoGrafos) delante de sus tarjetas. Lista
/// vacía = sacarlos. Avisa a quien escucha (ponerAvisoExtras) si cambió.
void ponerFiltrosExtra(const std::string& entrada, int orden, const nlohmann::json& filtros);
/// Todos los extra de la entrada, en orden (arreglo de filtros del motor).
nlohmann::json filtrosExtra(const std::string& entrada);
void ponerAvisoExtras(std::function<void(const std::string& entrada)> fn);

/// Consultas sobre las fuentes del motor de la app (lo conecta BroadcastEngine).
struct Consultas {
    std::function<std::vector<std::pair<std::string, std::string>>()> listar; ///< (nombre, tipo)
    std::function<nlohmann::json(const std::string&)> ajustes;                ///< los de una fuente
    /// Propiedad de un elemento de la cadena de una entrada: (entrada, elemento, prop, valor) → error.
    std::function<std::string(const std::string&, const std::string&, const std::string&, const std::string&)> propiedad;
    /// Las propiedades legibles de ese elemento: (entrada, elemento) → objeto.
    std::function<nlohmann::json(const std::string&, const std::string&)> propiedades;
    /// Crea (si no existe) una entrada global de audio: (nombre, tipo, ajustes, monitor, pistas) → ok.
    std::function<bool(const std::string&, const std::string&, const nlohmann::json&, bool, const std::vector<int>&)>
        crearGlobal;
    /// Saca una entrada global (si no está en ninguna escena, se va del motor).
    std::function<void(const std::string&)> quitarGlobal;
};
void ponerConsultas(Consultas c);
/// Todas las fuentes: (nombre, tipo).
std::vector<std::pair<std::string, std::string>> listar();
/// Los ajustes guardados de la fuente `nombre` (objeto vacío si no existe).
nlohmann::json ajustesDe(const std::string& nombre);
/// Pone una propiedad a un elemento de la cadena de la entrada (ba_f_<id del filtro>). "" o el error.
std::string propiedadElemento(const std::string& entrada, const std::string& elemento, const std::string& prop,
                              const std::string& valor);
nlohmann::json propiedadesElemento(const std::string& entrada, const std::string& elemento);
/// Una entrada global de audio (las voces del sistema): si ya existe, la deja como está
/// (con el ruteo que le haya dado el operador). `monitor`: suena en los auriculares;
/// `pistas`: a qué mezclas va de fábrica (vacío: a ninguna). Hilo de la UI.
bool crearGlobal(const std::string& nombre, const std::string& tipo, const nlohmann::json& ajustes, bool monitor,
                 const std::vector<int>& pistas);
void quitarGlobal(const std::string& nombre);

} // namespace ba::app::fuentes
