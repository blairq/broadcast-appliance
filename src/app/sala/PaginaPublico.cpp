// src/app/sala/PaginaPublico.cpp — cómo se ve la página del público (ver el .hpp)

#include "sala/PaginaPublico.hpp"

#include "i18n/I18n.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

namespace ba::app::sala {

PaginaPublico::PaginaPublico(ConferenceClient& sala) : sala_(sala) {}

std::string PaginaPublico::cambiar(const std::optional<std::string>& titulo, const std::optional<std::string>& cuando,
                                   const std::optional<std::string>& fondo, Listo listo) {
    if (!titulo && !cuando && !fondo) return ba::i18n::traducir("sala", "no hay nada para cambiar");
    nlohmann::json info = nlohmann::json::object();
    if (titulo) info["evento"] = *titulo;
    if (cuando) info["cuando"] = *cuando;
    nlohmann::json cuerpo{{"info", info}};
    if (fondo) {
        if (fondo->empty()) {
            cuerpo["fondo"] = "";
        } else {
            const QString ruta = QString::fromStdString(*fondo);
            QFile f(ruta);
            if (!f.open(QIODevice::ReadOnly)) return ba::i18n::llenar(ba::i18n::traducir("sala", "No pude leer la imagen: %1"), {*fondo});
            const QByteArray bytes = f.read(kMaxFondo + 1);
            if (bytes.size() > kMaxFondo)
                return ba::i18n::traducir("sala", "Esa imagen pesa mas de 2 MB. La bajan todos los telefonos del salon: "
                                                  "achicala antes.");
            cuerpo["fondo"] = bytes.toBase64().toStdString();
            cuerpo["fondoTipo"] = ruta.endsWith(".png", Qt::CaseInsensitive) ? "image/png" : "image/jpeg";
        }
    }
    pedir(sala_, "POST", "/api/v1/publico-info", cuerpo, std::move(listo));
    return {};
}

} // namespace ba::app::sala
