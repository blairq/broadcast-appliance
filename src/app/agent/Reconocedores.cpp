// ============================================================================
// src/app/agent/Reconocedores.cpp — ver Reconocedores.hpp
// ============================================================================
#include "agent/Reconocedores.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "agent/VozIdioma.hpp"
#include "i18n/I18n.hpp"

// Sin Qt (test_reconocedores): la marca para lupdate, que deja el texto como está.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(contexto, texto) texto
#endif

namespace ba::app::agent {

using json = nlohmann::json;

namespace {
constexpr MotorOido kDeFabrica = MotorOido::Parakeet; // nadie cambia sin pedirlo (si entiende el idioma)
/// El título o la descripción de un consumidor (marcados con QT_TRANSLATE_NOOP), traducido.
std::string etiqueta(const std::string& s) { return ba::i18n::traducir("reconocedores", s.c_str()); }
} // namespace

const std::vector<Reconocedores::Consumidor>& Reconocedores::consumidores() {
    static const std::vector<Consumidor> c{
        {"dictado", QT_TRANSLATE_NOOP("reconocedores", "Dictado del operador"),
         QT_TRANSLATE_NOOP("reconocedores", "Lo que el operador le dice al asistente con F9 o el micrófono de la barra."), false},
        {"transcripcion", QT_TRANSLATE_NOOP("reconocedores", "Transcripción continua y traducción en tiempo real"),
         QT_TRANSLATE_NOOP("reconocedores", "Las tarjetas «Voz a texto» en continuo: subtítulos, acta, disparadores y lo que se traduce."),
         false},
        {"recepcion", QT_TRANSLATE_NOOP("reconocedores", "Recepción (atención hablada)"),
         QT_TRANSLATE_NOOP("reconocedores", "Lo que dice la persona que la recepción atiende hablando (lobby o sesión remota)."), false},
        {"registro", QT_TRANSLATE_NOOP("reconocedores", "Tótem de registro"),
         QT_TRANSLATE_NOOP("reconocedores",
                           "Lo que contesta cada persona en los tótems (varias a la vez). Con Nemotron, puede "
                           "interrumpir al asistente hablando."),
         true},
    };
    return c;
}

const Reconocedores::Consumidor* Reconocedores::consumidor(const std::string& id) {
    for (const Consumidor& c : consumidores())
        if (c.id == id) return &c;
    return nullptr;
}

const std::vector<MotorOido>& Reconocedores::motores() {
    static const std::vector<MotorOido> m{MotorOido::Parakeet, MotorOido::Nemotron};
    return m;
}

std::string Reconocedores::nombre(MotorOido m) { return m == MotorOido::Nemotron ? "nemotron" : "parakeet"; }

std::string Reconocedores::titulo(MotorOido m) {
    return m == MotorOido::Nemotron ? ba::i18n::traducir("reconocedores", "Nemotron 3.5 (streaming)")
                                    : ba::i18n::traducir("reconocedores", "Parakeet TDT v3 (por frases)");
}

std::optional<MotorOido> Reconocedores::deNombre(const std::string& s) {
    if (s == "parakeet") return MotorOido::Parakeet;
    if (s == "nemotron") return MotorOido::Nemotron;
    return std::nullopt;
}

// Medido a ojo con los int8 del catálogo: los pesos (0,62 y 0,68 GB) más lo que
// reserva ONNX Runtime para correrlos. Es para el aviso, no una cuenta exacta.
int Reconocedores::memoriaMB(MotorOido m) { return m == MotorOido::Nemotron ? 900 : 800; }

std::string Reconocedores::consumidorDeClave(const std::string& clave) {
    if (clave.rfind("registro:", 0) == 0) return "registro";
    if (clave.rfind("atencion:", 0) == 0) return "recepcion";
    if (clave == "dictado") return "dictado";
    return "transcripcion";
}

std::string Reconocedores::idiomaDe(const std::string& consumidor) {
    return consumidor == "dictado" ? ba::i18n::actual().codigo : ba::i18n::idiomaDelEvento();
}

bool Reconocedores::reconoce(MotorOido m, const std::string& idioma) {
    return voz_idioma::reconoce(nombre(m), idioma);
}

MotorOido Reconocedores::deFabrica(const std::string& consumidor) {
    const std::string idioma = idiomaDe(consumidor);
    if (reconoce(kDeFabrica, idioma)) return kDeFabrica;
    for (MotorOido m : motores())
        if (reconoce(m, idioma)) return m;
    return kDeFabrica;
}

Reconocedores::Reconocedores(std::filesystem::path archivo) : archivo_(std::move(archivo)) {
    for (MotorOido m : motores()) motores_[m] = {};
    leer();
}

MotorOido Reconocedores::motorDe(const std::string& consumidor) const {
    const auto it = elegidos_.find(consumidor);
    return it == elegidos_.end() ? deFabrica(consumidor) : it->second;
}

std::string Reconocedores::elegir(const std::string& consumidor, const std::string& motor) {
    if (Reconocedores::consumidor(consumidor) == nullptr)
        return ba::i18n::llenar(ba::i18n::traducir("reconocedores", "no hay un consumidor «%1»"), {consumidor});
    const auto m = deNombre(motor);
    if (!m) return ba::i18n::traducir("reconocedores", "el motor es «parakeet» o «nemotron»");
    if (motorDe(consumidor) == *m) return {};
    if (*m == deFabrica(consumidor))
        elegidos_.erase(consumidor);
    else
        elegidos_[consumidor] = *m;
    guardar();
    return {};
}

bool Reconocedores::interrumpirElegido(const std::string& consumidor) const {
    const auto it = interrumpir_.find(consumidor);
    return it == interrumpir_.end() ? true : it->second;
}

bool Reconocedores::interrumpir(const std::string& consumidor) const {
    const Consumidor* c = Reconocedores::consumidor(consumidor);
    return c != nullptr && c->interrumpible && motorDe(consumidor) == MotorOido::Nemotron &&
           interrumpirElegido(consumidor);
}

std::string Reconocedores::setInterrumpir(const std::string& consumidor, bool on) {
    const Consumidor* c = Reconocedores::consumidor(consumidor);
    if (c == nullptr)
        return ba::i18n::llenar(ba::i18n::traducir("reconocedores", "no hay un consumidor «%1»"), {consumidor});
    if (!c->interrumpible)
        return ba::i18n::llenar(
            ba::i18n::traducir("reconocedores", "%1 no se puede interrumpir (no tiene cancelación de eco)"),
            {etiqueta(c->titulo)});
    if (interrumpirElegido(consumidor) == on) return {};
    if (on)
        interrumpir_.erase(consumidor);
    else
        interrumpir_[consumidor] = false;
    guardar();
    return {};
}

int Reconocedores::usos(MotorOido m) const {
    return static_cast<int>(
        std::count_if(usos_.begin(), usos_.end(), [m](const auto& kv) { return kv.second.motor == m; }));
}

bool Reconocedores::enUso(MotorOido m) const { return usos(m) > 0; }

std::set<std::string> Reconocedores::consumidoresDe(MotorOido m) const {
    std::set<std::string> out;
    for (const auto& [k, u] : usos_)
        if (u.motor == m) out.insert(u.consumidor);
    return out;
}

void Reconocedores::pedirCarga(MotorOido m) {
    EstadoMotor& e = motores_[m];
    if (e.cargado || e.cargando) return;
    e.cargando = true;
    e.error.clear();
    if (acciones_.cargar) acciones_.cargar(m);
}

MotorOido Reconocedores::usar(const std::string& clave, const std::string& consumidor, std::int64_t ahoraMs) {
    const MotorOido m = motorDe(consumidor);
    if (const auto it = usos_.find(clave); it != usos_.end()) {
        if (it->second.motor == m && it->second.consumidor == consumidor) return m;
        soltar(clave, ahoraMs); // cambió de motor: se suelta el viejo (con su margen)
    }
    usos_[clave] = Uso{consumidor, m};
    motores_[m].sinUsoDesde = -1;
    pedirCarga(m);
    return m;
}

void Reconocedores::soltar(const std::string& clave, std::int64_t ahoraMs) {
    const auto it = usos_.find(clave);
    if (it == usos_.end()) return;
    const MotorOido m = it->second.motor;
    usos_.erase(it);
    if (usos(m) == 0) motores_[m].sinUsoDesde = ahoraMs;
}

std::optional<MotorOido> Reconocedores::motorDeUso(const std::string& clave) const {
    const auto it = usos_.find(clave);
    if (it == usos_.end()) return std::nullopt;
    return it->second.motor;
}

std::vector<std::string> Reconocedores::usosDe(const std::string& consumidor) const {
    std::vector<std::string> out;
    for (const auto& [k, u] : usos_)
        if (u.consumidor == consumidor) out.push_back(k);
    return out;
}

bool Reconocedores::tic(std::int64_t ahoraMs) {
    bool algo = false;
    for (auto& [m, e] : motores_) {
        if (usos(m) > 0 || e.sinUsoDesde < 0) continue;
        if (!e.cargado && !e.cargando) continue;
        if (ahoraMs - e.sinUsoDesde < margenMs_) continue;
        // Nadie lo usó en todo el margen: se suelta la memoria.
        e.cargado = false;
        e.cargando = false;
        e.sinUsoDesde = -1;
        algo = true;
        if (acciones_.descargar) acciones_.descargar(m);
    }
    return algo;
}

void Reconocedores::informarCarga(MotorOido m, bool ok, const std::string& error) {
    EstadoMotor& e = motores_[m];
    // Si mientras cargaba se descargó (nadie lo usaba), lo que llega tarde no cuenta.
    if (!e.cargando) {
        if (ok && usos(m) == 0 && acciones_.descargar) acciones_.descargar(m);
        return;
    }
    e.cargando = false;
    e.cargado = ok;
    e.error = ok ? std::string() : error;
}

void Reconocedores::recargar(MotorOido m) {
    EstadoMotor& e = motores_[m];
    if (usos(m) == 0) {
        if (e.cargado || e.cargando) {
            e.cargado = e.cargando = false;
            e.sinUsoDesde = -1;
            if (acciones_.descargar) acciones_.descargar(m);
        }
        return;
    }
    e.cargado = false;
    e.cargando = false;
    pedirCarga(m);
}

bool Reconocedores::cargado(MotorOido m) const {
    const auto it = motores_.find(m);
    return it != motores_.end() && it->second.cargado;
}

bool Reconocedores::cargando(MotorOido m) const {
    const auto it = motores_.find(m);
    return it != motores_.end() && it->second.cargando;
}

std::string Reconocedores::error(MotorOido m) const {
    const auto it = motores_.find(m);
    return it == motores_.end() ? std::string() : it->second.error;
}

std::string Reconocedores::alerta() const {
    const std::set<std::string> p = consumidoresDe(MotorOido::Parakeet);
    const std::set<std::string> n = consumidoresDe(MotorOido::Nemotron);
    if (p.empty() || n.empty()) return {};
    const auto lista = [](const std::set<std::string>& ids) {
        std::string s;
        for (const std::string& id : ids) {
            const Consumidor* c = consumidor(id);
            if (!s.empty()) s += ", ";
            s += c != nullptr ? etiqueta(c->titulo) : id;
        }
        return s;
    };
    const int total = memoriaMB(MotorOido::Parakeet) + memoriaMB(MotorOido::Nemotron);
    return ba::i18n::llenar(ba::i18n::traducir("reconocedores",
                                               "Están en uso los dos reconocedores de voz a la vez: Parakeet (%1) y "
                                               "Nemotron (%2). Son dos modelos en memoria, ~%3 MB entre los dos. Si "
                                               "todos usan el mismo, queda uno solo."),
                            {lista(p), lista(n), std::to_string(total)});
}

std::string Reconocedores::avisoIdioma() const {
    std::string s;
    for (const Consumidor& c : consumidores()) {
        const MotorOido m = motorDe(c.id);
        const std::string idioma = idiomaDe(c.id);
        if (reconoce(m, idioma)) continue;
        const ba::i18n::Perfil p = ba::i18n::perfil(idioma);
        std::string otro;
        for (MotorOido x : motores())
            if (x != m && reconoce(x, idioma)) otro = titulo(x);
        if (!s.empty()) s += " ";
        s += otro.empty()
                 ? ba::i18n::llenar(ba::i18n::traducir("reconocedores",
                                                       "%1: %2 no entiende %3, y ningún otro reconocedor lo entiende."),
                                    {etiqueta(c.titulo), titulo(m), p.nombre.empty() ? idioma : p.nombre})
                 : ba::i18n::llenar(ba::i18n::traducir("reconocedores",
                                                       "%1: %2 no entiende %3; %4 sí (elegilo en la pestaña Agentes)."),
                                    {etiqueta(c.titulo), titulo(m), p.nombre.empty() ? idioma : p.nombre, otro});
    }
    return s;
}

json Reconocedores::estado() const {
    json ms = json::array();
    int memoria = 0;
    for (MotorOido m : motores()) {
        const EstadoMotor& e = motores_.at(m);
        json cons = json::array();
        for (const std::string& c : consumidoresDe(m)) cons.push_back(c);
        if (e.cargado || e.cargando) memoria += memoriaMB(m);
        ms.push_back({{"id", nombre(m)},
                      {"titulo", titulo(m)},
                      {"cargado", e.cargado},
                      {"cargando", e.cargando},
                      {"error", e.error},
                      {"usos", usos(m)},
                      {"consumidores", cons},
                      {"memoria_mb", memoriaMB(m)}});
    }
    json cs = json::array();
    std::set<MotorOido> elegidos;
    for (const Consumidor& c : consumidores()) {
        elegidos.insert(motorDe(c.id));
        cs.push_back({{"id", c.id},
                      {"titulo", etiqueta(c.titulo)},
                      {"descripcion", etiqueta(c.descripcion)},
                      {"motor", nombre(motorDe(c.id))},
                      {"elegido", elegidos_.count(c.id) > 0},
                      {"idioma", idiomaDe(c.id)},
                      {"reconoce", reconoce(motorDe(c.id), idiomaDe(c.id))},
                      {"usos", usosDe(c.id).size()},
                      {"interrumpible", c.interrumpible},
                      {"interrumpir", interrumpirElegido(c.id)}});
    }
    return {{"motores", ms},
            {"consumidores", cs},
            {"alerta", alerta()},
            {"aviso_idioma", avisoIdioma()},
            // Elegidos distintos: si llegan a usarse a la vez, se cargan los dos.
            {"mezcla", elegidos.size() > 1},
            {"memoria_mb", memoria},
            {"margen_s", margenMs_ / 1000}};
}

void Reconocedores::leer() {
    if (archivo_.empty()) return;
    std::ifstream in(archivo_, std::ios::binary);
    if (!in) return;
    const json o = json::parse(in, nullptr, false);
    if (!o.is_object()) return;
    // En variables: un json temporal en el `for` (`o.value(...).items()`) muere antes de recorrerlo.
    const json motores = o.value("motores", json::object());
    const json interrumpir = o.value("interrumpir", json::object());
    for (const auto& [k, v] : motores.items())
        if (v.is_string() && consumidor(k) != nullptr)
            if (const auto m = deNombre(v.get<std::string>())) elegidos_[k] = *m; // elegido a mano: manda
    for (const auto& [k, v] : interrumpir.items())
        if (v.is_boolean() && !v.get<bool>() && consumidor(k) != nullptr) interrumpir_[k] = false;
}

void Reconocedores::guardar() const {
    if (archivo_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    json motores = json::object(), interr = json::object();
    for (const auto& [k, m] : elegidos_) motores[k] = nombre(m);
    for (const auto& [k, on] : interrumpir_) interr[k] = on;
    std::ofstream out(archivo_, std::ios::binary | std::ios::trunc);
    out << json{{"motores", motores}, {"interrumpir", interr}}.dump(2) << "\n";
}

} // namespace ba::app::agent
