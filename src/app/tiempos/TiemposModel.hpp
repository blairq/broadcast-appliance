// ============================================================================
// src/app/tiempos/TiemposModel.hpp — la página «Latencias del pipeline» y el MCP (tiempos_*)
//
// Arma el grafo de demoras (tiempos::armar) con las fotos que ya se publican y lo
// expone como propiedad (`mapa`, avisa con `cambio`): la vista se engancha a eso, no
// a funciones que se leen una vez. Mientras alguna vista lo usa (`usar`, cuenta por vista) lo
// rehace 5 veces por segundo y guarda la historia de cada nodo (los últimos 60 s);
// cerrada no cuesta nada. El MCP pide una foto cuando quiere (`ahora`).
// ============================================================================
#pragma once

#include "comun/UsoPorVistas.hpp"
#include "tiempos/FilasTiempo.hpp"
#include "tiempos/Tiempos.hpp"

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include "ui/CapturaError.hpp"

namespace ba::app::tiempos {

class TiemposModel : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    // LO QUE VE LA PÁGINA, separado por ritmo (con 200 invitados son ~800 tarjetas): la
    // forma del grafo solo cuando cambia, los números por fila y la historia a 1 por segundo.
    /// La forma: {nodos: [sin los números], tramos: [{desde, hacia, medio}]}. Avisa solo
    /// cuando aparece, se va o cambia de clase/estado un nodo (el acomodo de la página).
    Q_PROPERTY(QVariantMap estructura READ estructura NOTIFY estructuraCambio)
    /// Los nodos, una fila por id (FilasTiempo: clave, dato, historia): cada vuelta avisa
    /// solo las filas que cambiaron; la historia, una vez por segundo.
    Q_PROPERTY(QObject* nodos READ nodos CONSTANT)
    /// Los tramos, una fila por «desde>hacia» (clave, dato).
    Q_PROPERTY(QObject* tramos READ tramos CONSTANT)
    /// Lo chico de cada vuelta: {salidas, estudio, programa_ms, recorrido (el detalle de la
    /// entrada de recorridoDe)}.
    Q_PROPERTY(QVariantMap resumen READ resumen NOTIFY cambio)
    /// La entrada cuyo recorrido mira la página (su detalle llega en resumen.recorrido).
    Q_PROPERTY(QString recorridoDe READ recorridoDe WRITE setRecorridoDe NOTIFY cambio)
    /// Alguna vista lo usa (usar): mide 5 veces por segundo.
    Q_PROPERTY(bool activo READ activo NOTIFY activoCambio)
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
    /// Cuántos segundos muestra la línea de tiempo (la elige el operador: 30 s a 30 min).
    Q_PROPERTY(int ventanaLineaS READ ventanaLineaS WRITE setVentanaLineaS NOTIFY serieCambio)
    /// Los nodos de `linea` con su historia de `ventanaLineaS`: [{id, datos}].
    Q_PROPERTY(QVariantList serieLinea READ serieLinea NOTIFY serieCambio)
    /// La hora de la última vuelta (ms): el borde derecho de la línea de tiempo.
    Q_PROPERTY(double finMs READ finMs NOTIFY cambio)

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

    QVariantMap estructura() const { return estructuraUi_; }
    QObject* nodos() const { return nodos_; }
    QObject* tramos() const { return tramos_; }
    FilasTiempo* filasNodos() const { return nodos_; }
    QVariantMap resumen() const { return resumenUi_; }
    QString recorridoDe() const { return recorridoDe_; }
    void setRecorridoDe(const QString& e) {
        if (e == recorridoDe_) return;
        recorridoDe_ = e;
        publicar(false);
    }
    bool activo() const { return timer_.isActive(); }
    /// Una vista (la página) empieza o deja de mirar.
    Q_INVOKABLE void usar(QObject* vista, bool si, const QString& que = {}) { vistas_.usar(vista, si, que); }

