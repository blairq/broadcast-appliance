// ============================================================================
// src/app/arranque/ArchivoConfigQt.hpp — ArchivoConfig.hpp para los que arman QJson
//
// Los dueños que trabajan con QJsonObject (pantallas, cámaras, el estudio) leen y
// guardan por el MISMO lector/escritor con esquema: esto solo traduce el objeto.
// ============================================================================
#pragma once

#include "arranque/ArchivoConfig.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app::config {

/// leerObjeto, como QJsonObject.
inline QJsonObject leerQObjeto(const std::filesystem::path& archivo) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(
                                       leerObjeto(archivo).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object();
}
/// guardar, desde un QJsonObject.
inline bool guardarQ(const std::filesystem::path& archivo, const QJsonObject& o, ba::persist::Pedido p = {}) {
    return guardar(archivo,
                   nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false),
                   std::move(p));
}

} // namespace ba::app::config
