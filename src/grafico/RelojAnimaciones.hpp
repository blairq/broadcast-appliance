// ============================================================================
// src/grafico/RelojAnimaciones.hpp — el reloj de las animaciones, a mano
//
// POR QUÉ EXISTE. Qt anima contra el reloj de pared: una animación de 400 ms
// dura 400 ms de verdad. Para un gráfico eso está bien al aire, pero es
// inservible para todo lo demás que necesitamos:
//
//   * Vista previa reproducible. «Mostrame cómo se ve a los 380 ms» tiene que
//     dar SIEMPRE lo mismo, hoy y mañana, con la máquina descansada o cargada.
//     Si dependiera del reloj de pared, dos renders del mismo instante
//     saldrían distintos y la LLM estaría persiguiendo fantasmas.
//   * La barra de tiempo con arrastre del IDE. Arrastrar hacia atrás es
//     imposible contra un reloj que solo avanza.
//   * Dibujar al framerate del programa. Si el motor va a 30 fps, cada cuadro
//     tiene que adelantar exactamente 33,33 ms, no «lo que tardó».
//
// Con esto el tiempo lo decide quien renderiza: `avanzar(ms)` y dibujar.
// ============================================================================
#pragma once

#include <QAnimationDriver>
#include <QElapsedTimer>

namespace ba::grafico {

class RelojAnimaciones : public QAnimationDriver {
    Q_OBJECT

  public:
    explicit RelojAnimaciones(QObject* padre = nullptr) : QAnimationDriver(padre) {}

    /// Adelanta el tiempo `ms` y deja que Qt recalcule las animaciones.
    void avanzar(qint64 ms) {
        ahora_ += ms;
        advance();
    }

    /// Lleva el tiempo a un instante concreto. Hacia atrás NO se puede: las
    /// animaciones de Qt son máquinas de estado que solo avanzan. Para ir a un
    /// instante anterior hay que volver a cargar el gráfico y avanzar desde
    /// cero — que es justo lo que hace el renderizador con cada pedido, y por
    /// eso cada cuadro sale igual sin importar en qué orden se pidan.
    void irA(qint64 ms) {
        if (ms > ahora_) avanzar(ms - ahora_);
    }

    void reiniciar() { ahora_ = 0; }

    // --- QAnimationDriver -----------------------------------------------------
    qint64 elapsed() const override { return ahora_; }

  private:
    qint64 ahora_ = 0;
};

} // namespace ba::grafico
