// ============================================================================
// src/app/mcp/McpPlataformas.hpp — Lo común de YouTube, Twitch y Facebook en el MCP
//
// Una fábrica registra, para las tres, lo que hacen igual con otra API: *_status,
// *_setup, *_login, *_logout y *_videos (mismos nombres, títulos y esquemas que cuando
// eran tres copias). Todo pasa por la plataforma (ui/PlataformaViewModel): ninguna
// herramienta le habla al cliente HTTPS, así que la página se entera de cada cambio.
// Lo propio de cada una sigue en McpYouTubeTools/McpTwitchTools/McpFacebookTools, con
// los mismos ayudantes (responder, faltaParaUsar).
//
// Sin StudioViewModel: se prueba solo (tests/test_plataformas.cpp).
// ============================================================================
#pragma once

#include <QString>
#include <QVariantMap>

#include <functional>
#include <string>

#include "mcp/McpCore.hpp"
#include "ui/PlataformaViewModel.hpp"

namespace ba::app::mcp {

/// La respuesta de una plataforma -> resultado MCP: el error con su nombre adelante (una
/// vez, y traducido), o `okText` y los datos.
PlataformaViewModel::Done responder(Done done, const PlataformaViewModel& p, const std::string& okText = {});

/// Si a `p` le falta algo para usarla (la app, la cuenta, la Página), contesta el error con
/// lo que hay que hacer y devuelve true.
bool faltaParaUsar(const PlataformaViewModel& p, const Done& done);

/// accounts_status de una plataforma: su estadoCuenta() y el paso siguiente (`next`).
json estadoDeCuenta(const PlataformaViewModel& p);

/// Lo que la fábrica necesita de afuera.
struct Plataformas {
    PlataformaViewModel& youtube;
    PlataformaViewModel& twitch;
    PlataformaViewModel& facebook;
    /// Guardar una credencial: CuentasViewModel::saveAccountCredential, la misma puerta que
    /// el formulario seguro. "" o el error.
    std::function<QString(const QString& plataforma, const QVariantMap& campos)> guardarCredencial;
    /// Leer el JSON de Google de Descargas FUERA de la interfaz; `listo(ruta, contenido,
    /// error)` vuelve en la interfaz.
    std::function<void(std::function<void(const QString& ruta, const QString& contenido, const QString& error)> listo)>
        leerDeDescargas;
};

/// La fábrica: registra *_status, *_setup, *_login, *_logout y *_videos de las tres.
void registrarPlataformas(McpCore& core, const Plataformas& p);

} // namespace ba::app::mcp
