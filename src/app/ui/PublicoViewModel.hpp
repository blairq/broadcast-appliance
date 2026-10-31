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
// SE CONSULTA SOLO MIENTRAS ALGUIEN MIRA. Cada vista lo usa (`usar(vista, si)`, en
// QML `Suscripcion`) al mostrarse y lo suelta al irse: si no, el aparato le estaría
// preguntando al gateway cada dos segundos para siempre, incluso con la pestaña
// cerrada. La cuenta es por vista: las Preguntas y la Votación (y la web del evento)
// lo miran a la vez y cerrar una no le apaga la consulta a la otra.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <nlohmann/json.hpp>

#include "comun/UsoPorVistas.hpp"

namespace ba::app::sala {
class VotacionSala;
class PreguntasSala;
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
    /// Si se está consultando al gateway: alguna vista lo usa (usar).
    Q_PROPERTY(bool activo READ activo NOTIFY cambio)
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
    bool activo() const { return mirando_.alguien(); }
    /// Una vista (o la web del evento) empieza o deja de mirar: se consulta mientras haya alguna.
    Q_INVOKABLE void usar(QObject* vista, bool si) { mirando_.usar(vista, si); }
    bool enLinea() const;
    /// Para la página web del evento, SOLO las moderadas (la que se responde, las
    /// elegidas, las respondidas, hasta 40); nunca las nuevas sin revisar ni las
    /// descartadas: [{texto, nombre, votos, estado}].
    nlohmann::json preguntasParaLaWeb() const;
    /// Para la página web del evento, la votación (null si no hubo): la pregunta y si
    /// está abierta; el resultado solo cuando ya se puede mostrar (cerrada, o el
    /// operador eligió mostrarlo mientras está abierta).
    nlohmann::json votacionParaLaWeb() const;

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
    /// La cola de preguntas de la sala (modelo).
    void setPreguntas(sala::PreguntasSala* p) { preguntasSala_ = p; }
    Q_INVOKABLE void cerrarVotacion();
    Q_INVOKABLE void mostrarEnVivo(bool si);
    /// Pide el enlace de la página del público (llega por `enlaceCambio`).
    Q_INVOKABLE void pedirEnlace(bool remoto = false);
    /// Cambia cómo se ve la página del público: {titulo?, cuando?, fondo?}
    /// (fondo = ruta de una imagen; "" lo saca). Las reglas (2 MB…) son del
    /// modelo (sala::PaginaPublico), las mismas que usa el MCP (publico_pagina).
    Q_INVOKABLE void cambiarPagina(const QVariantMap& cambios);

   signals:
    void cambio();
    void enlaceCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    void alVolverVotacion(const std::string& error, const nlohmann::json& respuesta, const QString& queFallo);
    void tomarPreguntas(const nlohmann::json& respuesta);
    void consultar();

    ConferenceViewModel* sala_ = nullptr;
    sala::VotacionSala* votacionSala_ = nullptr;
    sala::PreguntasSala* preguntasSala_ = nullptr;
    QTimer reloj_;
    QVariantList preguntas_;
    QVariantMap recuento_;
    QVariantMap votacion_;
    QVariantMap enlace_;
    bool enLinea_ = false;
    /// Quiénes lo están mirando (por vista, no un booleano: ver arriba).
    UsoPorVistas mirando_{this, [this](const QString&) { alCambiarMirada(); }};
    void alCambiarMirada();
};

} // namespace ba::app
