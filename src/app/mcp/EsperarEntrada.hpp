#pragma once
// src/app/mcp/EsperarEntrada.hpp — esperar a que una entrada de red conecte
// (unos 2 s de cuadros, o un error) y devolver su estado con el diagnóstico,
// para que el asistente no tenga que volver a llamar video_estado.
//
// Dos maneras de mirar, según el hilo de la herramienta (F3-03): en el dueño del motor
// (las del grafo, Hilo::Control), al grafo mismo y con el turno (cada vuelta va a la
// agenda); en la interfaz (las de la red), a la vista del grafo que trae la foto.

#include "i18n/I18n.hpp"
#include "ui/VideoViewModel.hpp"
#include "video/Diagnostico.hpp"
#include "video/VideoGrafos.hpp"

#include <QPointer>
#include <QTimer>

#include <functional>
#include <string>

namespace ba::app::mcp {

/// La espera: `leer` da el estado de la entrada; `luego` corre lo que sigue en 400 ms donde
/// se pueda leer (o no lo corre, si quien lee ya no está: entonces avisa `cb` con el error).
inline void esperarEntradaCon(std::function<nlohmann::json(const std::string&)> leer,
                              std::function<void(std::function<void()>)> luego, std::string entrada, int restanteMs,
                              std::function<void(nlohmann::json)> cb) {
    nlohmann::json st = leer(entrada);
    const nlohmann::json g = st.value("gstreamer", nlohmann::json::object());
    const bool error = !st.value("error", std::string()).empty() || !g.value("error", std::string()).empty();
    if (error || g.value("recibidos", 0.0) >= 60 || restanteMs <= 0) {
        st["diagnostico"] = video::diagnosticar(st);
        if (!error && g.value("recibidos", 0.0) < 60)
            st["nota"] = ba::i18n::traducir("mcp", "todavía conectando: mirá de nuevo con video_estado en unos segundos");
        return cb(std::move(st));
    }
    // Función libre que se vuelve a llamar: no hay lambda que se apunte a sí misma.
    luego([leer, luego, entrada = std::move(entrada), restanteMs, cb]() mutable {
        esperarEntradaCon(std::move(leer), std::move(luego), std::move(entrada), restanteMs - 400, std::move(cb));
    });
}

/// En el dueño del motor (una herramienta del control): el grafo, cada vuelta en la agenda.
inline void esperarEntrada(QPointer<video::VideoGrafos> v, std::string entrada, int restanteMs,
                           std::function<void(nlohmann::json)> cb) {
    if (!v) return cb(nlohmann::json{{"error", ba::i18n::traducir("mcp", "el video ya no está")}});
    esperarEntradaCon(
        [v](const std::string& e) {
            return v ? v->estadoEntrada(e) : nlohmann::json{{"error", ba::i18n::traducir("mcp", "el video ya no está")}};
        },
        [v](std::function<void()> fn) {
            if (v) QTimer::singleShot(400, v.data(), [v, fn = std::move(fn)]() {
                    if (v) v->enLaAgenda(fn);
                });
        },
        std::move(entrada), restanteMs, std::move(cb));
}

/// En la interfaz: la vista del grafo (la foto del motor).
inline void esperarEntrada(QPointer<VideoViewModel> vv, std::string entrada, int restanteMs,
                           std::function<void(nlohmann::json)> cb) {
    if (!vv) return cb(nlohmann::json{{"error", ba::i18n::traducir("mcp", "el video ya no está")}});
    esperarEntradaCon(
        [vv](const std::string& e) {
            return vv ? vv->estadoEntrada(e) : nlohmann::json{{"error", ba::i18n::traducir("mcp", "el video ya no está")}};
        },
        [vv](std::function<void()> fn) {
            if (vv) QTimer::singleShot(400, vv.data(), std::move(fn));
        },
        std::move(entrada), restanteMs, std::move(cb));
}

} // namespace ba::app::mcp
