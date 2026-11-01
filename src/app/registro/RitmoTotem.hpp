// ============================================================================
// src/app/registro/RitmoTotem.hpp — el ritmo de la conversación de un tótem (modelo)
//
// La conversación (registro/Conversacion) decide QUÉ decir; esto decide CUÁNDO: el fin de
// un turno de lo que se oye, cuándo terminó de hablar el asistente (y vuelve a escuchar),
// cuánto se espera la foto de frente o el documento antes de decir qué corregir, el
// silencio largo («¿seguís ahí?»), la pantalla de gracias que vuelve sola a esperar y el
// arranque con canal y sin cara. Una conexión, un RitmoTotem.
//
// Sin Qt, sin reloj y sin audio: registro/ConexionesTotem le avisa lo que pasa (los campos de
// abajo, que son el estado de la conexión) y cada tanto (su tic) le pregunta `revisar` con
// la hora y lo que mira de afuera (`Mirada`: si el VAD oye voz, si la voz ya salió por el
// canal…). Lo que vuelve (`Decision`) lo hace la conexión: cerrar el turno, pedirle a la
// conversación el fallo de la foto, mandar `listo` a la página. Con test
// (test_registro_ritmo), con la hora como argumento.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace ba::registro {

class RitmoTotem {
   public:
    // Los tiempos de la conversación. Se dicen en voz alta frente a una pantalla:
    // cortos para que no parezca colgado, largos para no apurar a nadie.
    static constexpr std::int64_t kSilencioMs = 12000; ///< sin decir nada: «¿seguís ahí?»
    static constexpr std::int64_t kMargenVozMs = 350;  ///< después de hablar, antes de volver a escuchar
    /// Fin del turno: tanto sin voz después de la última frase. El reconocedor cierra cada frase
    /// en una pausa corta (la de los subtítulos); quien dicta un correo hace pausas más largas, y
    /// una respuesta larga no se contesta por pedazos.
    static constexpr std::int64_t kFinTurnoMs = 700;
    static constexpr std::int64_t kTurnoTopeMs = 8000; ///< con el VAD oyendo «voz» sin parar (ruido): se manda igual
    static constexpr std::int64_t kFotoMs = 7000; ///< cuánto se espera una toma de frente antes de pedirla de nuevo
    static constexpr std::int64_t kArranqueSinCaraMs = 2500; ///< con canal y sin cara a la vista: arranca igual
    static constexpr std::int64_t kListoMs = 8000; ///< la pantalla de gracias (la página vuelve sola)
    /// Cuánto se espera leer el documento antes de decirle qué corregir (o pedir el dorso: con
    /// una cámara que enfoca, el frente sale en un par de segundos).
    static constexpr std::int64_t kDocumentoMs = 9000;
    /// Lo mismo con el dorso: cada foto tarda lo que tarde el modelo propio en leerla (uno a
    /// pocos segundos), así que se le da más tiempo para varios intentos.
    static constexpr std::int64_t kDorsoMs = 25000;
    /// Pasado esto, el paso del dorso termina aunque haya una foto en el modelo (el primer
    /// pedido puede esperar a que cargue el modelo local).
    static constexpr std::int64_t kDorsoTopeMs = 45000;

    // --- El estado de la conexión (lo pone el view model con lo que pasa) ---------------
    bool hablando = false;          ///< el asistente habla (al oído le llegan ceros)
    std::int64_t finHablaMs = 0;    ///< cuándo terminó de salir lo sintetizado (0 = todavía no)
    std::int64_t ultimaVozMs = 0;   ///< la última voz (de quien sea): para el silencio largo
    std::int64_t turnoMs = 0;       ///< cuándo llegó la última frase del turno
    bool hayListo = false;          ///< hay un `listo` para mandar cuando termine de hablar
    std::int64_t terminoMs = 0;     ///< cuándo terminó (la pantalla de gracias)
    bool buscandoFoto = false;      ///< el paso de la foto de frente
    std::int64_t fotoDesdeMs = 0;   ///< desde cuándo se busca (0 = todavía lo está pidiendo)
    bool buscandoDocumento = false; ///< el paso del documento
    std::int64_t docDesdeMs = 0;    ///< desde cuándo se busca (0 = todavía lo está pidiendo)
    bool leyendoDorso = false;      ///< hay una foto del dorso en el modelo de visión
    std::int64_t canalDesdeMs = 0;  ///< cuándo tomó el canal
    bool primeraDelCanal = false;   ///< todavía no arrancó ninguna con este canal

    /// Lo que mira el view model en cada vuelta (el modelo no lo sabe).
    struct Mirada {
        bool atendida = false;      ///< la atiende una persona: el guion está en pausa
        bool conversacion = false;  ///< hay una conversación armada
        bool enCurso = false;       ///< y está en curso
        bool hayTurno = false;      ///< hay frases oídas sin mandar
        bool oyendoVoz = false;     ///< el VAD oye voz ahora (solo importa con turno)
        bool vozSalio = false;      ///< lo dicho ya salió entero (solo importa hablando)
        bool buscandoDorso = false; ///< el paso del documento pide la parte de atrás
        bool pantallaListo = false; ///< la pantalla muestra las gracias
        bool hayCanal = false;      ///< tiene un canal de audio
        bool identificando = false; ///< hay una cara en el reconocimiento
    };
    enum class Paso {
        Nada,
        CerrarTurno,     ///< lo oído del turno, a la conversación (o al modelo)
        TerminoDeHablar, ///< vuelve a escuchar (ver Decision::mandarListo y ::estado)
        FotoFallo,       ///< no hubo una toma de frente a tiempo
        DocumentoFallo,  ///< no se leyó el documento a tiempo
        Silencio,        ///< un silencio largo en medio de la conversación
        Arrancar,        ///< con canal y sin cara: arranca igual
    };
    struct Decision {
        Paso paso = Paso::Nada;
        bool mandarListo = false; ///< (TerminoDeHablar) mandar el `listo` pendiente a la página
        /// (TerminoDeHablar, en curso) el estado nuevo de la pantalla: foto, escuchando,
        /// dorso o documento. Vacío: no cambia.
        std::string estado;
        bool volverAEsperar = false; ///< la pantalla de gracias ya volvió sola a esperar
    };
    /// Una vuelta: qué hay que hacer ahora (y lo que cambia del estado por el paso del tiempo).
    Decision revisar(const Mirada& m, std::int64_t ahora);
};

} // namespace ba::registro
