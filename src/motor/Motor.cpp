// ============================================================================
// src/motor/Motor.cpp — ver Motor.hpp
// ============================================================================
#include "Motor.hpp"

#include <chrono>

namespace ba::motor {

using json = nlohmann::json;

Motor::Motor(Config c) : cfg_(std::move(c)) {
    reloj_ = std::make_unique<Reloj>(cfg_.reloj, cfg_.relojServidor, cfg_.relojPuerto, cfg_.relojDominio);
    reloj_->ponerTope(cfg_.topeMs);
}

Motor::~Motor() { detener(); }

std::string Motor::iniciar() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (andando()) return {};
    programa_ = std::make_unique<Mezcla>(*reloj_, "programa", Mezcla::Opciones{true, true, true, ""});
    if (!programa_->anda()) return "el bus PROGRAMA no arrancó: " + programa_->error();
    // El programa para el gateway (lo que oyen los invitados en modo «programa»).
    if (!cfg_.prefijoAnillos.empty() && hayFabrica("baanillosink")) {
        const std::string d = "intersrc producer-name=" + entreComillas(programa_->productor()) +
                              " ! queue max-size-time=1000000000 ! baanillosink canal=" +
                              entreComillas(anillo("ContentForReturn"));
        GError* e = nullptr;
        puente_ = gst_parse_launch(d.c_str(), &e);
        if (e != nullptr) {
            registrar(Nivel::Aviso, std::string("el programa para el gateway: ") + e->message);
            g_error_free(e);
            if (puente_ != nullptr) gst_object_unref(puente_);
            puente_ = nullptr;
        } else {
            reloj_->adoptar(puente_);
            gst_pipeline_set_latency(GST_PIPELINE(puente_), static_cast<GstClockTime>(kMargenMezclaNs));
            gst_element_set_state(puente_, GST_STATE_PLAYING);
        }
    }
    seguir_ = true;
    hilo_ = std::thread([this]() { bucle(); });
    return {};
}

void Motor::detener() {
    seguir_ = false;
    if (hilo_.joinable()) hilo_.join();
    std::lock_guard<std::recursive_mutex> l(mu_);
    retornos_.clear();
    if (puente_ != nullptr) {
        gst_element_set_state(puente_, GST_STATE_NULL);
        gst_object_unref(puente_);
        puente_ = nullptr;
    }
    canales_.clear();
    programa_.reset();
}

void Motor::bucle() {
    while (seguir_) {
        {
            std::lock_guard<std::recursive_mutex> l(mu_);
            for (auto& [id, c] : canales_) c.entrada->tic();
            if (programa_) programa_->tic();
            for (auto& [n, r] : retornos_)
                if (r.bus) r.bus->tic();
            if (puente_ != nullptr) {
                GstBus* bus = gst_element_get_bus(puente_);
                while (GstMessage* m = gst_bus_pop(bus)) gst_message_unref(m);
                gst_object_unref(bus);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void Motor::sumar(Mezcla& m, const std::string& id, const Canal& c, int fundidoMs) {
    if (!c.alPrograma || !c.entrada->daAudio()) {
        m.quitar(id);
        return;
    }
    m.agregar(id);
    m.ponerNivel(id, c.nivel);
    m.ponerMute(id, c.mute);
    m.ponerEscena(id, c.global || c.enEscena ? 1.0 : 0.0, fundidoMs);
}

std::string Motor::agregarEntrada(const std::string& id, const json& ajustes, const json& opciones) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (id.empty()) return "falta el id";
    if (id.find(':') != std::string::npos) return "el id no puede tener «:»";
    if (canales_.count(id) > 0) return "ya hay una entrada «" + id + "»";
    Canal c;
    c.global = opciones.value("global", false);
    c.enEscena = opciones.value("en_escena", c.global);
    c.alPrograma = opciones.value("audio", true);
    c.nivel = opciones.value("nivel", 1.0);
    c.mute = opciones.value("mute", false);
    c.entrada = std::make_unique<Entrada>(*reloj_, cfg_.lienzo, id, ajustes);
    auto& puesto = canales_[id] = std::move(c);
    if (programa_) sumar(*programa_, id, puesto, 0);
    for (auto& [n, r] : retornos_)
        if (r.bus && r.propia != id) sumar(*r.bus, id, puesto, 0);
    return {};
}

std::string Motor::actualizarEntrada(const std::string& id, const json& ajustes) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.entrada->actualizar(ajustes);
    // Pudo pasar a tener (o dejar de tener) audio.
    if (programa_) sumar(*programa_, id, it->second, 0);
    for (auto& [n, r] : retornos_)
        if (r.bus && r.propia != id) sumar(*r.bus, id, it->second, 0);
    return {};
}

std::string Motor::quitarEntrada(const std::string& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    if (programa_) programa_->quitar(id);
    for (auto& [n, r] : retornos_)
        if (r.bus) r.bus->quitar(id);
    canales_.erase(it);
    return {};
}

std::string Motor::ponerNivel(const std::string& id, double lineal) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.nivel = std::clamp(lineal, 0.0, 4.0);
    if (programa_) programa_->ponerNivel(id, it->second.nivel);
    for (auto& [n, r] : retornos_)
        if (r.bus) r.bus->ponerNivel(id, it->second.nivel);
    return {};
}

std::string Motor::ponerMute(const std::string& id, bool mute) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    it->second.mute = mute;
    if (programa_) programa_->ponerMute(id, mute);
    for (auto& [n, r] : retornos_)
        if (r.bus) r.bus->ponerMute(id, mute);
    return {};
}

