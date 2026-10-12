// ============================================================================
// src/app/ui/PublicoViewModel.hpp — las preguntas del público y la urna
//
// Las dos cosas viven en B (el gateway), porque llegan de teléfonos y B es lo
// que los teléfonos alcanzan. Esto es el lado de A: trae el estado y manda las
// órdenes de moderación.
//
// POR QUE UN VIEW MODEL Y NO SOLO HERRAMIENTAS MCP. Hasta acá la cola y la urna
// solo se podían tocar escribiéndole al asistente. Abrir y cerrar una votación
// es algo que se hace EN VIVO, con la sala mirando: depender de escribirle a un
// modelo para cerrar una urna es exactamente el momento en que no se quiere
// depender de eso. Con el modelo acá, las manos y el asistente hacen lo mismo
// sobre el mismo estado.
//
// SE CONSULTA SOLO MIENTRAS ALGUIEN MIRA. La página prende `activo` al
// mostrarse y lo apaga al irse: si no, el aparato le estaría preguntando al
// gateway cada dos segundos para siempre, incluso con la pestaña cerrada.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <nlohmann/json.hpp>

namespace ba::app::sala {
class VotacionSala;
}

namespace ba::app {

class ConferenceViewModel;

class PublicoViewModel : public QObject {
    Q_OBJECT
    /// La cola completa, en el orden en que la mira quien modera: primero la
    /// que está al aire, después la cola, después las nuevas por votos.
    Q_PROPERTY(QVariantList preguntas READ preguntas NOTIFY cambio)
    /// {nuevas, elegidas, al_aire, respondidas, descartadas, total}
    Q_PROPERTY(QVariantMap recuento READ recuento NOTIFY cambio)
    /// La votación: {hubo, abierta, modo, secreta, enVivo, pregunta, votaron,
    /// podian, resultado, quienes, faltanVotar}. `hubo:false` = nunca se abrió.
    Q_PROPERTY(QVariantMap votacion READ votacion NOTIFY cambio)
    /// El enlace de la página del público y su QR.
    Q_PROPERTY(QVariantMap enlace READ enlace NOTIFY enlaceCambio)
    /// Si se está consultando al gateway. Lo prende la vista al mostrarse.
    Q_PROPERTY(bool activo READ activo WRITE setActivo NOTIFY cambio)
    /// false cuando el gateway no contesta: la vista tiene que poder decir
    /// «no sé» en vez de mostrar una cola vacía, que significa otra cosa.
    Q_PROPERTY(bool enLinea READ enLinea NOTIFY cambio)

   public:
    explicit PublicoViewModel(ConferenceViewModel* sala, QObject* parent = nullptr);
    ~PublicoViewModel() override;

    QVariantList preguntas() const;
    QVariantMap recuento() const;
    QVariantMap votacion() const;
    QVariantMap enlace() const;
    bool activo() const;
    void setActivo(bool a);
    bool enLinea() const;

    /// Mueve una pregunta por la cola: elegir, des_elegir, al_aire, responder,
    /// descartar, limpiar o vaciar. Las dos últimas no llevan id.
    Q_INVOKABLE void moderar(const QString& accion, const QString& id = {});
    /// Mete en la cola una que llegó por otro lado (el chat, una plataforma,
    /// o dicha a viva voz).
    Q_INVOKABLE void sumar(const QString& texto, const QString& nombre, const QString& deDonde);

    /// Abre una votación (las reglas y quién vota son del modelo: sala::VotacionSala).
    Q_INVOKABLE void abrirVotacion(const QString& pregunta, const QStringList& opciones, const QString& modo,
                                   bool secreta, bool enVivo);
    /// La votación de la sala (modelo); la pone quien arma los view models.
    void setVotacion(sala::VotacionSala* v) { votacionSala_ = v; }
    Q_INVOKABLE void cerrarVotacion();
    Q_INVOKABLE void mostrarEnVivo(bool si);
    /// Pide el enlace de la página del público (llega por `enlaceCambio`).
    Q_INVOKABLE void pedirEnlace(bool remoto = false);

   signals:
    void cambio();
    void enlaceCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    void alVolverVotacion(const std::string& error, const nlohmann::json& respuesta, const QString& queFallo);
    void consultar();

    ConferenceViewModel* sala_ = nullptr;
    sala::VotacionSala* votacionSala_ = nullptr;
    QTimer reloj_;
    QVariantList preguntas_;
    QVariantMap recuento_;
    QVariantMap votacion_;
    QVariantMap enlace_;
    bool enLinea_ = false;
    /// Cuántas vistas lo están mirando. Es un contador y no un booleano
    /// porque la misma cosa se muestra en dos lados —la página del Público y
    /// la pestaña del Evento— y que una se cierre no puede apagarle la
    /// consulta a la otra.
    int mirando_ = 0;
};

} // namespace ba::app
