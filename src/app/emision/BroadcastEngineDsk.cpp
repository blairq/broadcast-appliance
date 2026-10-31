// ============================================================================
// src/app/emision/BroadcastEngineDsk.cpp — Capas encima del programa: DSK 1..4, placa y negro
//
// Parte de BroadcastEngine. Todo va al DSK del PROGRAMA del motor (un segundo
// compositor encima de la escena, que no cambia con ella): las cuatro capas DSK
// (escenas o fuentes, en un recuadro o a pantalla completa), la placa de
// seguridad (fondo, imagen y texto) y el fundido a negro, en ese orden de abajo
// hacia arriba. La señal limpia no los lleva.
//
// Cada cambio vuelve a mandar la lista entera; lo nuevo entra y lo que se va
// sale con el fundido pedido (lo que no cambió queda como estaba).
//
// Cada salida tiene las suyas (PLAN-SALIDAS): su DSK, su placa y su negro van al
// PROGRAMA de esa salida. El fondo de la placa y el negro son la misma entrada
// interna para todas; el texto y la imagen de la placa, una por salida.
// ============================================================================

#include "emision/BroadcastEngine.hpp"

#include "Motor.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>

namespace ba::app {

using json = nlohmann::json;

namespace {
constexpr const char* kFtb = "__ftb__";
constexpr const char* kPlacaFondo = "__placa_fondo__";
constexpr const char* kPlacaTexto = "__placa_texto__";
constexpr const char* kPlacaImagen = "__placa_imagen__";
/// La entrada interna de una salida: la de siempre para la por omisión, con su id para
/// las demás (los ids de entrada no llevan «:», ver motor::Motor).
std::string internaDe(const char* base, const std::string& salida) {
    return salida == BroadcastEngine::kSalidaPorOmision ? std::string(base) : base + salida + "__";
}
} // namespace

void BroadcastEngine::publicarCapas(Salida& s, int ms) {
    if (!motor_) return;
    const int W = video_.baseWidth, H = video_.baseHeight;
    const std::string placaTexto = internaDe(kPlacaTexto, s.id), placaImagen = internaDe(kPlacaImagen, s.id);
    json capas = json::array();
    for (int i = 0; i < 4; ++i) {
        const Dsk& d = s.dsk[i];
        if (!d.visible) continue;
        for (std::size_t k = 0; k < d.contenido.size(); ++k) {
            const std::string& n = d.contenido[k];
            json c = escena(n) != nullptr ? json{{"escena", n}} : json{{"entrada", n}};
            int x = 0, y = 0, w = W, h = H;
            if (d.recuadro.is_object() && !d.recuadro.empty()) {
                const double tam = std::clamp(d.recuadro.value("tamano", 0.28), 0.05, 1.0);
                const double margen = std::clamp(d.recuadro.value("margen", 0.03), 0.0, 0.2);
                const std::string pos = d.recuadro.value("posicion", std::string("abajo_derecha"));
                w = static_cast<int>(W * tam);
                h = w * H / W;
                const int mx = static_cast<int>(W * margen), my = static_cast<int>(H * margen);
                x = pos.find("derecha") != std::string::npos ? W - mx - w : mx;
                y = pos.find("abajo") != std::string::npos ? H - my - h : my;
            }
            c.update({{"x", x}, {"y", y}, {"ancho", w}, {"alto", h}, {"orden", i * 20 + static_cast<int>(k)}, {"contener", true}});
            capas.push_back(c);
        }
    }
    if (s.placa) {
        capas.push_back({{"entrada", kPlacaFondo}, {"orden", 800}});
        if (!s.placaImagen.empty())
            capas.push_back({{"entrada", placaImagen}, {"x", W / 4}, {"y", H / 8}, {"ancho", W / 2}, {"alto", H / 2},
                             {"orden", 801}, {"contener", true}});
        capas.push_back({{"entrada", placaTexto}, {"x", 0}, {"y", s.placaImagen.empty() ? 0 : H * 2 / 3}, {"ancho", W},
                         {"alto", s.placaImagen.empty() ? H : H / 3}, {"orden", 802}});
    }
    if (s.ftb) capas.push_back({{"entrada", kFtb}, {"orden", 899}});
    const std::string e = motor_->dsk(capas, ms, s.id);
    if (!e.empty()) setError(ba::i18n::llenar(ba::i18n::traducir("emision", "capas: %1"), {e}));
}

bool BroadcastEngine::ftb(bool activar, int ms, bool bajarAudio, const std::string& salida) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    // Las internas se recuerdan acá: el motor las arma en su cola (ver
    // Motor::orden), así que preguntarle si ya está no sirve recién pedida.
    if (activar && internasPedidas_.insert(kFtb).second)
        motor_->agregarEntrada(kFtb, json{{"tipo", "color"}, {"color", "#FF000000"}, {"ancho", 64}, {"alto", 36}},
                               json{{"interna", true}, {"audio", false}});
    s->ftb = activar;
    // El audio baja con el negro (y vuelve con él) si se pidió.
    if (activar) s->ftbAudio = bajarAudio;
    if (s->ftbAudio) motor_->masterVolumen(activar ? 0.0 : 1.0, std::max(0, ms), s->id);
    if (!activar) s->ftbAudio = false;
    publicarCapas(*s, std::max(0, ms));
    return true;
}

