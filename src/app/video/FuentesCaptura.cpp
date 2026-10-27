// ============================================================================
// src/app/video/FuentesCaptura.cpp — ver FuentesCaptura.hpp
// ============================================================================
#include "video/FuentesCaptura.hpp"

#include "emision/FuentesApp.hpp"
#include "i18n/I18n.hpp"

#include "Capturas.hpp" // el del motor (src/motor)

#if BA_APP_HAVE_GUI
#include <QGuiApplication>
#include <QScreen>
#endif

#include <cmath>

namespace ba::app::video {

using json = nlohmann::json;

std::vector<Pantalla> pantallasDelEquipo() {
    std::vector<Pantalla> out;
#if BA_APP_HAVE_GUI
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) return out;
    const QList<QScreen*> pantallas = QGuiApplication::screens();
    QScreen* principal = QGuiApplication::primaryScreen();
    int n = 0;
    for (QScreen* s : pantallas) {
        Pantalla p;
        p.nombre = s->name().toStdString();
        const QRect g = s->geometry();
        const double r = s->devicePixelRatio();
        p.x = g.x(), p.y = g.y(), p.ancho = g.width(), p.alto = g.height();
        p.pxX = static_cast<int>(std::lround(g.x() * r)), p.pxY = static_cast<int>(std::lround(g.y() * r));
        p.pxAncho = static_cast<int>(std::lround(g.width() * r)), p.pxAlto = static_cast<int>(std::lround(g.height() * r));
        p.principal = s == principal;
        // «Pantalla 2: DELL U2720Q — 2560×1440 (principal)»: el modelo si lo dice, si no su nombre.
        QString quien = s->model().trimmed();
        if (quien.isEmpty()) quien = s->manufacturer().trimmed();
        if (quien.isEmpty() || quien == s->name()) quien = s->name().startsWith("\\\\.\\") ? QString() : s->name();
        const std::string num = std::to_string(++n), tam = std::to_string(p.pxAncho) + "×" + std::to_string(p.pxAlto);
        if (quien.isEmpty())
            p.etiqueta = ba::i18n::llenar(p.principal ? ba::i18n::traducir("captura", "Pantalla %1 — %2 (principal)")
                                                      : ba::i18n::traducir("captura", "Pantalla %1 — %2"),
                                          {num, tam});
        else
            p.etiqueta = ba::i18n::llenar(p.principal ? ba::i18n::traducir("captura", "Pantalla %1: %2 — %3 (principal)")
                                                      : ba::i18n::traducir("captura", "Pantalla %1: %2 — %3"),
                                          {num, quien.toStdString(), tam});
        out.push_back(p);
    }
#endif
    return out;
}

json ajustesPantalla(const json& in) {
    json a{{"tipo", "pantalla"}, {"cursor", in.value("show_cursor", true)}};
    // Lo viejo (de OBS): un índice.
    a["indice"] = in.value("display", in.value("monitor_index", 0));
    const std::string elegido = in.value("monitor", json()).is_string() ? in.value("monitor", std::string()) : std::string();
    const auto ps = pantallasDelEquipo();
    const Pantalla* p = nullptr;
    for (const auto& x : ps)
        if (x.nombre == elegido) p = &x;
    if (p == nullptr && in.value("monitor", json()).is_number_integer()) { // «monitor» como índice
        const int i = in.value("monitor", 0);
        if (i >= 0 && i < static_cast<int>(ps.size())) p = &ps[static_cast<size_t>(i)];
    }
    if (p == nullptr)
        for (const auto& x : ps)
            if (x.principal) p = &x; // sin elegir (o desconectado): el principal
    if (p != nullptr) {
        a["monitor"] = p->nombre;
        a["x"] = p->x, a["y"] = p->y, a["ancho"] = p->ancho, a["alto"] = p->alto;
        a["px"] = {p->pxX, p->pxY, p->pxAncho, p->pxAlto};
    }
    return a;
}

json ajustesVentana(const json& in) {
    const std::string v = in.value("ventana", std::string());
    const auto corte = v.find('\n');
    return {{"tipo", "ventana"},
            {"app", corte == std::string::npos ? v : v.substr(0, corte)},
            {"titulo", corte == std::string::npos ? std::string() : v.substr(corte + 1)},
            {"cursor", in.value("show_cursor", true)}};
}

