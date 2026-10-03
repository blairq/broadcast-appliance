#pragma once
// src/app/mcp/EsperarEntrada.hpp — esperar a que una entrada de red conecte
// (unos 2 s de cuadros, o un error) y devolver su estado con el diagnóstico,
// para que el asistente no tenga que volver a llamar video_estado.

#include "video/Diagnostico.hpp"
#include "video/VideoGrafos.hpp"

#include <QPointer>
#include <QTimer>

#include <functional>
#include <string>

namespace ba::app::mcp {

inline void esperarEntrada(QPointer<video::VideoGrafos> v, std::string entrada, int restanteMs, std::function<void(nlohmann::json)> cb) {
    if (!v) return cb(nlohmann::json{{"error", "el video ya no está"}});
    nlohmann::json st = v->estadoEntrada(entrada);
    const nlohmann::json g = st.value("gstreamer", nlohmann::json::object());
    const bool error = !st.value("error", std::string()).empty() || !g.value("error", std::string()).empty();
    if (error || g.value("recibidos", 0.0) >= 60 || restanteMs <= 0) {
        st["diagnostico"] = video::diagnosticar(st);
        if (!error && g.value("recibidos", 0.0) < 60)
            st["nota"] = "todavía conectando: mirá de nuevo con video_estado en unos segundos";
        return cb(std::move(st));
    }
    // Función libre que se vuelve a llamar: no hay lambda que se apunte a sí misma.
    QTimer::singleShot(400, [v, entrada = std::move(entrada), restanteMs, cb = std::move(cb)]() mutable {
        esperarEntrada(v, std::move(entrada), restanteMs - 400, std::move(cb));
    });
}

} // namespace ba::app::mcp
