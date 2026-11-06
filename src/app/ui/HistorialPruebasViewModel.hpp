// ============================================================================
// src/app/ui/HistorialPruebasViewModel.hpp — el historial de pruebas (Pruebas → Historial)
//
// Todas las pruebas de esta máquina (sala, capacidad, recibidas, conexión, asamblea), con
// sus números, sus fichas y sus series (sala/HistorialPruebas). La vista lee `lista`,
// `detalle` y `comparacion`; abrir, comparar y exportar leen el disco en el pool y vuelven
// acá. Se recarga sola cuando termina una prueba (alGuardar) y al pedirlo.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>

namespace ba::app {

class HistorialPruebasViewModel : public QObject {
    Q_OBJECT
    /// [{archivo, fecha, tipo, maquina, remota, frase, conMargen, maximo, limita, participantes,
    /// decodificar, version}] lo más nuevo primero.
    Q_PROPERTY(QVariantList lista READ lista NOTIFY listaChanged)
    /// La prueba abierta, entera (sin lo privado); vacío si ninguna.
    Q_PROPERTY(QVariantMap detalle READ detalle NOTIFY detalleChanged)
    /// [{clave, a, b}] de las dos comparadas; vacío si no se comparó.
    Q_PROPERTY(QVariantList comparacion READ comparacion NOTIFY comparacionChanged)
    Q_PROPERTY(bool cargando READ cargando NOTIFY listaChanged)

   public:
    explicit HistorialPruebasViewModel(std::filesystem::path datos, QObject* parent = nullptr);

    QVariantList lista() const { return lista_; }
    QVariantMap detalle() const { return detalle_; }
    QVariantList comparacion() const { return comparacion_; }
    bool cargando() const { return cargando_; }

    Q_INVOKABLE void recargar();
    Q_INVOKABLE void abrir(const QString& archivo);
    Q_INVOKABLE void cerrar();
    Q_INVOKABLE void comparar(const QString& a, const QString& b);
    /// `destino` (ruta o file://): .csv = la lista; otra cosa (.json) = la abierta o, sin
    /// abierta, la lista entera. "" o el error.
    Q_INVOKABLE QString exportar(const QString& destino);
    /// Una prueba terminó y guardó su informe (lo escribe la cola): se recarga en un momento.
    void alGuardar();

   signals:
    void listaChanged();
    void detalleChanged();
    void comparacionChanged();

   private:
    std::filesystem::path datos_;
    QVariantList lista_, comparacion_;
    QVariantMap detalle_;
    bool cargando_ = false;
    int ronda_ = 0;
};

} // namespace ba::app
