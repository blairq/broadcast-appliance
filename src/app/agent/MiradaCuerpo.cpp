// ============================================================================
// src/app/agent/MiradaCuerpo.cpp — el cuerpo del pedido y el recorte (ver Mirada.hpp)
//
// Aparte del resto (Qt, HTTPS, credenciales) para que el test lo pruebe sin nada
// de eso (test_documento_dorso).
// ============================================================================
#include "agent/Mirada.hpp"

#include <algorithm>

#include "documentos/Mrz.hpp"

namespace ba::app::agent {

nlohmann::json cuerpoMirada(const std::string& modelo, const PedidoMirada& p, const std::string& jpegBase64) {
    using nlohmann::json;
    json contenido = json::array();
    contenido.push_back(json{{"type", "text"}, {"text", p.pregunta}});
    contenido.push_back(
        json{{"type", "image_url"}, {"image_url", {{"url", "data:image/jpeg;base64," + jpegBase64}}}});
    json mensajes = json::array();
    if (!p.sistema.empty()) mensajes.push_back(json{{"role", "system"}, {"content", p.sistema}});
    mensajes.push_back(json{{"role", "user"}, {"content", contenido}});
    json body{{"model", modelo}, {"messages", mensajes}, {"max_tokens", p.maxTokens}, {"temperature", 0}};
    // Sin pensar: para mirar una imagen no hace falta, y en vivo el tiempo es lo
    // que importa.
    body["chat_template_kwargs"] = json{{"enable_thinking", false}, {"reasoning_effort", "low"}};
    return body;
}

PedidoMirada pedidoDelDorso() {
    PedidoMirada p;
    p.pregunta = ba::documentos::pedidoMrz();
    // La franja de abajo: los renglones quedan grandes y el modelo no se distrae con
    // la foto, la firma ni el código de arriba. Las capturas del video (960x540)
    // daban «NO SE VE»: van las fotos enteras que manda la página del tótem.
    p.recorteDesde = 0.58;
    p.anchoMax = 1920;
    p.calidad = 95;
    p.maxTokens = 200; // tres renglones de 30 y algo de sobra
    p.esperaMs = 20000; // más, y la persona ya se cansó de sostener el documento
    return p;
}

void recorteMirada(int ancho, int alto, const PedidoMirada& p, int& x, int& y, int& w, int& h) {
    const double desde = std::clamp(p.recorteDesde, 0.0, 0.95);
    x = 0;
    w = ancho;
    y = static_cast<int>(alto * desde);
    h = alto - y;
}

} // namespace ba::app::agent
