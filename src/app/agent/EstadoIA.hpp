// ============================================================================
// src/app/agent/EstadoIA.hpp — qué parte de la asistencia con IA no anda o falta configurar
//
// Junta en una lista lo que hoy está repartido: el asistente (instalado, clave,
// corriendo, MCP), el modelo local, el dictado y la voz del operador (reconocedor,
// micrófono), la voz que contesta, los auriculares, la voz del teleprompter y las
// traducciones. Cada problema dice qué pasa, por qué y ADÓNDE ir a arreglarlo
// (página y sección): lo muestra la pastilla «IA» del encabezado y lo lee el
// asistente (ia_estado), así alguien que no conoce el sistema sabe por dónde empezar.
//
// Se recalcula cuando cualquiera de esas partes avisa un cambio (sin sondear).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

namespace ba::app::agent {

class AgentViewModel;
class ModelosLocales;
class Voz;
class VozPrompter;
class Traductor;

class EstadoIA : public QObject {
    Q_OBJECT
    /// [{id, titulo, detalle, gravedad: "error"|"aviso", pagina, seccion}]
    Q_PROPERTY(QVariantList problemas READ problemas NOTIFY cambio)
    Q_PROPERTY(int errores READ errores NOTIFY cambio)
    Q_PROPERTY(int avisos READ avisos NOTIFY cambio)

   public:
    /// Cualquiera puede faltar (una compilación sin voz, sin traductor).
    EstadoIA(AgentViewModel* agente, ModelosLocales* modelos, Voz* voz, VozPrompter* vozPrompter,
             Traductor* traductor, QObject* parent = nullptr);

    QVariantList problemas() const { return problemas_; }
    int errores() const { return errores_; }
    int avisos() const { return avisos_; }
    /// Lo mismo para el MCP: {todo_bien, problemas: [...]}.
    nlohmann::json estado() const;

   signals:
    void cambio();

   private:
    void recalcular();
    AgentViewModel* agente_;
    ModelosLocales* modelos_;
    Voz* voz_;
    VozPrompter* vozPrompter_;
    Traductor* traductor_;
    QTimer pronto_; ///< varios avisos seguidos: un solo recálculo
    QVariantList problemas_;
    int errores_ = 0;
    int avisos_ = 0;
};

} // namespace ba::app::agent
