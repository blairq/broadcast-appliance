// ============================================================================
// src/app/dispositivos/Drivers.hpp — los drivers de esta máquina, para la
// interfaz y el asistente
//
// Junta las tres piezas:
//   - hw::SaludDrivers: qué anduvo y qué está en cuarentena, por máquina
//     (<datos>/Config/drivers.json);
//   - Sondeo: la prueba en un proceso hijo, con tiempo límite;
//   - el rango de los proveedores de GStreamer en ESTE proceso: uno que queda
//     en cuarentena pasa a NONE (ningún monitor de dispositivos lo carga más) y
//     vuelve a su rango si «Probar de nuevo» anda.
//
// AL ARRANCAR (`arrancar`, en segundo plano, después de que la interfaz está):
//   - MÁQUINA NUEVA (su huella no tiene un sondeo completo): se lista todo y se
//     prueba todo, de a uno, con su progreso («revisando todo (3 de 9)»). La
//     primera búsqueda de dispositivos en el proceso espera a que termine.
//   - MÁQUINA CONOCIDA: lo que anduvo se usa directo (no se lo vuelve a probar);
//     lo que está en cuarentena ya arrancó con rango NONE (platform/GstEntorno);
//     se lista lo instalado (sin cargar nada) y solo lo NUEVO (o con otra
//     versión, o que venía fallando) pasa por la sonda. Lo que no está, ausente.
//
// Es un modelo de lista (QAbstractListModel) que se actualiza en su lugar: la
// sección «Drivers» de Dispositivos y equipos se enlaza a sus roles y a
// `resumen`; las herramientas drivers_listar y driver_probar usan lo mismo.
// ============================================================================
#pragma once

#include "dispositivos/Sondeo.hpp"
#include "hw/SaludDrivers.hpp"

#include <QAbstractListModel>
#include <QDateTime>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <vector>

namespace ba::app::dispositivos {

class Drivers : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString resumen READ resumen NOTIFY estadoCambio)
    Q_PROPERTY(bool revisando READ revisando NOTIFY estadoCambio)
    Q_PROPERTY(bool listo READ listo NOTIFY estadoCambio)
    Q_PROPERTY(bool maquinaConocida READ maquinaConocida NOTIFY estadoCambio)
    Q_PROPERTY(QString maquina READ maquina CONSTANT)
    Q_PROPERTY(int umbral READ umbral NOTIFY estadoCambio)
    Q_PROPERTY(int enCuarentena READ enCuarentena NOTIFY estadoCambio)
    Q_PROPERTY(int cuantos READ cuantos NOTIFY estadoCambio)
   public:
    enum Rol {
        IdRol = Qt::UserRole + 1,
        NombreRol,
        TipoRol,
        EstadoRol, ///< anda | falla | cuarentena | sin_probar | probando | ausente
        FallasRol,
        MotivoRol,
        DetalleRol,
        CuandoRol,
        TextoRol, ///< el estado en una línea, para mostrar
    };

    struct Opciones {
        std::filesystem::path archivo; ///< vacío: en memoria
        std::string huella;
        std::string etiqueta; ///< el modelo de la máquina
        Sondeo::Opciones sondeo;
        int umbral = 4;
        /// Para los proveedores de GStreamer en este proceso (nombre sin «gst:», rango).
        std::function<void(const std::string& proveedor, int rango)> ponerRango;
    };
    explicit Drivers(Opciones o, QObject* parent = nullptr);

    /// Lo del arranque (máquina nueva: todo; conocida: novedades). En segundo plano.
    void arrancar();
    /// «Probar de nuevo»: lo saca de la cuarentena para UNA prueba, ya. "" o el error.
    Q_INVOKABLE QString reintentar(const QString& id);
    /// Vuelve a probar TODO lo instalado (lo que anda también; la cuarentena no).
    Q_INVOKABLE void revisarTodo();
    /// La búsqueda en el proceso se trabó: se prueban aparte los proveedores de
    /// GStreamer que se venían usando, para saber cuál fue.
    void sospechar(const QString& porque);

    bool sePuedeCargar(const QString& id) const { return salud_.sePuedeCargar(id.toStdString()); }
    hw::SaludDrivers& salud() { return salud_; }
    /// Todos, como los ve el asistente.
    QVariantList lista() const;
    QVariantMap uno(const QString& id) const;

    QString resumen() const;
    bool revisando() const { return revisando_; }
    bool listo() const { return listo_; }
    bool maquinaConocida() const { return conocidaAlArrancar_; }
    QString maquina() const;
    int umbral() const { return salud_.umbral(); }
    int enCuarentena() const { return salud_.cuantosEnCuarentena(); }
    int cuantos() const { return static_cast<int>(filas_.size()); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

   signals:
    void estadoCambio();
    /// Terminó lo del arranque (la primera búsqueda de dispositivos lo espera).
    void listoArranque();
    /// Uno acaba de quedar en cuarentena.
    void cuarentenaNueva(const QString& id, const QString& nombre, const QString& motivo);

   private:
    void listarY(std::function<void(const std::vector<hw::DriverListado>&)> despues);
    void probarCola();
    void alResultado(const ResultadoSonda& r);
    void terminoRonda();
    void refrescar();
    QVariantMap fila(const hw::EstadoDriver& e) const;
    static QString ahora() { return QDateTime::currentDateTime().toString(Qt::ISODate); }

    Opciones o_;
    hw::SaludDrivers salud_;
    Sondeo sondeo_;
    QStringList cola_;
    QSet<QString> probando_;
    bool revisando_ = false, listo_ = false, conocidaAlArrancar_ = false, completo_ = false;
    int hechos_ = 0, total_ = 0;
    int listados_ = 0;
    std::vector<hw::EstadoDriver> filas_;
    std::vector<QVariantMap> vistas_; ///< lo que ve la interfaz de cada fila (para avisar solo lo que cambió)
};

} // namespace ba::app::dispositivos
