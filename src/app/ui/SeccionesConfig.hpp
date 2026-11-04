// ============================================================================
// src/app/ui/SeccionesConfig.hpp — qué sección de Configuración se pidió
//
// La página (SettingsPage.qml) arma cada sección por su CLAVE: la que lleva cada ajuste
// del esquema («Idioma», «Grabación»). Lo que se ve es su título, traducido y a veces
// con algo más («Idioma · Language»). Quien pide una sección (el asistente con
// ui_show_page, una guía, un botón) puede traer el título que vio: con ese, la página
// mostraba el título bien pero ningún campo (solo «Valores por defecto de esta
// sección»). Acá el pedido se lleva a la clave: un nombre viejo, la clave en otra
// capitalización o el título en el idioma del programa.
// Sin QML ni modelos: la usa StudioViewModel::setSeccionConfig y la prueba test_secciones_config.
// ============================================================================
#pragma once

#include <QString>
#include <QVariantMap>

namespace ba::app::secciones {

/// La clave de la sección pedida. `titulos`: {clave: título que se ve}
/// (SettingsModel::titulosSecciones). Si no se reconoce, la pedida tal cual (las
/// secciones propias de la página que no están en `titulos`).
QString canonica(const QString& pedida, const QVariantMap& titulos);

} // namespace ba::app::secciones
