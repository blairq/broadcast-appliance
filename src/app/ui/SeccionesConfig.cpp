// src/app/ui/SeccionesConfig.cpp — ver el .hpp

#include "ui/SeccionesConfig.hpp"

#include <QHash>

namespace ba::app::secciones {

QString canonica(const QString& pedida, const QVariantMap& titulos) {
    // Los nombres de antes de reordenar Configuración (2026-10-11), por si el
    // asistente o una guía los usan todavía.
    static const QHash<QString, QString> kAntes{{"Escena", "Video"},
                                                {"Gateway", "Red de la conferencia"},
                                                {"Red", "Red de la conferencia"},
                                                {"Sala", "Conferencia"},
                                                {"Red de la sala", "Red de la conferencia"},
                                                {"Acceso directo", "Cloudflare"},
                                                {"Acceso remoto", "Cloudflare"},
                                                {"cloudflare", "Cloudflare"},
                                                {"WireGuard", "Túnel propio (WireGuard)"},
                                                {"wireguard", "Túnel propio (WireGuard)"},
                                                {"Túnel propio", "Túnel propio (WireGuard)"},
                                                {"Sistema", "Sistema y datos"},
                                                {"Barra de navegación", "Apariencia"},
                                                {"Barra de navegacion", "Apariencia"},
                                                {"Audio y voz", "Voz del sistema"},
                                                {"Voz del operador", "Voz del sistema"},
                                                {"pinggy", "Pinggy"},
                                                {"capacidad", "Capacidad"}};
    const QString s = pedida.trimmed();
    if (titulos.contains(s)) return s;
    if (const auto it = kAntes.constFind(s); it != kAntes.cend()) return it.value();
    // La clave con otra capitalización, o el título que se ve (en el idioma del programa).
    for (auto it = titulos.cbegin(); it != titulos.cend(); ++it)
        if (it.key().compare(s, Qt::CaseInsensitive) == 0 || it.value().toString().compare(s, Qt::CaseInsensitive) == 0)
            return it.key();
    return s;
}

} // namespace ba::app::secciones