    /// El grafo de ahora (lo rehace en el momento; para el MCP).
    nlohmann::json ahora();
    /// Un nodo con su historia: [{t_ms, ms, acumulado_ms}] de los últimos `segundos`. null si no existe.
    nlohmann::json nodo(const std::string& id, int segundos = 60) const;
    /// La historia de un nodo de los últimos `segundos`: hasta 60 s, a 5 por segundo (si la
    /// página está abierta); más, a uno por segundo (se junta siempre, hasta 30 min).
    QVariantList historia(const QString& id, int segundos = 60) const;
    int ventanaLineaS() const { return ventanaLineaS_; }
    void setVentanaLineaS(int s);
    QVariantList serieLinea() const { return serieUi_; }
    double finMs() const { return static_cast<double>(finMs_); }
    QVariantList eventos() const;
    QStringList linea() const { return linea_; }
    void setLinea(const QStringList& l) {
        if (l == linea_) return;
        linea_ = l;
        emit lineaCambio();
        armarSerie();
    }
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
    Q_INVOKABLE void analizarUi(int segundos) { avisarError(analizar(segundos)); }
    /// Analizar solo una entrada (tocando su tarjeta).
    Q_INVOKABLE void analizarEntradaUi(const QString& entrada, int segundos) {
        avisarError(analizar(segundos, {}, {entrada.toStdString()}));
    }
    /// El último análisis por elemento de una entrada, para leerlo (el MCP): {entrada, t_ms,
    /// hace_s, segundos, rearmada, resumen, hallazgos, tarjetas: [{id, titulo, ms,
    /// ms_por_cuadro, memoria_entrada, memoria_salida, cruces, elementos}], elementos: [cada
    /// uno con su tarjeta]}. null si esa entrada no se analizó.
    nlohmann::json recorrido(const std::string& entrada);
    /// ¿Existe esa entrada en el motor?
    bool existeEntrada(const std::string& entrada) const;
    Q_INVOKABLE void borrarAnalisis();

    static constexpr int kPeriodoMs = 200;
    static constexpr int kHistoriaS = 60;
    static constexpr int kHistoriaLargaS = 30 * 60;

   signals:
    /// Para el operador (ya traducido): los errores de las acciones de la interfaz.
    void notify(const QString& texto, const QString& tipo);
    void cambio();
    void estructuraCambio();
    void activoCambio();
    void analisisCambio();
    void eventosCambio();
    void lineaCambio();
    void serieCambio();

   private:
    void tic();
    /// Rehace el grafo; `avisar`: también lo que ve la página (si nadie la mira, no se avisa).
    bool rehacer(bool avisar = true);
    void cadaSegundo();
    void detectar(qint64 t);
    void evento(qint64 t, const std::string& tipo, const std::string& texto, const std::string& entrada = {});

    std::function<Fotos()> fotos_;
    QTimer timer_;
    UsoPorVistas vistas_{this, [this](const QString&) { setActivo(vistas_.alguien()); }};
    void setActivo(bool a);
    nlohmann::json mapa_ = nlohmann::json::object();
    /// Lo que ve la página, armado de mapa_ (ver estructura/nodos/tramos/resumen). Con
    /// `conHistorias`, también la historia de cada tarjeta.
    void publicar(bool conHistorias);
    FilasTiempo* nodos_ = new FilasTiempo(this);
    FilasTiempo* tramos_ = new FilasTiempo(this);
    QVariantMap estructuraUi_;
    std::string firmaEstructura_;
    QVariantMap resumenUi_;
    QString recorridoDe_;
    int vueltas_ = 0; ///< cuántas vueltas (la historia va una de cada 5)
    /// El último dato publicado de cada fila (para no convertir ni avisar lo que no cambió).
    std::map<std::string, std::pair<nlohmann::json, QVariantMap>> previos_;
    struct Muestra {
        qint64 t = 0;
        double ms = 0, acumulado = 0;
    };
    std::map<std::string, std::deque<Muestra>> historia_;
    QStringList linea_;
    int ventanaLineaS_ = 60;
    QVariantList serieUi_;
    qint64 finMs_ = 0;
    /// Rehace la serie de la línea de tiempo (tras cada vuelta, o al cambiar la línea).
    void armarSerie();
    QTimer reloj1s_;
    std::map<std::string, std::deque<Muestra>> larga_;
    std::deque<nlohmann::json> eventos_;
    mutable QVariantList eventosCache_; ///< eventos(), armada una vez por cambio
    mutable bool eventosSucio_ = true;
    std::uint64_t anotados_ = 0; ///< cuántos se anotaron (evento()): avisar aunque el tope saque uno
    void avisarEventos();
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
