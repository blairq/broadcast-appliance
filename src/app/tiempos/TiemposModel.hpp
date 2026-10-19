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
#include <thread>

namespace ba::app::tiempos {

class TiemposModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap mapa READ mapa NOTIFY cambio)
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY activoCambio)
    /// Los análisis (el último primero): [{id, estado: midiendo|listo|fallo, progreso 0..1,
    /// desde_ms, segundos, salidas: [{id, titulo, total_ms, p95_ms}], nodos: {id: {p50_ms,
    /// p95_ms, n}}, error}]. Quedan en el evento (Config/tiempos-analisis.json).
    Q_PROPERTY(QVariantList analisis READ analisis NOTIFY analisisCambio)
    /// Lo que pasó (los últimos 30 min, lo más nuevo al final): [{t, tipo: conexion |
    /// desconexion | escena | salida | latencia | tarde | rearme, texto, entrada}]. Sale de
    /// comparar cada foto con la anterior (no depende de que cada parte avise).
    Q_PROPERTY(QVariantList eventos READ eventos NOTIFY eventosCambio)
    /// Los nodos que se ven en la línea de tiempo de la página (en orden: el color sale de
    /// ahí). La eligen el operador (tocando tarjetas) o el asistente (tiempos_linea).
    Q_PROPERTY(QStringList linea READ linea WRITE setLinea NOTIFY lineaCambio)

   public:
    /// `fotos`: junta lo que publica cada parte (lo arma StudioViewModel; no espera a nadie).
    /// `archivo`: dónde quedan los análisis (el del evento abierto).
    TiemposModel(std::function<Fotos()> fotos, std::filesystem::path archivo, QObject* parent = nullptr);
    ~TiemposModel() override;

    /// El análisis por elemento (motor::Sondas) de una entrada: empezar devuelve "" o por qué
    /// no; terminar, lo medido. Se llaman en un hilo aparte (la entrada puede estar rearmándose).
    struct Sondas {
        std::function<std::string(const std::string&)> empezar;
        std::function<nlohmann::json(const std::string&)> terminar;
    };
    void setSondas(Sondas s) { sondas_ = std::move(s); }

    QVariantMap mapa() const { return mapaUi_; }
    bool activo() const { return timer_.isActive(); }
    void setActivo(bool a);

    /// El grafo de ahora (lo rehace en el momento; para el MCP).
    nlohmann::json ahora();
    /// Un nodo con su historia: [{t_ms, ms, acumulado_ms}] de los últimos `segundos`. null si no existe.
    nlohmann::json nodo(const std::string& id, int segundos = 60) const;
    /// La historia de un nodo de los últimos `segundos`: hasta 60 s, a 5 por segundo (si la
    /// página está abierta); más, a uno por segundo (se junta siempre, hasta 30 min).
    Q_INVOKABLE QVariantList historia(const QString& id, int segundos = 60) const;
    QVariantList eventos() const;
    QStringList linea() const { return linea_; }
    void setLinea(const QStringList& l) { if (l != linea_) { linea_ = l; emit lineaCambio(); } }
    nlohmann::json eventosJson(int segundos) const;
    /// Un evento propio (una nota del operador o del asistente) en el momento `t` (ms de época;
    /// 0 = ahora). Devuelve el evento.
    nlohmann::json agregarEvento(qint64 t, const std::string& texto, const std::string& quien = "operador");
    Q_INVOKABLE void agregarEventoUi(double t, const QString& texto) { agregarEvento(static_cast<qint64>(t), texto.toStdString()); }
    Q_INVOKABLE void borrarEventoUi(double t, const QString& texto);
    /// Cómo cambió cada nodo alrededor del momento `t`: la mediana de lo que agrega (y de lo
    /// acumulado) en los `segundos` de antes y en los de después, ordenado por cuánto cambió.
    nlohmann::json impacto(qint64 t, int segundos = 10) const;

    QVariantList analisis() const;
    /// Mide `segundos` (de 2 a 120) y deja un análisis con la mediana y el p95 de cada nodo
    /// y de cada salida. `listo` (si viene) recibe el análisis al terminar. Uno por vez:
    /// devuelve "" o por qué no arrancó.
    /// Mientras mide, las entradas llevan sondas por elemento: lo que agrega cada tarjeta (y
    /// lo que retiene) y en qué memoria va el video (dónde baja de la GPU y dónde sube).
    /// `entradas` vacío = todas las del motor.
    QString analizar(int segundos, std::function<void(const nlohmann::json&)> listo = {},
                     std::vector<std::string> entradas = {});
    Q_INVOKABLE QString analizarUi(int segundos) { return analizar(segundos); }
    /// Analizar solo una entrada (tocando su tarjeta).
    Q_INVOKABLE QString analizarEntradaUi(const QString& entrada, int segundos) {
        return analizar(segundos, {}, {entrada.toStdString()});
    }
    /// El último análisis por elemento de una entrada (null si no hay).
    nlohmann::json pipeline(const std::string& entrada) const;
    Q_INVOKABLE QVariantMap pipelineUi(const QString& entrada) const;
    Q_INVOKABLE void borrarAnalisis();

    static constexpr int kPeriodoMs = 200;
    static constexpr int kHistoriaS = 60;
    static constexpr int kHistoriaLargaS = 30 * 60;

   signals:
    void cambio();
    void activoCambio();
    void analisisCambio();
    void eventosCambio();
    void lineaCambio();

   private:
    void tic();
    /// Rehace el grafo; `avisar`: también lo que ve la página (si nadie la mira, no se avisa).
    bool rehacer(bool avisar = true);
    void cadaSegundo();
    void detectar(qint64 t);
    void evento(qint64 t, const std::string& tipo, const std::string& texto, const std::string& entrada = {});

    std::function<Fotos()> fotos_;
    QTimer timer_;
    nlohmann::json mapa_ = nlohmann::json::object();
    QVariantMap mapaUi_;
    struct Muestra {
        qint64 t = 0;
        double ms = 0, acumulado = 0;
    };
    std::map<std::string, std::deque<Muestra>> historia_;
    QStringList linea_;
    QTimer reloj1s_;
    std::map<std::string, std::deque<Muestra>> larga_;
    std::deque<nlohmann::json> eventos_;
    nlohmann::json previo_; ///< lo de la foto anterior que importa para los eventos
    nlohmann::json firmes_ = nlohmann::json::object(); ///< cambios que todavía no se sostuvieron (ver detectar)
    static constexpr qint64 kFirmeMs = 5000;
    static constexpr qint64 kArranqueMs = 10000;
    qint64 arranque_ = 0;
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
    void cerrar(); ///< deja el análisis listo (después de juntar las sondas)
    Sondas sondas_;
    std::vector<std::string> conSondas_;   ///< las entradas con sondas puestas en este análisis
    std::map<std::string, nlohmann::json> pipelines_; ///< el último análisis por elemento de cada entrada
    std::thread hilo_;                     ///< pone y saca las sondas (fuera de la UI)
    void enHilo(std::function<void()> f);
    void guardar() const;
};

} // namespace ba::app::tiempos