json ventanasParaElegir() {
    json out = json::array();
    for (const auto& w : ba::motor::ventanasCapturables()) {
        std::string t = w.titulo.size() > 70 ? w.titulo.substr(0, 67) + "…" : w.titulo;
        out.push_back({{"valor", w.app + "\n" + w.titulo},
                       {"etiqueta", (w.app.empty() ? std::string() : w.app + " — ") + t},
                       {"app", w.app},
                       {"titulo", w.titulo},
                       {"ancho", w.ancho},
                       {"alto", w.alto}});
    }
    return out;
}

void registrarTiposCaptura() {
    using fuentes::Prop;
    using fuentes::Tipo;
    {
        Tipo t;
        t.id = kTipoPantalla;
        t.nombre = ba::i18n::traducir("captura", "Captura de pantalla");
        t.defectos = {{"show_cursor", true}};
        t.props = [](const json& a) {
            Prop m{"monitor", ba::i18n::traducir("captura", "Pantalla"), "list"};
            const auto ps = pantallasDelEquipo();
            const std::string elegido = a.value("monitor", json()).is_string() ? a.value("monitor", std::string()) : std::string();
            bool esta = elegido.empty();
            for (const auto& p : ps) {
                m.opciones.push_back({{"value", p.nombre}, {"label", p.etiqueta}});
                esta = esta || p.nombre == elegido;
            }
            if (!esta)
                m.opciones.push_back(
                    {{"value", elegido},
                     {"label", ba::i18n::llenar(ba::i18n::traducir("captura", "%1 (no está conectada: se usa la principal)"),
                                                {elegido})}});
            m.ayuda = ps.size() > 1 ? ba::i18n::llenar(ba::i18n::traducir("captura",
                                                                          "Cuál de las %1 pantallas conectadas capturar."),
                                                       {std::to_string(ps.size())})
                                    : ba::i18n::traducir("captura", "La pantalla de esta máquina.");
            return std::vector<Prop>{m, {"show_cursor", ba::i18n::traducir("captura", "Mostrar el puntero"), "bool"}};
        };
        fuentes::registrar(t);
    }
    {
        Tipo t;
        t.id = kTipoVentana;
        t.nombre = ba::i18n::traducir("captura", "Captura de una aplicación");
        t.defectos = {{"show_cursor", true}};
        t.props = [](const json& a) {
            std::vector<Prop> out;
            std::string porque;
            if (!ba::motor::hayCapturaDeVentana(&porque)) {
                Prop i{"aviso", ba::i18n::traducir("captura", "No disponible"), "info"};
                i.ayuda = ba::i18n::llenar(
                    ba::i18n::traducir("captura", "En este sistema no se puede capturar una ventana sola: %1."), {porque});
                out.push_back(i);
            }
            Prop v{"ventana", ba::i18n::traducir("captura", "Aplicación"), "list"};
            const std::string elegido = a.value("ventana", std::string());
            bool esta = elegido.empty();
            for (const auto& w : ventanasParaElegir()) {
                v.opciones.push_back({{"value", w["valor"]}, {"label", w["etiqueta"]}});
                esta = esta || w["valor"] == elegido;
            }
            if (!esta) {
                const auto corte = elegido.find('\n');
                const std::string app = elegido.substr(0, corte);
                v.opciones.push_back(
                    {{"value", elegido},
                     {"label", ba::i18n::llenar(ba::i18n::traducir("captura", "%1 (no está abierta: se espera a que se abra)"),
                                                {app})}});
            }
            v.ayuda = ba::i18n::traducir("captura", "La ventana a capturar (aunque esté tapada). Se recuerda la "
                                                    "aplicación: si se cierra y se vuelve a abrir, la captura sigue sola.");
            out.push_back(v);
            out.push_back({"show_cursor", ba::i18n::traducir("captura", "Mostrar el puntero"), "bool"});
            return out;
        };
        fuentes::registrar(t);
    }
}

} // namespace ba::app::video
