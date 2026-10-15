// ============================================================================
// src/app/tiempos/TiemposModel.hpp — la página «Latencias del pipeline» y el MCP (tiempos_*)
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
#include <filesystem>
#include <functional>
#include <vector>
#include <map>
#include <string>

namespace ba::app::tiempos {

class TiemposModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap mapa READ mapa NOTIFY cambio)
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY activoCambio)
    /// Los análisis (el último primero): [{id, estado: midiendo|listo|fallo, progreso 0..1,
    /// desde_ms, segundos, salidas: [{id, titulo, total_ms, p95_ms}], nodos: {id: {p50_ms,
    /// p95_ms, n}}, error}]. Quedan en el evento (Config/tiempos-analisis.json).
    Q_PROPERTY(QVariantList analisis READ analisis NOTIFY analisisCambio)

   public:
    /// `fotos`: junta lo que publica cada parte (lo arma StudioViewModel; no espera a nadie).
    /// `archivo`: dónde quedan los análisis (el del evento abierto).
    TiemposModel(std::function<Fotos()> fotos, std::filesystem::path archivo, QObject* parent = nullptr);

    QVariantMap mapa() const { return mapaUi_; }
    bool activo() const { return timer_.isActive(); }
    void setActivo(bool a);

    /// El grafo de ahora (lo rehace en el momento; para el MCP).
    nlohmann::json ahora();
    /// Un nodo con su historia: [{t_ms, ms, acumulado_ms}] de los últimos 60 s (si la
    /// página estuvo abierta). null si no existe.
    nlohmann::json nodo(const std::string& id) const;
    Q_INVOKABLE QVariantList historia(const QString& id) const;

    QVariantList analisis() const;
    /// Mide `segundos` (de 2 a 120) y deja un análisis con la mediana y el p95 de cada nodo
    /// y de cada salida. `listo` (si viene) recibe el análisis al terminar. Uno por vez:
    /// devuelve "" o por qué no arrancó.
    QString analizar(int segundos, std::function<void(const nlohmann::json&)> listo = {});
    Q_INVOKABLE QString analizarUi(int segundos) { return analizar(segundos); }
    Q_INVOKABLE void borrarAnalisis();

    static constexpr int kPeriodoMs = 200;
    static constexpr int kHistoriaS = 60;

   signals:
    void cambio();
    void activoCambio();
    void analisisCambio();

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
    // El análisis en curso (uno por vez) y los hechos.
    std::filesystem::path archivo_;
    nlohmann::json analisis_ = nlohmann::json::array();
    QTimer muestreo_;
    qint64 desde_ = 0;
    int segundos_ = 0;
    std::map<std::string, std::vector<double>> muestrasNodo_, muestrasSalida_;
    std::map<std::string, std::string> titulosSalida_;
    std::function<void(const nlohmann::json&)> listo_;
    void muestrear();
    void terminar();
    void guardar() const;
};

} // namespace ba::app::tiempos
