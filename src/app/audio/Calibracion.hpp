// ============================================================================
// src/app/audio/Calibracion.hpp — la calibración acústica de la sala (el modelo)
//
// Ver docs/planes/PLAN-CALIBRACION-SALA.md. Una SESIÓN es una sala (medidas), sus
// parlantes (a qué salida y canal van, dónde están), los micrófonos con que se
// mide (a qué entrada van y su calibración: plano, curva del modelo, archivo,
// sustitución), los puntos (zona de escucha, cerca de un parlante, una pared…)
// y las mediciones. Se guarda en Mediciones/Calibraciones/<id>/sesion.json, con
// la respuesta impulsiva de cada medición en un .wav al lado.
//
// Medir: el barrido exponencial del Profiler de LSP (ba-lspdsp) por la salida
// del parlante mientras se graba el micrófono (LazoMedicion); de la captura salen
// la respuesta impulsiva, la llegada del directo, la relación señal/ruido, el RT
// por banda, la respuesta en frecuencia y la curva energía-tiempo. Con lo medido:
// analizar (promedio espacial, modos, fs), proponer (filtros con su motivo),
// aplicar (un EQ paramétrico de LSP en el rack que alimenta la sala), verificar
// (medir de nuevo: antes contra después) y revertir.
//
// Nada suena si la emisión o la grabación están en curso. Todo lo usa igual la
// ventana del Mezclador (pestaña Calibración) y el MCP (calibracion_*).
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct ba_medicion;

namespace ba::app::audio {

class LazoMedicion;
class RackManager;
namespace lspdsp {
struct Api;
}

class Calibracion : public QObject {
    Q_OBJECT
    /// La sesión abierta (vacía si no hay): {id, nombre, sala, objetivo, parlantes,
    /// microfonos, puntos, mediciones (sin las curvas), aplicado}.
    Q_PROPERTY(QVariantMap sesion READ sesion NOTIFY sesionCambio)
    /// Las sesiones guardadas: [{id, nombre, fecha, mediciones}].
    Q_PROPERTY(QVariantList sesiones READ sesiones NOTIFY sesionCambio)
    /// "", "midiendo" o "analizando".
    Q_PROPERTY(QString estado READ estado NOTIFY estadoCambio)
    /// Qué está pasando o qué pasó (el último error también).
    Q_PROPERTY(QString mensaje READ mensaje NOTIFY estadoCambio)
    /// El análisis, la propuesta y la verificación (se rehacen con cada medición).
    Q_PROPERTY(QVariantMap analisis READ analisis NOTIFY analisisCambio)
    Q_PROPERTY(QVariantMap propuesta READ propuesta NOTIFY analisisCambio)
    Q_PROPERTY(QVariantMap verificacion READ verificacion NOTIFY analisisCambio)
    /// La última medición, entera (con la IR y la curva energía-tiempo para dibujar).
    Q_PROPERTY(QVariantMap ultima READ ultima NOTIFY ultimaCambio)
    /// Los dispositivos: [{id, nombre, canales}] (con la sala simulada en --test).
    Q_PROPERTY(QVariantList salidas READ salidas NOTIFY dispositivosCambio)
    Q_PROPERTY(QVariantList entradas READ entradas NOTIFY dispositivosCambio)
    /// Las frecuencias de las curvas (240 puntos, 1/24 de octava).
    Q_PROPERTY(QVariantList grilla READ grilla CONSTANT)
    /// Los micrófonos con curva conocida: [{id, nombre, nota}].
    Q_PROPERTY(QVariantList modelosMicrofono READ modelosMicrofono CONSTANT)
    /// Si ba-lspdsp está (sin ella no se puede medir).
    Q_PROPERTY(bool disponible READ disponible CONSTANT)

   public:
    using json = nlohmann::json;
    Calibracion(std::filesystem::path dataRoot, bool conSimulada, QObject* parent = nullptr);
    ~Calibracion() override;

    QVariantMap sesion() const;
    QVariantList sesiones() const;
    QString estado() const { return estado_; }
    QString mensaje() const { return mensaje_; }
    QVariantMap analisis() const;
    QVariantMap propuesta() const;
    QVariantMap verificacion() const;
    QVariantMap ultima() const;
    QVariantList salidas() const { return salidas_; }
    QVariantList entradas() const { return entradas_; }
    QVariantList grilla() const;
    QVariantList modelosMicrofono() const;
    bool disponible() const;

    void setRacks(RackManager* r) { racks_ = r; }
    /// Si algo está al aire o grabando (no se mide).
    void setAlAire(std::function<bool()> f) { alAire_ = std::move(f); }

