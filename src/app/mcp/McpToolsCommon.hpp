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
class StudioViewModel;
}

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

} // namespace ba::app::mcp
