// ============================================================================
// src/app/BroadcastEngineDsk.cpp — Capas encima del programa: DSK 1..4, placa y negro
//
// Parte de BroadcastEngine. Todo va al DSK del PROGRAMA del motor (un segundo
// compositor encima de la escena, que no cambia con ella): las cuatro capas DSK
// (escenas o fuentes, en un recuadro o a pantalla completa), la placa de
// seguridad (fondo, imagen y texto) y el fundido a negro, en ese orden de abajo
// hacia arriba. La señal limpia no los lleva.
//
// Cada cambio vuelve a mandar la lista entera; lo nuevo entra y lo que se va
// sale con el fundido pedido (lo que no cambió queda como estaba).
// ============================================================================

#include "BroadcastEngine.hpp"

#include "Motor.hpp"

#include <algorithm>

namespace ba::app {

using json = nlohmann::json;

namespace {
constexpr const char* kFtb = "__ftb__";
constexpr const char* kPlacaFondo = "__placa_fondo__";
constexpr const char* kPlacaTexto = "__placa_texto__";
constexpr const char* kPlacaImagen = "__placa_imagen__";
} // namespace

void BroadcastEngine::publicarCapas(int ms) {
    if (!motor_) return;
    const int W = video_.baseWidth, H = video_.baseHeight;
    json capas = json::array();
    for (int i = 0; i < 4; ++i) {
        const Dsk& d = dsk_[i];
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
    if (placa_) {
        capas.push_back({{"entrada", kPlacaFondo}, {"orden", 800}});
        if (!placaImagen_.empty())
            capas.push_back({{"entrada", kPlacaImagen}, {"x", W / 4}, {"y", H / 8}, {"ancho", W / 2}, {"alto", H / 2},
                             {"orden", 801}, {"contener", true}});
        capas.push_back({{"entrada", kPlacaTexto}, {"x", 0}, {"y", placaImagen_.empty() ? 0 : H * 2 / 3}, {"ancho", W},
                         {"alto", placaImagen_.empty() ? H : H / 3}, {"orden", 802}});
    }
    if (ftb_) capas.push_back({{"entrada", kFtb}, {"orden", 899}});
    const std::string e = motor_->dsk(capas, ms);
    if (!e.empty()) setError("capas: " + e);
}

bool BroadcastEngine::ftb(bool activar, int ms, bool bajarAudio) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    if (activar && !motor_->estado()["entradas"].contains(kFtb))
        motor_->agregarEntrada(kFtb, json{{"tipo", "color"}, {"color", "#FF000000"}, {"ancho", 64}, {"alto", 36}},
                               json{{"interna", true}, {"audio", false}});
    ftb_ = activar;
    // El audio baja con el negro (y vuelve con él) si se pidió.
    if (activar) ftbAudio_ = bajarAudio;
    if (ftbAudio_) motor_->masterVolumen(activar ? 0.0 : 1.0, std::max(0, ms));
    if (!activar) ftbAudio_ = false;
    publicarCapas(std::max(0, ms));
    return true;
}

bool BroadcastEngine::placa(bool activar, const std::string& texto, const std::string& imagen, int ms) {
    if (!started_) {
        setError("el motor no está andando");
        return false;
    }
    if (activar) {
        placaTexto_ = texto.empty() ? std::string("Volvemos enseguida") : texto;
        placaImagen_ = imagen;
        const int H = video_.baseHeight, W = video_.baseWidth;
        const json estado = motor_->estado()["entradas"];
        if (!estado.contains(kPlacaFondo))
            motor_->agregarEntrada(kPlacaFondo, json{{"tipo", "color"}, {"color", "#FF1C1C22"}, {"ancho", 64}, {"alto", 36}},
                                   json{{"interna", true}, {"audio", false}});
        const json t{{"tipo", "texto"}, {"texto", placaTexto_}, {"tamano", std::max(12, H / 22)}, {"color", "#FFFFFFFF"},
                     {"ancho", W}, {"alto", imagen.empty() ? H : H / 3}};
        if (estado.contains(kPlacaTexto))
            motor_->actualizarEntrada(kPlacaTexto, t);
        else
            motor_->agregarEntrada(kPlacaTexto, t, json{{"interna", true}, {"audio", false}});
        if (!imagen.empty()) {
            const json im{{"tipo", "imagen"}, {"archivo", imagen}};
            if (estado.contains(kPlacaImagen))
                motor_->actualizarEntrada(kPlacaImagen, im);
            else
                motor_->agregarEntrada(kPlacaImagen, im, json{{"interna", true}, {"audio", false}});
        }
    }
    placa_ = activar;
    publicarCapas(std::max(0, ms));
    return true;
}

nlohmann::json BroadcastEngine::estadoPlaca() const {
    return {{"ftb", ftb_}, {"ftb_audio", ftbAudio_}, {"placa", placa_}, {"placa_texto", placaTexto_}, {"placa_imagen", placaImagen_}};
}

bool BroadcastEngine::dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro) {
    if (capa < 1 || capa > 4) {
        setError("la capa va de 1 a 4");
        return false;
    }
    for (const auto& n : contenido)
        if (escena(n) == nullptr && sources_.count(n) == 0) {
            setError("no existe la escena o fuente «" + n + "»");
            return false;
        }
    Dsk& d = dsk_[capa - 1];
    const std::vector<std::string> antes = d.contenido;
    d.contenido = contenido;
    d.recuadro = recuadro.is_object() ? recuadro : json::object();
    if (d.visible) publicarCapas(0);
    for (const auto& n : antes)
        if (std::find(contenido.begin(), contenido.end(), n) == contenido.end()) soltarSiHuerfana(n);
    return true;
}

bool BroadcastEngine::dskMostrar(int capa, bool visible, int ms) {
    if (capa < 1 || capa > 4) {
        setError("la capa va de 1 a 4");
        return false;
    }
    if (visible && dsk_[capa - 1].contenido.empty()) {
        setError("la capa " + std::to_string(capa) + " está vacía (dsk_armar primero)");
        return false;
    }
    dsk_[capa - 1].visible = visible;
    publicarCapas(std::max(0, ms));
    return true;
}

nlohmann::json BroadcastEngine::dskEstado() const {
    nlohmann::json out = nlohmann::json::array();
    for (int i = 0; i < 4; ++i)
        out.push_back({{"capa", i + 1},
                       {"visible", dsk_[i].visible},
                       {"opacidad", dsk_[i].visible ? 1.0 : 0.0},
                       {"contenido", dsk_[i].contenido}});
    return out;
}

std::vector<std::string> BroadcastEngine::fuentesDsk() const {
    std::vector<std::string> out;
    for (const auto& d : dsk_) {
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
        setError("el motor no está andando");
        return false;
    }
    const json s{{"text", texto},
                 {"font", {{"face", "Helvetica"}, {"size", video_.baseHeight / 24}, {"flags", 1}}},
                 {"color1", 0xFFFFFFFF},
                 {"outline", true}};
    if (sources_.count(nombre) > 0) return updateSourceSettings(nombre, s.dump());
    SourceSpec spec;
    spec.id = "text_ft2_source_v2";
    spec.name = nombre;
    spec.settingsJson = s.dump();
    return crearFuente(spec, true); // global: queda viva aunque no esté en ninguna escena
}

} // namespace ba::app
