// ============================================================================
// src/app/emision/RetornoGateway.cpp — ver RetornoGateway.hpp
// ============================================================================

#include "emision/RetornoGateway.hpp"

#include <algorithm>
#include <cstring>

#include "Motor.hpp"
#include "diag/Registro.hpp"
#include "emision/EscenasYFuentes.hpp"
#include "emision/SalidaMotor.hpp"
#include "i18n/I18nTexto.hpp"
#include "ipc/CanalesSalida.hpp"
#include "ipc/HealthBlock.hpp" // nowQpc() para los timestamps del IPC

namespace ba::app {

namespace {
constexpr const wchar_t* kProgramVideoName = L"BA.Program";
} // namespace

std::string RetornoDeSalida::clave() const {
    return porOmision ? std::string("programa@gateway") : "programa:" + id + "@gateway";
}

std::wstring RetornoDeSalida::nombreCanal() const {
    return porOmision ? std::wstring(kProgramVideoName) : ba::ipc::nombreCanalSalida(id);
}

bool RetornoGateway::publishReturnFeeds() {
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "publishReturnFeeds sin startup"));
        return false;
    }
    if (returnFeedsActive_) return true;
    // El audio de cada salida (BA.Audio.ContentForReturn, BA.Audio.Salida.<id>) lo
    // escribe el motor desde que la arma.
    bool ok = true;
    for (const auto& s : m_.lasSalidas()) ok = publicarRetorno(s->retorno) && ok;
    if (!ok) return false;
    returnFeedsActive_ = true;
    return true;
}

void RetornoGateway::ponerTopeRetorno(int ancho, int alto) {
    if (ancho <= 0 || alto <= 0 || (ancho == topeRetornoAncho_ && alto == topeRetornoAlto_)) return;
    topeRetornoAncho_ = ancho;
    topeRetornoAlto_ = alto;
    if (!returnFeedsActive_) return;
    // Andando: cada salida rehace su canal y su vista al tamaño nuevo.
    for (const auto& s : m_.lasSalidas()) {
        soltarVistaRetorno(*s->retorno);
        publicarRetorno(s->retorno);
    }
}

ba::ipc::SlotConfig RetornoGateway::canalRetorno() const {
    // Encajada en el tope, con el aspecto de la salida y sin agrandarla (agrandar no
    // agrega detalle, solo bytes). Par: lo piden los conversores a I420.
    const VideoConfig& v = m_.video_;
    const int ow = std::max(2, v.outputWidth), oh = std::max(2, v.outputHeight);
    double k = std::min({1.0, static_cast<double>(topeRetornoAncho_) / ow, static_cast<double>(topeRetornoAlto_) / oh});
    ba::ipc::SlotConfig c{std::max(2, static_cast<int>(ow * k) & ~1), std::max(2, static_cast<int>(oh * k) & ~1)};
    // El período del lienzo: el gateway duerme hasta el próximo cuadro (ipc/Mirada.hpp, F2-14).
    if (v.fpsNum > 0 && v.fpsDen > 0)
        c.periodoNs = static_cast<std::uint64_t>(v.fpsDen) * 1'000'000'000ULL / static_cast<std::uint64_t>(v.fpsNum);
    return c;
}

bool RetornoGateway::publicarRetorno(const std::shared_ptr<RetornoDeSalida>& r) {
    const ba::ipc::SlotConfig vcfg = canalRetorno();
    {
        std::lock_guard<std::mutex> l(r->mu);
        // Un canal publicado no cambia de tamaño (ver SpoutSender::publish): con otro
        // lienzo (resetVideo) se rehace, y el gateway se vuelve a suscribir solo al ver
        // otra instancia, como con los canales de escena. Antes se intentaba publicar
        // otra vez el mismo y fallaba: la salida al gateway quedaba colgada.
        if (r->canal.published() && (r->ancho != vcfg.width || r->alto != vcfg.height)) r->canal = ba::ipc::SpoutSender();
        if (!r->canal.published()) {
            if (!r->canal.publish(r->nombreCanal().c_str(), vcfg)) {
                m_.setError(r->porOmision ? ba::i18n::traducir("emision", "no se pudo publicar el canal de video de programa")
                                          : ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo publicar el canal de video de la salida «%1»"),
                                                             {r->id}));
                return false;
            }
            r->ancho = vcfg.width;
            r->alto = vcfg.height;
        }
    }
    return !r->enUso || pedirVistaRetorno(r);
}