void Motor::escenaAlAire(const std::vector<std::string>& ids, int fundidoMs) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    const std::set<std::string> al(ids.begin(), ids.end());
    for (auto& [id, c] : canales_) {
        if (c.global) continue;
        const bool antes = c.enEscena;
        c.enEscena = al.count(id) > 0;
        if (antes == c.enEscena) continue;
        const double g = c.enEscena ? 1.0 : 0.0;
        if (programa_) programa_->ponerEscena(id, g, fundidoMs);
        for (auto& [n, r] : retornos_)
            if (r.bus) r.bus->ponerEscena(id, g, fundidoMs);
    }
}

std::string Motor::controlMedio(const std::string& id, const std::string& accion, std::int64_t ms) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = canales_.find(id);
    if (it == canales_.end()) return "no hay una entrada «" + id + "»";
    if (accion == "ir") {
        it->second.entrada->irA(ms);
        return {};
    }
    return it->second.entrada->controlMedio(accion);
}

void Motor::armarRetorno(int n, Retorno& r) {
    r.bus.reset();
    if (r.modo != "mix_minus" || cfg_.prefijoAnillos.empty()) return;
    Mezcla::Opciones op;
    op.limitador = true; // lo que oye el invitado tampoco satura
    op.medidores = false;
    op.anillo = anillo("Return." + std::to_string(n));
    r.bus = std::make_unique<Mezcla>(*reloj_, "retorno" + std::to_string(n), op);
    for (const auto& [id, c] : canales_)
        if (id != r.propia) sumar(*r.bus, id, c, 0);
}

std::string Motor::retorno(int n, const std::string& modo, const std::string& propia) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (n < 1 || n > 64) return "invitado fuera de rango (1…64)";
    if (modo != "programa" && modo != "mix_minus" && modo != "apagado") return "modo: programa, mix_minus o apagado";
    if (modo == "programa" || modo == "apagado") {
        retornos_.erase(n);
        return {};
    }
    Retorno& r = retornos_[n];
    if (r.modo == modo && r.propia == propia && r.bus) return {};
    r.modo = modo;
    r.propia = propia;
    armarRetorno(n, r);
    return r.bus && r.bus->anda() ? std::string() : "el retorno no arrancó";
}

bool Motor::ponerLimitador(bool activo, double techoDb) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return programa_ && programa_->ponerLimitador(activo, techoDb);
}

void Motor::ponerTopeEstudio(std::int64_t ms) { reloj_->ponerTope(ms); }

json Motor::estado() const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    json entradas = json::object();
    for (const auto& [id, c] : canales_) {
        json e = c.entrada->estado();
        e["global"] = c.global;
        e["en_escena"] = c.enEscena;
        e["nivel"] = c.nivel;
        e["mute"] = c.mute;
        entradas[id] = std::move(e);
    }
    json ret = json::object();
    for (const auto& [n, r] : retornos_)
        ret[std::to_string(n)] = {{"modo", r.modo}, {"propia", r.propia}, {"anda", r.bus && r.bus->anda()}};
    json tarde = json::array();
    for (const auto& [id, c] : canales_)
        if (c.entrada->llegaTarde()) tarde.push_back(id);
    return {{"andando", andando()},
            {"estudio",
             {{"latencia_ms", static_cast<double>(reloj_->latencia()) / 1e6},
              {"tope_ms", reloj_->topeMs()},
              {"tope_automatico", reloj_->topeAutomatico()},
              {"llegan_tarde", tarde}}},
            {"lienzo",
             {{"ancho", cfg_.lienzo.ancho},
              {"alto", cfg_.lienzo.alto},
              {"fps", static_cast<double>(cfg_.lienzo.fpsN) / std::max(1, cfg_.lienzo.fpsD)}}},
            {"programa", programa_ ? programa_->estado() : json()},
            {"retornos", ret},
            {"entradas", entradas}};
}

} // namespace ba::motor
