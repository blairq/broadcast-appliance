// src/app/agent/Subtitulos.hpp — las frases de las salidas de texto a los cuadros «Subtítulos»
//
// El cuadro de escena «Subtítulos» (obs-module/ba_subtitulos.cpp) elige qué
// salida de texto muestra (la transcripción de una «Voz a texto» o una
// traducción). Esto le hace llegar cada frase de esa salida (proc `agregar`):
// el cuadro la parte en líneas y maneja los tiempos. También le da al cuadro la
// lista de salidas que hay, para su desplegable (proc global `ba_flujos_texto`).
#pragma once

#include <QObject>
#include <QString>

namespace ba::app::agent {

class FlujosTexto;

class Subtitulos : public QObject {
    Q_OBJECT
   public:
    explicit Subtitulos(FlujosTexto& flujos, QObject* parent = nullptr);
    ~Subtitulos() override;

    /// Cuántos cuadros «Subtítulos» muestran `flujo` (para avisar si ninguno).
    int cuadrosDe(const QString& flujo) const;

   private:
    void alFrase(const QString& flujo, const QString& texto);
    FlujosTexto& flujos_;
};

} // namespace ba::app::agent
