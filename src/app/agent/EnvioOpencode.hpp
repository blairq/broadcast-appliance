// ============================================================================
// src/app/agent/EnvioOpencode.hpp — lo automático, a la conversación del evento
//
// Los avisos que el programa le manda solo al asistente (disparadores, notas del
// sistema, las novedades del evento) se ESCRIBÍAN en su terminal, como si los
// tipeara alguien. Con OpenCode eso tenía dos problemas: se mezclaba con lo que
// el operador estuviera escribiendo, y cada arranque empezaba en una sesión
// nueva (otra pestaña), así que el asistente perdía el hilo del evento.
//
// Ahora, con OpenCode, van por la API del servidor compartido (ServidorOpencode)
// a la sesión del evento (SesionesEvento), la misma que muestra la terminal:
//
//   * POST /api/session/<id>/prompt con `delivery: "queue"`: el servidor la pone
//     en la bandeja de la sesión y la entrega cuando termina lo que esté haciendo;
//     no corta una respuesta en curso ni toca lo que el operador está tipeando.
//   * Además, mientras la sesión está ocupada (GET /api/session/active) no se
//     manda nada: lo que llega se junta y sale en UN mensaje cuando se libera
//     (una ráfaga de avisos es un mensaje, no diez turnos del modelo).
//   * Los adjuntos (imágenes) van como archivos del mensaje (`files`, file://).
//
// Probado contra opencode 2.2.1 (serve): `queue` espera al turno en curso, en
// orden; una sesión borrada contesta 404 y se crea otra.
//
// El transporte HTTP se puede cambiar (setHttp) para probar sin red.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <functional>

class QNetworkAccessManager;

namespace ba::app::agent {

/// Lo que espera para salir, en orden. Se junta al salir: todo lo que haya (hasta
/// `max` avisos) va en un solo mensaje.
class ColaAutomatica {
   public:
    struct Item {
        QString texto;
        QStringList adjuntos;
    };
    struct Lote {
        QString texto;        ///< los textos, uno por párrafo
        QStringList adjuntos; ///< sin repetir
        std::deque<Item> items;
        bool vacio() const { return items.empty(); }
    };

    void agregar(const QString& texto, const QStringList& adjuntos = {});
    bool vacia() const { return items_.empty(); }
    int size() const { return static_cast<int>(items_.size()); }
    /// Saca los primeros `max` (todos si hay menos) y los junta.
    Lote tomar(int max = 20);
    /// Un lote que no se pudo mandar vuelve adelante, en su orden.
    void devolver(Lote lote);
    void vaciar() { items_.clear(); }

   private:
    std::deque<Item> items_;
};

class EnvioOpencode : public QObject {
    Q_OBJECT
   public:
    /// (método, ruta que empieza con /api, cuerpo JSON o vacío, respuesta(estado HTTP, cuerpo)).
    /// Estado 0: no hubo respuesta (el servidor no está).
    using Http = std::function<void(const QByteArray& metodo, const QString& ruta, const QByteArray& cuerpo,
                                    std::function<void(int estado, const QByteArray& respuesta)>)>;

    explicit EnvioOpencode(QObject* parent = nullptr);
    ~EnvioOpencode() override;

    /// El servidor compartido (url y clave de ServidorOpencode). Vacío: desconectado
    /// (lo pendiente se descarta: sin servidor no hay sesión a la que mandarlo).
    void conectar(const QString& url, const QString& clave);
    void desconectar();
    bool conectado() const { return static_cast<bool>(http_); }
    /// Para las pruebas: otro transporte (no toca la red).
    void setHttp(Http h) { http_ = std::move(h); }

    /// La sesión del evento: `guardada` si el servidor la tiene; si no (nunca hubo, o se
    /// borró), una nueva con ese título en `carpeta` (la del asistente: de ahí toma su
    /// configuración). listo(id, "") o listo("", error).
    void asegurarSesion(const QString& guardada, const QString& titulo, const QString& carpeta,
                        std::function<void(const QString& id, const QString& error)> listo);
    void setSesion(const QString& id);
    QString sesion() const { return sesion_; }

    /// Un mensaje automático para la sesión (ya con su marca: «[AVISO AUTOMATICO…]»).
    void encolar(const QString& texto, const QStringList& adjuntos = {});
    int pendientes() const { return cola_.size(); }
    /// Cada cuánto se vuelve a mirar si la sesión se liberó (ms).
    void setEsperaMs(int ms) { esperaMs_ = ms; }

    /// El cuerpo del POST …/prompt de un lote (público para las pruebas).
    static QByteArray cuerpoPrompt(const ColaAutomatica::Lote& lote);

   signals:
    /// Salió un mensaje (con cuántos avisos juntó).
    void enviado(const QString& texto, int avisos);
    /// No se pudo (se reintenta; tras varios, se descarta y se avisa).
    void fallo(const QString& error);

   private:
    void intentar();
    void reintentarMasTarde();
    void mandar(ColaAutomatica::Lote lote);

    Http http_;
    QNetworkAccessManager* red_ = nullptr;
    QString sesion_;
    ColaAutomatica cola_;
    QTimer reloj_;
    int esperaMs_ = 2000;
    bool enVuelo_ = false;
    int fallos_ = 0;
    quint64 generacion_ = 0; ///< sube al (des)conectar: las respuestas viejas no tocan nada
};

} // namespace ba::app::agent
