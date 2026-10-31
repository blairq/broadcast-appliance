// ============================================================================
// src/app/ui/ScopeVista.hpp — un instrumento de video para una vista (tipo QML `Scope`)
//
// Cada panel tiene el suyo (su fuente, su tipo y su tamaño: no se pisan entre vistas).
// Mientras está `activo`, mira cinco veces por segundo si llegó un cuadro nuevo de la
// fuente (ImagenViewModel::cuadroScope) y, solo entonces, dibuja el scope y mide la
// legalidad en otro hilo; `dato` cambia cuando el dibujo está listo. Apagado, no hace nada.
//
//   Scope { imagen: Studio.imagen; fuente: ""; tipo: "onda"; ancho: w; alto: h; activo: visible }
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <cstdint>

namespace ba::app {

class ImagenViewModel;

class ScopeVista : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* imagen READ imagen WRITE setImagen NOTIFY configCambio)
    /// "" = el programa; si no, una entrada.
    Q_PROPERTY(QString fuente READ fuente WRITE setFuente NOTIFY configCambio)
    /// onda | parade | vector | histograma
    Q_PROPERTY(QString tipo READ tipo WRITE setTipo NOTIFY configCambio)
    Q_PROPERTY(int ancho READ ancho WRITE setAncho NOTIFY configCambio)
    Q_PROPERTY(int alto READ alto WRITE setAlto NOTIFY configCambio)
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY configCambio)
    /// {imagen: data URL PNG, legalidad: {negros_aplastados_pct, blancos_quemados_pct,
    /// fuera_de_gama_pct}} | {esperando: true} | {error}.
    Q_PROPERTY(QVariantMap dato READ dato NOTIFY datoCambio)

   public:
    explicit ScopeVista(QObject* parent = nullptr);

    QObject* imagen() const;
    void setImagen(QObject* o);
    QString fuente() const { return fuente_; }
    void setFuente(const QString& f);
    QString tipo() const { return tipo_; }
    void setTipo(const QString& t);
    int ancho() const { return ancho_; }
    void setAncho(int a);
    int alto() const { return alto_; }
    void setAlto(int a);
    bool activo() const { return activo_; }
    void setActivo(bool a);
    QVariantMap dato() const { return dato_; }

   signals:
    void configCambio();
    void datoCambio();

   private:
    /// Lo que se pidió cambió: se dibuja de nuevo con el próximo cuadro (aunque sea el mismo).
    void otraVez();
    void mirar();
    void poner(QVariantMap d);

    QPointer<ImagenViewModel> imagen_;
    QString fuente_, tipo_ = QStringLiteral("onda");
    int ancho_ = 640, alto_ = 360;
    bool activo_ = false;
    QVariantMap dato_;
    QTimer reloj_;
    std::uint64_t ultimaSerie_ = 0;
    bool hayUltima_ = false;
    bool dibujando_ = false;
    int pedido_ = 0; ///< sube con cada cambio de lo pedido: un dibujo viejo que vuelve se descarta
};

} // namespace ba::app