bool RetornoGateway::pedirVistaRetorno(const std::shared_ptr<RetornoDeSalida>& r) {
    if (!m_.motor_ || r->pedido) return true;
    const ba::ipc::SlotConfig tam = canalRetorno();
    const std::string e = m_.motor_->verPedir(r->clave(), tam.width, tam.height);
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    // El retorno va en el aviso: vive mientras el motor pueda llamarlo (verAvisar con
    // nullptr lo suelta antes de volver).
    m_.motor_->verAvisar(r->clave(), [this, r](const motor::Cuadro& c) { enviarRetorno(*r, c); });
    r->pedido = true;
    return true;
}

void RetornoGateway::soltarVistaRetorno(RetornoDeSalida& r) {
    if (!r.pedido || !m_.motor_) return;
    m_.motor_->verAvisar(r.clave(), nullptr);
    m_.motor_->verSoltar(r.clave());
    r.pedido = false;
}

void RetornoGateway::soltarRetorno(RetornoDeSalida& r) {
    soltarVistaRetorno(r);
    std::lock_guard<std::mutex> l(r.mu);
    r.canal = ba::ipc::SpoutSender();
}

void RetornoGateway::enviarRetorno(RetornoDeSalida& r, const motor::Cuadro& c) {
    // Los bytes son los del buffer de la vista (sin copiar): la única copia es al canal.
    std::lock_guard<std::mutex> l(r.mu);
    if (!r.canal.published() || c.ancho != r.ancho || c.alto != r.alto) return;
    const std::size_t fila = static_cast<std::size_t>(c.ancho) * 4;
    const std::uint8_t* px = c.bgra.data();
    if (static_cast<std::size_t>(c.stride) != fila) {
        r.filas.resize(fila * static_cast<std::size_t>(c.alto));
        for (int y = 0; y < c.alto; ++y)
            std::memcpy(r.filas.data() + fila * y, c.bgra.data() + static_cast<std::size_t>(c.stride) * y, fila);
        px = r.filas.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++r.cuadros;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    meta.contenidoQpc = c.contenidoQpc; // para alinearlo con su audio en el gateway (F2-11)
    r.canal.send(px, fila * static_cast<std::size_t>(c.alto), meta);
}

void RetornoGateway::retornoEnUso(bool si, const std::string& salida) {
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s || s->retorno->enUso == si) return;
    s->retorno->enUso = si;
    if (!returnFeedsActive_ || !m_.motor_) return;
    if (si)
        pedirVistaRetorno(s->retorno);
    else
        soltarVistaRetorno(*s->retorno);
}

bool RetornoGateway::escenaACanal(int k, const std::string& nombre) {
    if (k < 0 || k >= ba::ipc::kCanalesEscena) {
        m_.setError(ba::i18n::traducir("emision", "canal de escena fuera de rango"));
        return false;
    }
    CanalEscena& c = canalesEscena_[static_cast<std::size_t>(k)];
    // La escena por su id (renombrarla no rehace el canal); escenaDeCanal dice el nombre.
    const std::string id = m_.escenas_->idDe(nombre);
    if (!nombre.empty() && c.escena == id) return true;
    if (!c.clave.empty() && m_.motor_) {
        m_.motor_->verAvisar(c.clave, nullptr);
        m_.motor_->verSoltar(c.clave);
    }
    {
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.escena.clear();
        c.clave.clear();
    }
    if (nombre.empty()) return true;
    if (!m_.started_ || !m_.motor_) {
        m_.setError(ba::i18n::traducir("emision", "escena para pantallas: el motor no arranco"));
        return false;
    }
    if (m_.escenas_->escena(nombre) == nullptr) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "escena para pantallas: no existe '%1'"), {nombre}));
        return false;
    }
    // Lo que el gateway le manda a un televisor es a lo sumo 1280x720 (ver
    // MediaPump::Cfg::programVideoW): componer más grande solo gasta.
    const int w = std::min(1280, m_.video_.baseWidth);
    const int h = std::max(2, (w * m_.video_.baseHeight / std::max(1, m_.video_.baseWidth)) & ~1);
    if (!c.sender || c.ancho != w || c.alto != h) {
        // Un canal publicado no cambia de tamaño (ver SpoutSender::publish):
        // con otro lienzo se rehace, y el gateway se vuelve a suscribir solo.
        auto s = std::make_unique<ba::ipc::SpoutSender>();
        if (!s->publish(ba::ipc::nombreCanalEscena(k).c_str(), ba::ipc::SlotConfig{w, h})) {
            m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo publicar el canal de escena %1"), {std::to_string(k)}));
            return false;
        }
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.sender = std::move(s);
        c.ancho = w;
        c.alto = h;
    }
    const std::string clave = "escena:" + id + "@pantallas" + std::to_string(k);
    const std::string e = m_.motor_->verPedir(clave, w, h);
    if (!e.empty()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "escena para pantallas: %1"), {e}));
        return false;
    }
    {
        std::lock_guard<std::mutex> l(canalesEscenaM_);
        c.escena = id;
        c.clave = clave;
    }
    m_.motor_->verAvisar(clave, [this, k](const motor::Cuadro& q) { enviarEscena(k, q); });
    diag::registro().linea(diag::Nivel::Info, "pantallas", 0,
                           ba::i18n::llenar(ba::i18n::traducir("pantallas", "escena «%1» en el canal %2 (%3x%4)"), {nombre, std::to_string(k), std::to_string(w), std::to_string(h)}));
    return true;
}

