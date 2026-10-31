// ============================================================================
// src/app/mcp/McpToolsCommon.hpp — conversiones compartidas por las herramientas
// ============================================================================
#pragma once

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

#include "mcp/McpCore.hpp"

namespace ba::app {
class AppRuntime;
class StudioViewModel;
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

/// Estado de la sala tal como lo publica B (snapshot vigente, sin pedirlo).
json roomSnapshot(StudioViewModel& vm);
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
escenas::Conmutador& conmutadorDe(const McpCore& core, StudioViewModel& vm, const json& args);
/// El de una salida ya resuelta ("" = la por omisión).
escenas::Conmutador& conmutadorDe(StudioViewModel& vm, const std::string& salida);
/// [{id, nombre, color, por_omision, escena, armada, streaming, reconectando,
/// recording, destinos, limpia, repeticion, placa}], la por omisión primera. De la foto
/// del motor (en la interfaz: StudioViewModel::ventanilla().foto()).
json resumenSalidas(const FotoMotor& f);

} // namespace ba::app::mcp
