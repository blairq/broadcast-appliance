// ============================================================================
// src/app/tiempos/TiemposModel.hpp — la página «Tiempos» y el MCP (tiempos_*)
//
// Arma el grafo de demoras (tiempos::armar) con las fotos que ya se publican y lo
// expone como propiedad (`mapa`, avisa con `cambio`): la vista se engancha a eso, no
// a funciones que se leen una vez. Mientras la página está abierta (`activo`) lo
// rehace 5 veces por segundo y guarda la historia de cada nodo (los últimos 60 s);
// cerrada no cuesta nada. El MCP pide una foto cuando quiere (`ahora`).
// ============================================================================
#pragma once

#include "tiempos/Tiempos.hpp"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <deque>
#include <functional>
#include <map>
#include <string>

namespace ba::app::tiempos {

class TiemposModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap mapa READ mapa NOTIFY cambio)
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY activoCambio)

   public:
    /// `fotos`: junta lo que publica cada parte (lo arma StudioViewModel; no espera a nadie).
    explicit TiemposModel(std::function<Fotos()> fotos, QObject* parent = nullptr);

    QVariantMap mapa() const { return mapaUi_; }
    bool activo() const { return timer_.isActive(); }
    void setActivo(bool a);

    /// El grafo de ahora (lo rehace en el momento; para el MCP).
    nlohmann::json ahora();
    /// Un nodo con su historia: [{t_ms, ms, acumulado_ms}] de los últimos 60 s (si la
    /// página estuvo abierta). null si no existe.
    nlohmann::json nodo(const std::string& id) const;
    Q_INVOKABLE QVariantList historia(const QString& id) const;

    static constexpr int kPeriodoMs = 200;
    static constexpr int kHistoriaS = 60;

   signals:
    void cambio();
    void activoCambio();

   private:
    void tic();
    bool rehacer();
    std::function<Fotos()> fotos_;
    QTimer timer_;
    nlohmann::json mapa_ = nlohmann::json::object();
    QVariantMap mapaUi_;
    struct Muestra {
        qint64 t = 0;
        double ms = 0, acumulado = 0;
    };
    std::map<std::string, std::deque<Muestra>> historia_;
};

} // namespace ba::app::tiempos
