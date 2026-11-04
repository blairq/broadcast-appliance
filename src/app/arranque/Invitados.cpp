// ============================================================================
// src/app/arranque/Invitados.cpp — ver Invitados.hpp
// ============================================================================

#include "arranque/Invitados.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "Motor.hpp" // motor: el retorno del invitado sin audio
#include "arranque/RuntimeMotor.hpp"
#include "emision/InputSpecs.hpp"
#include "i18n/I18nTexto.hpp"

namespace ba::app {

namespace {

/// El audio del invitado del lugar `slot`: su entrada, con su retorno (de fábrica, el programa
/// sin su voz). Sin audio de invitados (guests.audio) su voz no está en el programa, pero igual
/// necesita su anillo de retorno: el gateway nunca le manda el programa común a un invitado
/// al aire, y sin anillo le apaga el retorno (gateway/RetornoLugar.hpp).
void audioDeInvitado(ApiMotor& engine, bool conAudio, int slot) {
    if (!conAudio) {
        if (motor::Motor* m = engine.motor()) m->retorno(slot, "programa", "");
        return;
    }
    if (!engine.addGuestAudio(slot))
        std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "invitado %1 (audio) omitido: %2"),
                                                     {std::to_string(slot), engine.lastError()})
                                        .c_str());
}

/// ¿Alguna escena tiene el video del invitado del lugar `slot` por `canal`? La identidad es el
/// lugar y el canal (de sus ajustes), nunca el nombre («Invitado 2» se puede renombrar).
bool hayVideoDeInvitado(const AppConfig& c, int slot, const std::string& canal) {
    for (const auto& sc : c.scenes)
        for (const auto& s : sc.sources)
            if (inputs::lugarDeInvitado(s) == std::make_pair(slot, canal)) return true;
    return false;
}

} // namespace

void Invitados::ajustar(int n) {
    AppConfig& cfg = rt_.cfg_;
    const int antes = cfg.guests;
    cfg.guests = n;
    rt_.gateway_.guests = n;
    for (int slot = antes + 1; slot <= n; ++slot) audioDeInvitado(rt_.motorPropio(), cfg.guestAudio, slot);
    // Y el video de los lugares nuevos (la cámara y la pantalla de cada uno): sin
    // esto, con el cupo en 4 se veían solo 2 invitados en Entradas. Ocultas en la
    // primera escena, como las del arranque: el operador las pone donde quiera.
    if (cfg.guestVideo && !cfg.scenes.empty()) {
        for (int slot = antes + 1; slot <= n; ++slot)
            for (const bool share : {false, true}) {
                if (hayVideoDeInvitado(cfg, slot, share ? "share" : "guest")) continue;
                SourceSpec spec = inputs::videoInvitadoSpec(slot, share ? "share" : "guest",
                                                            "Invitado " + std::to_string(slot) + (share ? " — pantalla" : ""));
                spec.visible = false;
                if (!rt_.addSource(cfg.scenes.front().name, spec))
                    std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "«%1» omitido: %2"),
                                                                 {spec.name, rt_.lastError()})
                                                    .c_str());
            }
    }
    // Al bajar el cupo (la sala ya lo aceptó: no hay nadie en esos lugares), las
    // entradas de video de los lugares que se van salen de todas las escenas: si no,
    // en Video seguían «Invitado 4…6» sin nadie. El audio queda (en silencio: lleva
    // atado su retorno mix-minus). Si el cupo vuelve a subir, se crean de nuevo.
    if (n < antes) {
        std::vector<std::pair<std::string, std::string>> sacar; // escena, fuente
        for (const auto& sc : cfg.scenes)
            for (const auto& s : sc.sources) {
                const auto [slot, canal] = inputs::lugarDeInvitado(s);
                if (slot > n && (canal == "guest" || canal == "share")) sacar.emplace_back(sc.name, s.name);
            }
        for (const auto& [escena, fuente] : sacar) rt_.removeSource(escena, fuente);
    }
    rt_.save();
}

