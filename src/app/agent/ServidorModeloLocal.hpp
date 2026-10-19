// ============================================================================
// src/app/agent/ServidorModeloLocal.hpp — UN llama-server para todos los chats
//
// El chat del modelo local (pi) le habla a un llama-server del programa. Con dos
// asistentes (la pestaña del evento y la de automatización) cada uno levantaba el
// suyo: el mismo modelo cargado dos veces en la GPU. Ahora hay uno solo para el
// proceso: el primero que lo pide lo arranca, los demás —con el mismo modelo—
// usan el mismo puerto, y cuando el último lo suelta se apaga. Pedir otro modelo
// lo reinicia (para todos: hay un solo modelo cargado).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <map>

class QProcess;
class QTimer;

namespace ba::app::agent {

class ServidorModeloLocal : public QObject {
    Q_OBJECT
   public:
    /// El del proceso.
    static ServidorModeloLocal& el();

    struct Pedido {
        QString ejecutable; ///< llama-server
        QString gguf;
        int capasGpu = 999;
        int contexto = 32768;
        QString logs;       ///< carpeta de Logs
        int cargaS = 180;   ///< cuánto esperar a que cargue
    };
    /// `quien` lo usa. `listo(puerto, "")` cuando contesta /health (enseguida si ya
    /// estaba cargado con ese modelo); `listo(0, error)` si no arrancó. `estado`: los
    /// avisos de mientras («Cargando el modelo local…»).
    void pedir(QObject* quien, const Pedido& p, std::function<void(quint16, QString)> listo,
               std::function<void(QString)> estado = {});
    /// `quien` deja de usarlo; sin nadie, se apaga.
    void soltar(QObject* quien);
    quint16 puerto() const { return puerto_; }

   private:
    ServidorModeloLocal() = default;
    void arrancar(const Pedido& p);
    void parar();
    void avisarListo(quint16 puerto, const QString& error);

    QProcess* proc_ = nullptr;
    QTimer* reloj_ = nullptr;
    quint16 puerto_ = 0;
    QString gguf_;
    bool listo_ = false;
    std::map<QObject*, std::pair<std::function<void(quint16, QString)>, std::function<void(QString)>>> esperan_;
    std::map<QObject*, bool> usan_;
};

} // namespace ba::app::agent
