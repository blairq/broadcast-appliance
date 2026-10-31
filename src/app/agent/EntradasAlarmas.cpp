// src/app/agent/EntradasAlarmas.cpp — ver el .hpp

#include "agent/EntradasAlarmas.hpp"

#include <utility>

namespace ba::app::agent {

EntradasAlarmas::EntradasAlarmas(AlertInputs deLaInterfaz, AlertInputs directas)
    : ui_(std::move(deLaInterfaz)), directas_(std::move(directas)) {}

void EntradasAlarmas::refrescar() {
    // Solo lo que la interfaz da y no hay directo (lo directo no pasa por la foto).
    const auto copiar = [](const auto& fuente, const auto& directa, auto& foto) {
        if (fuente && !directa) foto.poner(fuente());
    };
    if (ui_.audio) f_->audio.poner(ui_.audio()); // el audio se junta con el directo
    copiar(ui_.media, directas_.media, f_->media);
    copiar(ui_.room, directas_.room, f_->room);
    copiar(ui_.chat, directas_.chat, f_->chat);
    copiar(ui_.roomChat, directas_.roomChat, f_->roomChat);
    copiar(ui_.textos, directas_.textos, f_->textos);
    copiar(ui_.status, directas_.status, f_->status);
    copiar(ui_.disco, directas_.disco, f_->disco);
    copiar(ui_.senales, directas_.senales, f_->senales);
    copiar(ui_.loudness, directas_.loudness, f_->loudness);
    copiar(ui_.superres, directas_.superres, f_->superres);
    copiar(ui_.programaVideo, directas_.programaVideo, f_->programaVideo);
    copiar(ui_.recursos, directas_.recursos, f_->recursos);
    copiar(ui_.persistencia, directas_.persistencia, f_->persistencia);
    copiar(ui_.energia, directas_.energia, f_->energia);
    f_->vuelta.poner(true);
}

AlertInputs EntradasAlarmas::paraElVigia() const {
    AlertInputs in;
    const std::shared_ptr<Fotos> f = f_;
    // Directa si la hay; si no, la foto de lo que dio la interfaz; si no, nada (como antes).
    // Las fotos viven en f_, que se comparte con el vigía: no dependen de esta instancia.
    const auto deFoto = [f](auto Fotos::*miembro) { return [f, miembro]() { return ((*f).*miembro).leer(); }; };
    const auto tomar = [&](const auto& directa, const auto& fuente, auto miembro, auto& destino) {
        if (directa)
            destino = directa;
        else if (fuente)
            destino = deFoto(miembro);
    };
    tomar(directas_.media, ui_.media, &Fotos::media, in.media);
    tomar(directas_.room, ui_.room, &Fotos::room, in.room);
    tomar(directas_.chat, ui_.chat, &Fotos::chat, in.chat);
    tomar(directas_.roomChat, ui_.roomChat, &Fotos::roomChat, in.roomChat);
    tomar(directas_.textos, ui_.textos, &Fotos::textos, in.textos);
    tomar(directas_.status, ui_.status, &Fotos::status, in.status);
    tomar(directas_.disco, ui_.disco, &Fotos::disco, in.disco);
    tomar(directas_.senales, ui_.senales, &Fotos::senales, in.senales);
    tomar(directas_.loudness, ui_.loudness, &Fotos::loudness, in.loudness);
    tomar(directas_.superres, ui_.superres, &Fotos::superres, in.superres);
    tomar(directas_.programaVideo, ui_.programaVideo, &Fotos::programaVideo, in.programaVideo);
    tomar(directas_.recursos, ui_.recursos, &Fotos::recursos, in.recursos);
    tomar(directas_.persistencia, ui_.persistencia, &Fotos::persistencia, in.persistencia);
    tomar(directas_.energia, ui_.energia, &Fotos::energia, in.energia);
    // El audio: las fuentes de la foto y lo directo (el programa) al final.
    if (ui_.audio || directas_.audio)
        in.audio = [f, directa = directas_.audio]() {
            std::vector<AlertInputs::Audio> v = f->audio.leer();
            if (directa)
                for (AlertInputs::Audio& a : directa()) v.push_back(std::move(a));
            return v;
        };
    in.nowMs = directas_.nowMs ? directas_.nowMs : ui_.nowMs;
    return in;
}

} // namespace ba::app::agent
