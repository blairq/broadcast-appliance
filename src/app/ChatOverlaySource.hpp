// ============================================================================
// src/app/ChatOverlaySource.hpp — Fuente "Chat en vivo" (libobs)
//
// Un tipo de fuente mas para las escenas: el chat del vivo de YouTube y/o el
// chat de la sala, sobre el video (fondo transparente). La registra la propia
// app (no un plugin): la imagen la dibuja la interfaz (ChatOverlayRenderer,
// Qt/QPainter) con los mensajes que ya tiene y la entrega aca como cuadros
// BGRA (fuente asincrona de libobs). Cada instancia tiene su tamaño y estilo.
//
// Threading: libobs crea/destruye instancias desde sus threads; el
// renderizador las consulta y les entrega cuadros desde el thread de la UI.
// Todo pasa por un registro con mutex, por id (nunca punteros sueltos).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::app::chatoverlay {

constexpr const char* kSourceId = "ba_chat_overlay";

struct Instance {
    std::uint64_t id = 0;
    std::string name; ///< nombre de la fuente (para depurar)
    int width = 640;
    int height = 720;
    int fontSize = 28;
    int maxMessages = 8;
    bool showYoutube = true;
    bool showTwitch = true;
    bool showFacebook = true;
    bool showRoom = true;
    int bgOpacity = 55; ///< 0..100, fondo de cada mensaje
    bool showNames = true;
    int fadeSec = 0; ///< 0 = no desaparecen
    /// Solo los mensajes que el operador DESTACÓ (de fábrica): lo que escribe el
    /// público no sale al aire sin que una persona lo elija.
    bool soloDestacados = true;
    std::uint64_t settingsVersion = 0;
};

/// Registra el tipo en libobs (despues de cargar los modulos).
void registerSourceType();
/// Instancias vivas (copia).
std::vector<Instance> instances();
/// Entrega un cuadro BGRA (alfa recto) a la instancia `id` (si sigue viva).
void push(std::uint64_t id, const std::uint8_t* bgra, int width, int height);

} // namespace ba::app::chatoverlay
