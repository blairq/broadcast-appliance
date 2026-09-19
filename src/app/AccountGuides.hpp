// ============================================================================
// src/app/AccountGuides.hpp — como conectar YouTube, Twitch y Facebook
//
// UNA sola fuente para los pasos de configuracion: las pestañas de cada
// plataforma los muestran y el asistente (MCP: account_setup_guide) los usa
// para guiar a la persona. Cada guia trae los pasos con sus enlaces, los
// valores exactos a pegar (URI de redireccion, tipo de cliente...) y los
// campos de la credencial que pide el formulario seguro de la barra del
// operador (el secreto lo escribe la persona en la app: nunca pasa por el
// chat con el asistente).
// ============================================================================
#pragma once

#include <QString>

#include <nlohmann/json.hpp>

namespace ba::app::accounts {

/// "youtube", "twitch" o "facebook". Vacio si no es ninguna.
std::string normalize(const std::string& platform);

/// Guia de una plataforma: {platform, title, intro, minutes, steps[{n, t, d,
/// links[{l, u}], l, u}], paste{...}, credential{fields[{key, label, secret,
/// placeholder}], importFile}, notes[]}. `fbRedirect`: la URI de redireccion
/// de Facebook de esta instalacion (depende del puerto elegido).
nlohmann::json guide(const std::string& platform, const std::string& fbRedirect = {});

/// El JSON del cliente OAuth de Google mas nuevo en la carpeta de Descargas
/// ("client_secret_*.json", el nombre con que Google lo descarga). Vacio si no
/// hay; `why` explica por que.
QString findGoogleClientJson(QString* why = nullptr);

} // namespace ba::app::accounts
