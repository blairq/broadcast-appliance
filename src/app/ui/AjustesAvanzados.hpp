// ============================================================================
// src/app/ui/AjustesAvanzados.hpp — la tabla de los ajustes «avanzados»
//
// Los que antes eran valores fijos en el código (docs/planes/VALORES-FIJOS.md).
// El código los lee con ba::ajustes (src/core/ajustes) con el MISMO valor por
// defecto que esta tabla; acá están el rótulo, el rango, la explicación y dónde
// se guardan: app.json (la app), gateway.json (la sala) o launcher.json (el
// arranque), siempre bajo "ajustes".
//
// Los textos (rótulo, explicación, opciones, unidades con palabras) van marcados con
// QT_TRANSLATE_NOOP("ajustes", …): la tabla se arma una vez; los traduce SettingsModel.
// La sección es una clave: no se traduce acá (SettingsModel::seccionVisible).
// ============================================================================
#pragma once

#include <QString>
#include <QVariant>

#include <utility>
#include <vector>

namespace ba::app {

struct AjusteAvanzado {
    enum class Destino { App, Sala, Launcher };
    QString key, section, label, kind;
    std::vector<std::pair<QString, QString>> opciones; ///< para "choice": {valor, rótulo}
    double min = 0, max = 0;
    QString suffix, help, scope;
    QVariant def;
    Destino destino = Destino::App;
};

const std::vector<AjusteAvanzado>& ajustesAvanzados();

} // namespace ba::app
