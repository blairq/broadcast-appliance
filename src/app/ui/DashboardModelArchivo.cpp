// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/ui/DashboardModelArchivo.cpp — la disposición del tablero de la app, en un archivo
// (Config/ui.json). Aparte de DashboardModel.cpp para que el tablero web (src/tablero-web)
// use el mismo modelo con otro almacén, sin arrastrar la configuración en disco.
#include "ui/DashboardModel.hpp"

#include "arranque/ArchivoConfigQt.hpp"

namespace ba::app {

DashboardModel::DashboardModel(std::filesystem::path file, QObject* parent)
    : DashboardModel(
          Almacen{[file]() { return config::leerQObjeto(file); },
                  // Ruta vacía = no se persiste (el arranque que no quiere pisar un ui.json roto).
                  file.empty() ? std::function<void(const QJsonObject&)>() : [file](const QJsonObject& o) { config::guardarQ(file, o); }},
          parent) {}

} // namespace ba::app
