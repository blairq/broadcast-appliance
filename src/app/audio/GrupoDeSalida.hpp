// ============================================================================
// src/app/audio/GrupoDeSalida.hpp — a qué placa pertenece una salida de audio
//
// La lista de salidas (Audio → «Salida…») se agrupa por placa. La clave del
// grupo sale del ID de la salida, que es estable, y NUNCA de su nombre: el
// nombre lo pone el operador (Dispositivos y equipos) y renombrar una salida
// la sacaba de su grupo —en la Mac, donde el id no tiene puntos, el grupo era
// el nombre—.
//
// Solo en PulseAudio el id dice la placa: `alsa_output.<placa>.<perfil>`
// (también `bluez_output.…`): la placa es el tramo entre el primer y el
// segundo punto. En el resto (Core Audio, WASAPI: «{0.0.0.00000000}.{guid}»,
// que también tiene puntos y no dice nada de la placa) cada salida es su
// propio grupo, con su id de clave.
//
// Sin Qt: se prueba solo (test_dispositivos).
// ============================================================================
#pragma once

#include <string>

namespace ba::app::audio {

inline std::string grupoDeSalida(const std::string& id) {
    const std::size_t punto = id.find('.');
    // «<algo>_output.<placa>.<perfil>»: el formato de los sinks de PulseAudio.
    constexpr const char* kSufijo = "_output";
    constexpr std::size_t kLargo = 7;
    if (punto != std::string::npos && punto > kLargo && id.compare(punto - kLargo, kLargo, kSufijo) == 0) {
        const std::size_t fin = id.find('.', punto + 1);
        if (fin != std::string::npos && fin > punto + 1) return id.substr(punto + 1, fin - punto - 1);
    }
    return id;
}

} // namespace ba::app::audio