std::string RetornoGateway::escenaDeCanal(int k) const {
    if (k < 0 || k >= ba::ipc::kCanalesEscena) return {};
    return m_.escenas_->nombreDe(canalesEscena_[static_cast<std::size_t>(k)].escena);
}

int RetornoGateway::tomarCanalEscena(const std::string& escena, const std::string& dueno) {
    if (escena.empty()) return -1;
    int canal = -1;
    for (int k = 0; k < ba::ipc::kCanalesEscena && canal < 0; ++k)
        if (escenaDeCanal(k) == escena) canal = k;
    for (int k = 0; k < ba::ipc::kCanalesEscena && canal < 0; ++k)
        if (escenaDeCanal(k).empty()) canal = k;
    if (canal < 0) return -1;
    if (escenaDeCanal(canal).empty() && !escenaACanal(canal, escena)) return -1;
    duenosEscena_[static_cast<std::size_t>(canal)].insert(dueno);
    return canal;
}

void RetornoGateway::soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo) {
    for (int k = 0; k < ba::ipc::kCanalesEscena; ++k) {
        if (salvo.count(k) > 0) continue;
        auto& d = duenosEscena_[static_cast<std::size_t>(k)];
        if (d.erase(dueno) > 0 && d.empty()) escenaACanal(k, ""); // Pantallas lo vuelve a pedir si lo usa
    }
}

bool RetornoGateway::canalEscenaConDuenos(int k) const {
    return k >= 0 && k < ba::ipc::kCanalesEscena && !duenosEscena_[static_cast<std::size_t>(k)].empty();
}

void RetornoGateway::enviarEscena(int k, const motor::Cuadro& q) {
    std::lock_guard<std::mutex> l(canalesEscenaM_);
    CanalEscena& c = canalesEscena_[static_cast<std::size_t>(k)];
    if (c.escena.empty() || !c.sender || q.ancho != c.ancho || q.alto != c.alto) return;
    const std::size_t fila = static_cast<std::size_t>(q.ancho) * 4;
    const std::uint8_t* px = q.bgra.data();
    if (static_cast<std::size_t>(q.stride) != fila) {
        c.filas.resize(fila * static_cast<std::size_t>(q.alto));
        for (int y = 0; y < q.alto; ++y)
            std::memcpy(c.filas.data() + fila * y, q.bgra.data() + static_cast<std::size_t>(q.stride) * y, fila);
        px = c.filas.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++c.cuadros;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    c.sender->send(px, fila * static_cast<std::size_t>(q.alto), meta);
}

void RetornoGateway::stopReturnFeeds() {
    for (int k = 0; k < ba::ipc::kCanalesEscena; ++k) escenaACanal(k, "");
    if (!returnFeedsActive_ || !m_.motor_) return;
    for (const auto& s : m_.lasSalidas()) soltarVistaRetorno(*s->retorno);
    m_.porOmision().retorno->enUso = true; // como al arrancar
    returnFeedsActive_ = false;
}

} // namespace ba::app