    // Todas devuelven "" si anduvo o el motivo; las que crean algo, el id en `id`.
    Q_INVOKABLE QString nueva(const QString& nombre, double largo, double ancho, double alto);
    Q_INVOKABLE QString abrir(const QString& id);
    Q_INVOKABLE QString setSala(double largo, double ancho, double alto);
    /// La curva objetivo: pendiente en dB/octava (0 = plana; -0,8 de fábrica).
    Q_INVOKABLE QString setObjetivo(double pendiente);
    Q_INVOKABLE QString agregarParlante(const QString& nombre, const QString& dispositivo, int canal, double x,
                                        double y);
    /// tipo: plano, modelo (con `modelo`: sm58, sm57…), archivo (con `archivo`).
    Q_INVOKABLE QString agregarMicrofono(const QString& nombre, const QString& dispositivo, int canal,
                                         const QString& tipo, const QString& modelo, const QString& archivo);
    Q_INVOKABLE QString setCalibracionMicrofono(const QString& id, const QString& tipo, const QString& modelo,
                                                const QString& archivo);
    /// tipo: escucha (la zona de público), cerca (a ~1 m de un parlante), pared, microfono (donde está uno de la mesa).
    Q_INVOKABLE QString agregarPunto(const QString& nombre, const QString& tipo, double x, double y);
    /// Mover un parlante, micrófono o punto en el croquis (metros).
    Q_INVOKABLE QString mover(const QString& id, double x, double y);
    Q_INVOKABLE QString quitar(const QString& id);
    /// Mide `parlante` con `microfono` puesto en `punto`. etapa: antes, verificacion
    /// o sustitucion. Asíncrona: termina con medicionTerminada.
    Q_INVOKABLE QString medir(const QString& parlante, const QString& punto, const QString& microfono,
                              const QString& etapa, double duracionS, double nivelDb);
    Q_INVOKABLE void cancelar();
    /// La corrección de `microfono` por sustitución: la misma posición medida con
    /// un micrófono de referencia (medicion `conReferencia`) y con él (`conMicrofono`).
    Q_INVOKABLE QString calibrarPorSustitucion(const QString& microfono, const QString& conReferencia,
                                               const QString& conMicrofono);
    /// Pone la propuesta de `parlante` en un EQ paramétrico de LSP («eq-sala») en el
    /// rack de `fuente` (la que alimenta ese parlante).
    Q_INVOKABLE QString aplicar(const QString& parlante, const QString& fuente);
    /// Saca el EQ de sala del rack de `fuente`.
    Q_INVOKABLE QString revertir(const QString& fuente);
    Q_INVOKABLE QString borrarMedicion(const QString& id);
    Q_INVOKABLE void refrescarDispositivos();
    /// Una medición entera (para dibujarla).
    Q_INVOKABLE QVariantMap medicion(const QString& id) const;
    /// Lo que se dibuja de un parlante: {puntos: [{punto, nombre, curva}], promedio,
    /// objetivo, previsto, despues, filtros, modos}.
    Q_INVOKABLE QVariantMap curvas(const QString& parlante) const;

    // Para el MCP.
    json sesionJson(bool conCurvas) const;
    json analisisJson() const { return analisis_; }
    json propuestaJson() const { return propuesta_; }
    json verificacionJson() const { return verificacion_; }
    json medicionJson(const std::string& id) const;
    std::string ultimoId() const { return ultimoId_; }

    /// Lo que hace una medición con lo capturado (sin Qt: lo usan las pruebas).
    /// Devuelve la medición (o {error}); en `ir` la respuesta impulsiva (desde 2 ms antes del directo).
    static json procesarCaptura(const lspdsp::Api* L, ba_medicion* m, int frecuencia, const std::vector<float>& captura,
                                std::vector<float>* ir);

   signals:
    void sesionCambio();
    void estadoCambio();
    void analisisCambio();
    void ultimaCambio();
    void dispositivosCambio();
    /// Terminó una medición: su id, o el error.
    void medicionTerminada(const QString& id, const QString& error);

   private:
    json* buscar(const std::string& id);
    const json* buscar(const std::string& id) const;
    std::string idNuevo(const std::string& prefijo) const;
    std::filesystem::path carpeta() const;
    void guardar();
    void rehacerAnalisis();
    void setEstado(const QString& e, const QString& m);
    void terminarMedicion(json med, std::vector<float> ir, QString error);

    std::filesystem::path root_;
    bool conSimulada_ = false;
    json s_; ///< la sesión abierta (null si no hay)
    json analisis_, propuesta_, verificacion_;
    json ultima_;
    std::string ultimoId_;
    QString estado_, mensaje_;
    QVariantList salidas_, entradas_;
    RackManager* racks_ = nullptr;
    std::function<bool()> alAire_;
    std::unique_ptr<LazoMedicion> lazo_;
    ba_medicion* med_ = nullptr;
    std::thread hilo_;
    int generacion_ = 0; ///< para descartar lo que llega de una medición cancelada
};

} // namespace ba::app::audio