void Invitados::agregar() {
    AppConfig& cfg = rt_.cfg_;
    ApiMotor& engine = rt_.motorPropio();
    // Audio de invitados: global (canal propio), siempre en la mezcla.
    for (int slot = 1; slot <= cfg.guests; ++slot) audioDeInvitado(engine, cfg.guestAudio, slot);
    // Video: si ninguna escena tiene invitados (config nueva o vieja), se
    // agregan a la primera escena en fila; despues el operador los mueve.
    bool any = false;
    for (const auto& sc : cfg.scenes) {
        for (const auto& s : sc.sources) {
            any = any || inputs::esVideoInvitado(s.id, s.settingsJson);
        }
    }
    if (!any && cfg.guestVideo && !cfg.scenes.empty()) {
        for (int slot = 1; slot <= cfg.guests; ++slot) {
            SourceSpec spec = inputs::videoInvitadoSpec(slot, "guest", "Invitado " + std::to_string(slot));
            spec.rect = guestRect(slot, cfg.guests);
            cfg.scenes.front().sources.push_back(spec);
        }
    }
    // R3 (docs/funciones/SESION-REMOTA.md): el integrante remoto con la palabra, como una
    // entrada más. El audio es global (suena en cualquier escena, con su rack en
    // Audio); el video va oculto a pantalla completa en la primera escena, como la
    // pantalla de un invitado: el operador lo muestra (o lo usa en otra escena).
    // Dos canales: el que tiene la palabra y, en un debate entre remotos, el otro (canal 2).
    for (int k = 1; k <= 2; ++k) {
        const std::string sufijo = k == 1 ? "" : " " + std::to_string(k);
        SourceSpec a;
        a.name = "Remoto con la palabra" + sufijo + " (audio)";
        a.id = inputs::kMicType; // por GStreamer (baanillo): su rack corre en la cadena
        a.settingsJson = inputs::ajustesAudioAnillo("remoto", k);
        if (!engine.addGlobalAudio(a))
            std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "el audio de «%1» no entró: %2"),
                                                         {a.name, engine.lastError()})
                                            .c_str());
        if (!hayVideoDeInvitado(cfg, k, "remoto") && !cfg.scenes.empty()) {
            SourceSpec v = inputs::videoInvitadoSpec(k, "remoto", "Remoto con la palabra" + sufijo);
            v.visible = false;
            cfg.scenes.front().sources.push_back(v);
        }
        // Lo que comparte (su pantalla o la otra cámara del celular), oculto como la pantalla de un invitado.
        if (!hayVideoDeInvitado(cfg, k, "remoto_contenido") && !cfg.scenes.empty()) {
            SourceSpec v = inputs::videoInvitadoSpec(k, "remoto_contenido", "Remoto con la palabra" + sufijo + " — contenido");
            v.visible = false;
            cfg.scenes.front().sources.push_back(v);
        }
    }
    // Cada lugar con su cámara y su pantalla (la pantalla o ventana que comparte, a pantalla
    // completa), aunque ya hubiera invitados: si el cupo se subió (a 4) con escenas que
    // tenían los de 2, los nuevos no se creaban y en Entradas se veían solo 2. Los que
    // faltan, OCULTOS en la primera escena: el operador los muestra cuando los quiere al aire.
    if (cfg.guestVideo && !cfg.scenes.empty()) {
        for (int slot = 1; slot <= cfg.guests; ++slot)
            for (const bool share : {false, true}) {
                if (hayVideoDeInvitado(cfg, slot, share ? "share" : "guest")) continue;
                SourceSpec spec = inputs::videoInvitadoSpec(slot, share ? "share" : "guest",
                                                            "Invitado " + std::to_string(slot) + (share ? " — pantalla" : ""));
                spec.visible = false;
                cfg.scenes.front().sources.push_back(spec);
            }
    }
}

CanvasRect guestRect(int slot, int count) {
    const float w = 1.0f / static_cast<float>(count > 0 ? count : 1);
    return CanvasRect{w * static_cast<float>(slot - 1), 0.62f, w, 0.36f};
}

} // namespace ba::app
