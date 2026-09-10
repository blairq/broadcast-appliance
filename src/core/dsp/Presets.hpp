// Presets.hpp
//
// Proposito:
//   Presets de channel strip (spec 7.3): funciones puras que devuelven un
//   `StripParams` con los valores de fabrica de los tres micrófonos de
//   broadcast. Son solo valores iniciales: el ingeniero los ajusta en
//   `dsp_presets.json` (esquema versionado). El automixer de mesa redonda
//   (Dugan) es un objeto compartido entre filtros, NO un preset, y queda
//   fuera de este archivo.
//
//   - `presetPodium()`    -> atril / cuello de cisne.
//   - `presetLavalier()`  -> corbatero / lavalier.
//   - `presetHandheld()`  -> microfono de mano.
//
// Modelo de threading:
//   Funciones puras y sin estado: thread-safe, pueden llamarse desde cualquier
//   thread a la vez. Devuelven `StripParams` por valor.
//
// Estrategia de errores:
//   Ninguna: no hay I/O ni asignacion fallible. Cada preset fija `fs=48000`
//   (el reloj de libobs, spec 7.4) y deja los campos no mencionados en su
//   valor por defecto de `StripParams`.
#pragma once

#include "ChannelStrip.hpp"

namespace ba::dsp {

/// Preset ATRIL / cuello de cisne (spec 7.3).
///
/// HPF 100-120 Hz a 18 dB/oct; corte suave de -2..-3 dB entre 250-400 Hz
/// (contra el sonido "encajonado"); compresor 2.5:1 con knee suave, attack
/// 10 ms, release 150 ms. El "notch adaptativo" de 3 bandas fijas fue
/// eliminado (no se adaptan a nada y dañan la voz sin motivo).
///
/// :returns: `StripParams` con los valores de fabrica del atril.
inline StripParams presetPodium() {
    StripParams p;
    p.fs = 48000.0;

    // HPF 110 Hz (dentro de 100-120), 18 dB/oct.
    p.hpfFreq = 110.0;
    p.hpfSlopeDbOct = 18.0;

    // Corte suave de -2.5 dB en 320 Hz (centro de 250-400), peaking Q~1.
    p.eq[0].type = BiquadType::Peaking;
    p.eq[0].freq = 320.0;
    p.eq[0].gainDb = -2.5;
    p.eq[0].q = 1.0;
    p.eq[0].enabled = true;

    // Compresor 2.5:1, knee suave, attack 10 ms, release 150 ms.
    p.compEnabled = true;
    p.compRatio = 2.5;
    p.compThresholdDb = -20.0;
    p.compKneeDb = 6.0;
    p.compAttackMs = 10.0;
    p.compReleaseMs = 150.0;

    // Limitador por defecto (-1 dBFS) para seguridad.
    p.limEnabled = true;
    p.limCeilingDb = -1.0;
    return p;
}

/// Preset CORBATERO / lavalier (spec 7.3).
///
/// HPF 80-100 Hz; de-esser entre 5-8 kHz; expander 1:2 (umbral -45 dBFS,
/// rango -12 dB) en vez de un gate duro (que cortaria sílabas); compresor
/// 3:1. Opcional: shelving de agudos +2 dB para compensar la ubicacion en el
/// pecho. El compresor multibanda fue eliminado (innecesario para voz).
///
/// :returns: `StripParams` con los valores de fabrica del corbatero.
inline StripParams presetLavalier() {
    StripParams p;
    p.fs = 48000.0;

    // HPF 90 Hz (dentro de 80-100), 18 dB/oct.
    p.hpfFreq = 90.0;
    p.hpfSlopeDbOct = 18.0;

    // Shelving de agudos +2 dB (opcional, compensa pecho).
    p.eq[0].type = BiquadType::HighShelf;
    p.eq[0].freq = 8000.0;
    p.eq[0].gainDb = 2.0;
    p.eq[0].q = 0.7071;
    p.eq[0].enabled = true;

    // De-esser 6.5 kHz (dentro de 5-8 kHz).
    p.deEsserEnabled = true;
    p.deEsserFreq = 6500.0;
    p.deEsserRatio = 4.0;
    p.deEsserThresholdDb = -30.0;

    // Expander/gate suave: umbral -45 dBFS, rango -12 dB.
    p.gateEnabled = true;
    p.gateThresholdDb = -45.0;
    p.gateRangeDb = 12.0;
    p.gateAttackMs = 1.0;
    p.gateReleaseMs = 100.0;

    // Compresor 3:1.
    p.compEnabled = true;
    p.compRatio = 3.0;
    p.compThresholdDb = -20.0;
    p.compKneeDb = 6.0;
    p.compAttackMs = 10.0;
    p.compReleaseMs = 150.0;

    // Limitador por defecto (-1 dBFS).
    p.limEnabled = true;
    p.limCeilingDb = -1.0;
    return p;
}

/// Preset MICROFONO DE MANO (spec 7.3).
///
/// HPF 100 Hz; low-shelf de -4 dB en 200 Hz (contra el efecto de
/// proximidad); compresor 3:1; limitador a -1 dBFS.
///
/// :returns: `StripParams` con los valores de fabrica del microfono de mano.
inline StripParams presetHandheld() {
    StripParams p;
    p.fs = 48000.0;

    // HPF 100 Hz, 18 dB/oct.
    p.hpfFreq = 100.0;
    p.hpfSlopeDbOct = 18.0;

    // Low-shelf -4 dB en 200 Hz (efecto de proximidad).
    p.eq[0].type = BiquadType::LowShelf;
    p.eq[0].freq = 200.0;
    p.eq[0].gainDb = -4.0;
    p.eq[0].q = 0.7071;
    p.eq[0].enabled = true;

    // Compresor 3:1.
    p.compEnabled = true;
    p.compRatio = 3.0;
    p.compThresholdDb = -20.0;
    p.compKneeDb = 6.0;
    p.compAttackMs = 10.0;
    p.compReleaseMs = 150.0;

    // Limitador a -1 dBFS.
    p.limEnabled = true;
    p.limCeilingDb = -1.0;
    return p;
}

} // namespace ba::dsp
