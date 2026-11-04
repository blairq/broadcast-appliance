// ============================================================================
// src/app/mcp/McpToolsCommon.hpp — conversiones compartidas por las herramientas
// ============================================================================
#pragma once

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <tuple>

#include "mcp/McpCore.hpp"

namespace ba::app {
class AppRuntime;
class InterfazMcp;
struct FotoMotor;
namespace escenas {
class Conmutador;
}
} // namespace ba::app

namespace ba::app::mcp {

json qvToJson(const QVariant& v);
QVariant jsonToQv(const json& v);
json toJson(const QJsonObject& o);
json toJson(const QJsonArray& a);
QJsonObject toQt(const json& j);
/// Filas de un modelo Qt como JSON (cada rol con su nombre).
json modelRows(const QAbstractItemModel& m);

/// Lo pesado de una herramienta de la interfaz (escalar y codificar una imagen, leer o
/// recorrer archivos, esperar un proceso) en un hilo aparte: `trabajo` corre en el pool
/// del MCP («mcp-aparte», dos hilos: no compite con la copia de un evento en el pool
/// global) y `done` vuelve en el hilo de `volver` (la interfaz: qApp). En la interfaz queda
/// solo lo que es de ella (tomar el cuadro, leer el modelo). Una excepción de `trabajo`
/// vuelve como error de la herramienta.
void enPool(QObject* volver, std::function<ToolResult()> trabajo, Done done);

/// Esperar algo que AVISA, en vez de sondearlo con un reloj: cada vez que `obj` emite
/// `senal` (o cualquiera de las de una std::tuple; y una vez al empezar) se pregunta `mirar()`; cuando trae un resultado se
/// contesta con él, y si pasan `topeMs` sin resultado, con `alVencer()`. En el hilo de
/// `obj` (la interfaz, si es un modelo de ella). Una sola respuesta; lo de la espera se
/// borra solo (y si `obj` se va antes, también: entonces contesta `alVencer()`).
template <class Obj, class... Senal>
void esperarHasta(Obj* obj, std::tuple<Senal...> senales, std::function<std::optional<ToolResult>()> mirar, int topeMs,
                  std::function<ToolResult()> alVencer, Done done) {
    if (auto ya = mirar()) return done(*ya);
    auto* espera = new QObject(obj); // dueño de las conexiones y del reloj: se va con ellas
    auto terminado = std::make_shared<bool>(false);
    const auto terminar = [espera, terminado, done](ToolResult r) {
        if (*terminado) return;
        *terminado = true;
        espera->deleteLater();
        done(std::move(r));
    };
    std::apply(
        [&](auto... s) {
            (QObject::connect(obj, s, espera, [mirar, terminar]() {
                 if (auto r = mirar()) terminar(*r);
             }),
             ...);
        },
        senales);
    QTimer::singleShot(topeMs, espera, [alVencer, terminar]() { terminar(alVencer()); });
    QObject::connect(espera, &QObject::destroyed, [alVencer, terminado, done]() {
        if (*terminado) return;
        *terminado = true;
        done(alVencer());
    });
}
/// Con una sola señal (lo más común).
template <class Obj, class Senal>
void esperarHasta(Obj* obj, Senal senal, std::function<std::optional<ToolResult>()> mirar, int topeMs,
                  std::function<ToolResult()> alVencer, Done done) {
    esperarHasta(obj, std::make_tuple(senal), std::move(mirar), topeMs, std::move(alVencer), std::move(done));
}

/// Estado de la sala tal como lo publica B (snapshot vigente, sin pedirlo).
json roomSnapshot(InterfazMcp& vm);
/// Participante por id, nombre exacto (sin mayusculas) o prefijo unico.
bool resolveParticipant(const json& snapshot, const std::string& query, json& out, std::string& err);
/// Clave del FrameService con su video vivo en A ("guest:N", "lobby:K",
/// "backstage:K"), o vacio si A no recibe su video ahora.
std::string personFeedKey(const json& participant);

/// La advertencia de las herramientas que rearman el motor (el lienzo, los cuadros
/// por segundo, el reloj del estudio): una sola, para todas las que lo hacen. Va en la
/// descripción, así que en el original (ver traducirParaVer).
std::string avisoReiniciaMotor();

// --- Las salidas (PLAN-SALIDAS; ver McpSalidasTools.cpp) ---------------------
/// El conmutador (aire, armada, take, T-bar, AFV) de la salida de la llamada en
/// curso (McpCore::salidaDe): el de las herramientas de escenas con `salida`.
escenas::Conmutador& conmutadorDe(const McpCore& core, InterfazMcp& vm, const json& args);
/// El de una salida ya resuelta ("" = la por omisión).
escenas::Conmutador& conmutadorDe(InterfazMcp& vm, const std::string& salida);
/// [{id, nombre, color, por_omision, escena, armada, streaming, reconectando,
/// recording, destinos, limpia, repeticion, placa}], la por omisión primera. De la foto
/// del motor (en la interfaz: StudioViewModel::ventanilla().foto()).
json resumenSalidas(const FotoMotor& f);

} // namespace ba::app::mcp