bool BroadcastEngine::placa(bool activar, const std::string& texto, const std::string& imagen, int ms,
                            const std::string& salida) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    if (activar) {
        // Sale al aire: el texto por defecto va en el idioma del evento, y se arma al
        // activarla (una placa ya al aire no cambia si cambia el idioma: la próxima sí).
        s->placaTexto = texto.empty() ? ba::i18n::traducirEvento("emision", "Volvemos enseguida") : texto;
        s->placaImagen = imagen;
        const int H = video_.baseHeight, W = video_.baseWidth;
        if (internasPedidas_.insert(kPlacaFondo).second)
            motor_->agregarEntrada(kPlacaFondo, json{{"tipo", "color"}, {"color", "#FF1C1C22"}, {"ancho", 64}, {"alto", 36}},
                                   json{{"interna", true}, {"audio", false}});
        const json t{{"tipo", "texto"}, {"texto", s->placaTexto}, {"tamano", std::max(12, H / 22)}, {"color", "#FFFFFFFF"},
                     {"ancho", W}, {"alto", imagen.empty() ? H : H / 3}};
        const std::string placaTexto = internaDe(kPlacaTexto, s->id), placaImagen = internaDe(kPlacaImagen, s->id);
        if (!internasPedidas_.insert(placaTexto).second)
            motor_->actualizarEntrada(placaTexto, t);
        else
            motor_->agregarEntrada(placaTexto, t, json{{"interna", true}, {"audio", false}});
        if (!imagen.empty()) {
            const json im{{"tipo", "imagen"}, {"archivo", imagen}};
            if (!internasPedidas_.insert(placaImagen).second)
                motor_->actualizarEntrada(placaImagen, im);
            else
                motor_->agregarEntrada(placaImagen, im, json{{"interna", true}, {"audio", false}});
        }
    }
    s->placa = activar;
    publicarCapas(*s, std::max(0, ms));
    return true;
}

nlohmann::json BroadcastEngine::estadoPlaca(const std::string& salida) const {
    const Salida* s = laSalida(salida);
    if (s == nullptr) return json::object();
    return {{"ftb", s->ftb}, {"ftb_audio", s->ftbAudio}, {"placa", s->placa}, {"placa_texto", s->placaTexto},
            {"placa_imagen", s->placaImagen}};
}

bool BroadcastEngine::dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro,
                               const std::string& salida) {
    if (capa < 1 || capa > 4) {
        setError(ba::i18n::traducir("emision", "la capa va de 1 a 4"));
        return false;
    }
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    for (const auto& n : contenido)
        if (escena(n) == nullptr && sources_.count(n) == 0) {
            setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena o fuente «%1»"), {n}));
            return false;
        }
    Dsk& d = s->dsk[capa - 1];
    const std::vector<std::string> antes = d.contenido;
    d.contenido = contenido;
    d.recuadro = recuadro.is_object() ? recuadro : json::object();
    if (d.visible) publicarCapas(*s, 0);
    for (const auto& n : antes)
        if (std::find(contenido.begin(), contenido.end(), n) == contenido.end()) soltarSiHuerfana(n);
    return true;
}

bool BroadcastEngine::dskMostrar(int capa, bool visible, int ms, const std::string& salida) {
    if (capa < 1 || capa > 4) {
        setError(ba::i18n::traducir("emision", "la capa va de 1 a 4"));
        return false;
    }
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    if (visible && s->dsk[capa - 1].contenido.empty()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "la capa %1 está vacía (dsk_armar primero)"), {std::to_string(capa)}));
        return false;
    }
    s->dsk[capa - 1].visible = visible;
    publicarCapas(*s, std::max(0, ms));
    return true;
}

nlohmann::json BroadcastEngine::dskEstado(const std::string& salida) const {
    nlohmann::json out = nlohmann::json::array();
    const Salida* s = laSalida(salida);
    if (s == nullptr) return out;
    for (int i = 0; i < 4; ++i)
        out.push_back({{"capa", i + 1},
                       {"visible", s->dsk[i].visible},
                       {"opacidad", s->dsk[i].visible ? 1.0 : 0.0},
                       {"contenido", s->dsk[i].contenido}});
    return out;
}

std::vector<std::string> BroadcastEngine::fuentesDsk(const std::string& salida) const {
    std::vector<std::string> out;
    const Salida* s = laSalida(salida);
    if (s == nullptr) return out;
    for (const auto& d : s->dsk) {
        if (!d.visible) continue;
        for (const std::string& n : d.contenido) {
            // Una escena cuenta con todo lo que tiene adentro.
            const auto hijas = sceneSources(n);
            if (hijas.empty())
                out.push_back(n);
            else
                out.insert(out.end(), hijas.begin(), hijas.end());
        }
    }
    return out;
}

bool BroadcastEngine::textoGlobal(const std::string& nombre, const std::string& texto) {
    if (!started_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    const json s{{"text", texto},
                 {"font", {{"face", "Helvetica"}, {"size", video_.baseHeight / 24}, {"flags", 1}}},
                 {"color1", 0xFFFFFFFF},
                 {"outline", true}};
    if (sources_.count(nombre) > 0) return updateSourceSettings(nombre, s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    SourceSpec spec;
    spec.id = "text_ft2_source_v2";
    spec.name = nombre;
    spec.settingsJson = s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return crearFuente(spec, true); // global: queda viva aunque no esté en ninguna escena
}

} // namespace ba::app
