// src/app/registro/RitmoTotem.cpp — ver RitmoTotem.hpp
#include "registro/RitmoTotem.hpp"

namespace ba::registro {

RitmoTotem::Decision RitmoTotem::revisar(const Mirada& m, std::int64_t ahora) {
    Decision d;
    // Atendida por una persona: ni silencios, ni foto, ni arranque solo (el guion está en pausa).
    if (m.atendida) return d;
    // Lo oído espera el fin del turno: un rato sin frases nuevas y sin voz sonando.
    if (m.hayTurno && !hablando) {
        const std::int64_t paso = ahora - turnoMs;
        if ((paso >= kFinTurnoMs && !m.oyendoVoz) || paso >= kTurnoTopeMs) d.paso = Paso::CerrarTurno;
        return d;
    }
    // Terminó de hablar: lo sintetizado ya salió por el canal.
    if (hablando) {
        if (m.vozSalio && finHablaMs == 0) finHablaMs = ahora;
        if (m.vozSalio && ahora - finHablaMs >= kMargenVozMs) {
            hablando = false;
            ultimaVozMs = ahora;
            d.paso = Paso::TerminoDeHablar;
            if (hayListo) {
                hayListo = false;
                terminoMs = ahora;
                d.mandarListo = true;
            }
            if (m.conversacion && m.enCurso) {
                // Pidió la foto: desde ahora se buscan tomas de frente (y se cuenta el tiempo).
                if (buscandoFoto) fotoDesdeMs = ahora;
                if (buscandoDocumento) docDesdeMs = ahora; // pidió el documento: desde ahora se busca
                d.estado = buscandoFoto         ? "foto"
                           : !buscandoDocumento ? "escuchando"
                           : m.buscandoDorso    ? "dorso" // la página guía: «la parte de atrás»
                                                : "documento";
            }
        }
        return d;
    }
    if (buscandoFoto && m.conversacion) {
        if (fotoDesdeMs == 0) fotoDesdeMs = ahora; // sin voz: no hubo nada que esperar
        if (ahora - fotoDesdeMs > kFotoMs) d.paso = Paso::FotoFallo;
        return d;
    }
    if (buscandoDocumento && m.conversacion) {
        if (docDesdeMs == 0) docDesdeMs = ahora; // sin voz: no hubo nada que esperar
        // Con una foto del dorso en el modelo, se espera su respuesta (hasta un tope).
        const std::int64_t paso = ahora - docDesdeMs;
        if ((paso > (m.buscandoDorso ? kDorsoMs : kDocumentoMs) && !leyendoDorso) || paso > kDorsoTopeMs)
            d.paso = Paso::DocumentoFallo;
        return d;
    }
    if (m.conversacion && m.enCurso) {
        if (ahora - ultimaVozMs > kSilencioMs) {
            ultimaVozMs = ahora;
            d.paso = Paso::Silencio;
        }
        return d;
    }
    // La pantalla de gracias ya volvió sola a esperar.
    if (m.pantallaListo && ahora - terminoMs > kListoMs) d.volverAEsperar = true;
    // Tomó canal y no aparece una cara: el canal ES el arranque (el que abre la página en el
    // celular ya decidió registrarse). Solo la primera vez por canal: en un kiosco el canal
    // queda y lo que arranca es una cara nueva.
    if (m.hayCanal && primeraDelCanal && !m.identificando && ahora - canalDesdeMs > kArranqueSinCaraMs)
        d.paso = Paso::Arrancar;
    return d;
}

} // namespace ba::registro
