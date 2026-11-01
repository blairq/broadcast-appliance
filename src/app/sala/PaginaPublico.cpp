// src/app/sala/PaginaPublico.cpp — cómo se ve la página del público (ver el .hpp)

#include "sala/PaginaPublico.hpp"

#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "ui/ConferenceClient.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>

namespace ba::app::sala {

PaginaPublico::PaginaPublico(ConferenceClient& sala) : sala_(sala) {
    sala_.registrarRepublicador("publico", &vida_, [this]() { mandarGuardado(); });
}

std::string PaginaPublico::armar(const nlohmann::json& pedido, nlohmann::json& cuerpo) {
    nlohmann::json info = nlohmann::json::object();
    if (pedido.contains("titulo")) info["evento"] = pedido["titulo"];
    if (pedido.contains("cuando")) info["cuando"] = pedido["cuando"];
    cuerpo = {{"info", info}};
    if (!pedido.contains("fondo")) return {};
    const std::string fondo = pedido.value("fondo", std::string());
    if (fondo.empty()) {
        cuerpo["fondo"] = "";
        return {};
    }
    const QString ruta = QString::fromStdString(fondo);
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return ba::i18n::llenar(ba::i18n::traducir("sala", "No pude leer la imagen: %1"), {fondo});
    const QByteArray bytes = f.read(kMaxFondo + 1);
    if (bytes.size() > kMaxFondo)
        return ba::i18n::traducir("sala", "Esa imagen pesa mas de 2 MB. La bajan todos los telefonos del salon: "
                                          "achicala antes.");
    cuerpo["fondo"] = bytes.toBase64().toStdString();
    cuerpo["fondoTipo"] = ruta.endsWith(".png", Qt::CaseInsensitive) ? "image/png" : "image/jpeg";
    return {};
}

std::string PaginaPublico::cambiar(const std::optional<std::string>& titulo, const std::optional<std::string>& cuando,
                                   const std::optional<std::string>& fondo, Listo listo) {
    if (!titulo && !cuando && !fondo) return ba::i18n::traducir("sala", "no hay nada para cambiar");
    nlohmann::json pedido = nlohmann::json::object();
    if (titulo) pedido["titulo"] = *titulo;
    if (cuando) pedido["cuando"] = *cuando;
    if (fondo) pedido["fondo"] = *fondo;
    // La sala reemplaza la info entera: va con lo que ya estaba (lo que no vino, como
    // estaba); el fondo, solo si vino (es la imagen: no se manda de nuevo de gusto).
    nlohmann::json total = guardado_;
    total.update(pedido);
    nlohmann::json aMandar = total;
    if (!pedido.contains("fondo")) aMandar.erase("fondo");
    nlohmann::json cuerpo;
    if (std::string e = armar(aMandar, cuerpo); !e.empty()) return e;
    // Del evento: queda guardado.
    guardado_ = total;
    if (!deEste_.empty())
        ba::persist::cola().escribir(deEste_ / "publico.json",
                                     guardado_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
    pedir(sala_, "POST", "/api/v1/publico-info", cuerpo, std::move(listo));
    return {};
}

void PaginaPublico::setCarpetaEvento(std::function<std::filesystem::path()> carpeta) {
    carpeta_ = std::move(carpeta);
    eventoCambio();
}

void PaginaPublico::eventoCambio() {
    const std::filesystem::path carpeta = carpeta_ ? carpeta_() : std::filesystem::path();
    if (carpeta == deEste_) return;
    deEste_ = carpeta;
    guardado_ = nlohmann::json::object();
    if (carpeta.empty()) return;
    const int n = ++lectura_;
    QPointer<QObject> vida(&vida_);
    const std::filesystem::path archivo = carpeta / "publico.json";
    QThreadPool::globalInstance()->start([this, vida, n, archivo]() {
        std::string texto;
        nlohmann::json g = ba::persist::cola().leer(archivo, texto) ? nlohmann::json::parse(texto, nullptr, false)
                                                                    : nlohmann::json::object();
        if (!g.is_object()) g = nlohmann::json::object();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [this, vida, n, g = std::move(g)]() {
                if (!vida || n != lectura_) return;
                guardado_ = g;
                // Lo de este evento; si no tiene fondo, sin fondo (no el del evento anterior).
                if (!guardado_.contains("fondo")) guardado_["fondo"] = "";
                mandarGuardado();
            },
            Qt::QueuedConnection);
    });
}

void PaginaPublico::mandarGuardado() {
    if (guardado_.empty() || !sala_.online()) return;
    const nlohmann::json pedido = guardado_;
    QPointer<QObject> vida(&vida_);
    // La imagen es disco: se arma en otro hilo y se manda desde el de la interfaz.
    QThreadPool::globalInstance()->start([this, vida, pedido]() {
        nlohmann::json cuerpo;
        if (!armar(pedido, cuerpo).empty()) {
            nlohmann::json sinFondo = pedido;
            sinFondo["fondo"] = ""; // la imagen ya no está: lo demás igual
            armar(sinFondo, cuerpo);
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [this, vida, cuerpo = std::move(cuerpo)]() {
                if (vida) pedir(sala_, "POST", "/api/v1/publico-info", cuerpo, {});
            },
            Qt::QueuedConnection);
    });
}

} // namespace ba::app::sala
