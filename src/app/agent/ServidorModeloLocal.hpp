// ============================================================================
// src/app/agent/ServidorModeloLocal.hpp — UN llama-server para todos los chats
//
// El chat del modelo local (pi) le habla a un llama-server del programa. Con dos
// asistentes (la pestaña del evento y la de automatización) cada uno levantaba el
// suyo: el mismo modelo cargado dos veces en la GPU. Ahora hay uno solo para el
// proceso: el primero que lo pide lo arranca, los demás —con el mismo modelo—
// usan el mismo puerto, y cuando el último lo suelta se apaga. Pedir otro modelo
// lo reinicia (para todos: hay un solo modelo cargado).
//
// LA VISTA. Si está instalado el proyector de visión del modelo (el «mmproj» del
// catálogo de modelos locales, tipo `vision`), se carga con --mmproj: el mismo
// servidor contesta pedidos con imágenes (el dorso de un documento en el tótem,
// ui/LectorDocumentos), y la imagen no sale de 127.0.0.1. Quién sabe qué mmproj va
// con cada .gguf es ModelosLocales (mmprojPara): lo engancha StudioViewModel con
// setMmprojPara, así todos los que piden el servidor lo cargan igual.
//
// EL CALOR (F4-05, la política térmica «priorizar programa»). Con `porCalor(true)`,
// apenas no está contestando nada (pregunta /slots cada pocos segundos) se apaga y
// queda DORMIDO: los que lo usan siguen anotados y su puerto, reservado. Un pedido
// mientras duerme lo despierta (en el mismo puerto); al aliviarse la máquina vuelve
// solo si alguien lo sigue usando.
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
        /// El proyector de visión; vacío = el que diga setMmprojPara (si hay).
        QString mmproj;
        /// Si ya anda con OTRO modelo, no cambiarlo (para los demás): `listo(0, error)`.
        /// Para quien solo quiere mirar algo y no puede dejar sin chat al asistente.
        bool noReiniciar = false;
    };
    /// El llama-server del programa (en la carpeta de binarios).
    static QString ejecutable();
    /// Qué mmproj va con un .gguf ("" = ninguno instalado). Lo pone StudioViewModel.
    void setMmprojPara(std::function<QString(const QString& gguf)> f) { mmprojPara_ = std::move(f); }
    /// El que se cargaría con ese .gguf (el de setMmprojPara, si existe el archivo).
    QString mmprojPara(const QString& gguf) const;
    /// Anda y cargó la visión (--mmproj).
    bool conVision() const { return proc_ != nullptr && !mmproj_.isEmpty(); }
    /// `quien` lo usa. `listo(puerto, "")` cuando contesta /health (enseguida si ya
    /// estaba cargado con ese modelo); `listo(0, error)` si no arrancó. `estado`: los
    /// avisos de mientras («Cargando el modelo local…»).
    void pedir(QObject* quien, const Pedido& p, std::function<void(quint16, QString)> listo,
               std::function<void(QString)> estado = {});
    /// `quien` deja de usarlo; sin nadie, se apaga.
    void soltar(QObject* quien);
    quint16 puerto() const { return puerto_; }
    /// La máquina está caliente (o se alivió): dormirlo apenas no atienda nada, o despertarlo.
    void porCalor(bool on);
    bool dormido() const { return dormido_; }

   private:
    ServidorModeloLocal() = default;
    void arrancar(const Pedido& p);
    void parar();
    /// Con calor: si no está contestando nada (/slots), lo duerme.
    void dormirSiOcioso();
    void avisarListo(quint16 puerto, const QString& error);

    QProcess* proc_ = nullptr;
    QTimer* reloj_ = nullptr;
    quint16 puerto_ = 0;
    QString gguf_;
    QString mmproj_;
    std::function<QString(const QString&)> mmprojPara_;
    bool listo_ = false;
    Pedido ultimo_;              ///< con qué se arrancó (para despertarlo igual)
    bool calor_ = false;         ///< porCalor
    bool dormido_ = false;       ///< se apagó por calor con gente que lo usa (puerto_ reservado)
    QTimer* vigiaCalor_ = nullptr;
    std::map<QObject*, std::pair<std::function<void(quint16, QString)>, std::function<void(QString)>>> esperan_;
    std::map<QObject*, bool> usan_;
};

} // namespace ba::app::agent
