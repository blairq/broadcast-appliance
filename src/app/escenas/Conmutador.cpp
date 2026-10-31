// src/app/escenas/Conmutador.cpp — el conmutador de escenas de cada salida (ver el .hpp)

#include "escenas/Conmutador.hpp"

#include "i18n/I18n.hpp"

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"

#include <QPointer>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <fstream>

namespace ba::app::escenas {

namespace {
constexpr const char* kPorOmision = BroadcastEngine::kSalidaPorOmision;
}

// ============================================================================
// Conmutador (uno por salida)
// ============================================================================

Conmutador::Conmutador(AppRuntime& rt, Conmutadores& registro, std::string salida, Afv afv, QObject* parent)
    : QObject(parent), rt_(rt), registro_(registro), salida_(std::move(salida)), afv_(std::move(afv)) {}

std::string Conmutador::alAire() const { return rt_.engine().currentScene(salida_); }

std::string Conmutador::armada() const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    return armada_;
}

double Conmutador::tbarPosicion() const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    return tbarPos_;
}

void Conmutador::avisarEnDueno(std::function<void(Conmutador&)> emitir) {
    // Los view models son del dueño: lo que hizo el plano de control les llega encolado.
    // Si mientras tanto la salida se borró (el conmutador se fue), no hay a quién avisar.
    rt_.enDueno([yo = QPointer<Conmutador>(this), emitir = std::move(emitir)]() {
        if (yo) emitir(*yo);
    });
}

std::string Conmutador::cortar(const std::string& escena, const std::string& tipoPedido, int msPedido, const char* via) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    const std::string antes = alAire();
    std::string tipo = tipoPedido;
    int ms = msPedido;
    // Sin pedido explícito: la transición de entrada de la escena (si tiene).
    if (tipo.empty()) {
        const nlohmann::json e = registro_.transicionEntrada(escena);
        if (!e.empty()) {
            tipo = e.value("tipo", std::string());
            ms = e.value("ms", -1);
        }
    }
    const bool ok = tipo.empty() ? rt_.setScene(escena, salida_) : rt_.setSceneCon(escena, tipo, ms, salida_);
    if (!ok)
        return rt_.lastError().empty()
                   ? ba::i18n::llenar(ba::i18n::traducir("aire", "no se pudo poner «%1» al aire"), {escena})
                   : rt_.lastError();
    // Cada cambio del aire al registro, con por dónde vino: «la cambia y vuelve sola»
    // se ve acá (quién la devolvió y cuándo). Las salidas que no son la por omisión, con su id.
    const std::string que = salida_ == kPorOmision ? via : std::string(via) + " · " + salida_;
    ba::diag::registro().linea(ba::diag::Nivel::Info, "aire", 0,
                               tipo.empty() ? ba::i18n::llenar(ba::i18n::traducir("aire", "al aire: «%1» (salió «%2», por %3)"),
                                                               {escena, antes, que})
                                            : ba::i18n::llenar(ba::i18n::traducir("aire", "al aire: «%1» (salió «%2», por %3, %4)"),
                                                               {escena, antes, que, tipo}));
    aplicarAfv(escena, tipo == "cut" ? 0 : (ms < 0 ? rt_.ajustesAire().transicionMs : ms));
    rt_.save(); // la escena al aire vuelve igual tras un corte de luz
    avisarEnDueno([e = QString::fromStdString(escena)](Conmutador& c) { emit c.alAireCambio(e); });
    return {};
}

std::string Conmutador::poner(const std::string& escena, const std::string& tipo, int ms) {
    return cortar(escena, tipo, ms, ba::i18n::traducir("aire", "corte directo").c_str());
}

std::string Conmutador::armar(const std::string& escena) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    if (escena == alAire())
        // Armar lo que ya está al aire deja el TAKE sin sentido (cortaría a lo mismo).
        return ba::i18n::llenar(ba::i18n::traducir("aire", "«%1» ya está al aire: armá otra para el próximo take."), {escena});
    const auto nombres = rt_.engine().sceneNames();
    if (!escena.empty() && std::find(nombres.begin(), nombres.end(), escena) == nombres.end())
        return ba::i18n::llenar(ba::i18n::traducir("aire", "no existe la escena «%1»"), {escena});
    setArmadaYAvisar(escena);
    return {};
}

std::string Conmutador::take(const std::string& tipo, int ms) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    if (armada_.empty()) return ba::i18n::traducir("aire", "No hay ninguna escena armada: elegí una en el bus de preview.");
    const std::string entra = armada_;
    const std::string sale = alAire();
    if (std::string e = cortar(entra, tipo, ms, "TAKE"); !e.empty()) return e;
    // El plano que sale del aire queda armado: en una mesa, después de un take
    // el preview tiene lo anterior y volver es un solo golpe.
    setArmadaYAvisar(sale);
    return {};
}

std::string Conmutador::tbar(double t, bool soltar) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    if (armada_.empty()) return ba::i18n::traducir("aire", "No hay ninguna escena armada para la T-bar.");
    const std::string entra = armada_;
    const std::string sale = alAire();
    bool llego = false;
    if (!rt_.engine().transicionManual(entra, static_cast<float>(t), soltar, &llego, salida_)) return rt_.engine().lastError();
    tbarPos_ = soltar || llego ? 0.0 : std::clamp(t, 0.0, 1.0);
    if (llego) {
        rt_.anotarEscenaAlAire(entra, salida_);
        aplicarAfv(entra, 0);
        rt_.save();
        avisarEnDueno([e = QString::fromStdString(entra)](Conmutador& c) { emit c.alAireCambio(e); });
        setArmadaYAvisar(sale);
    }
    return {};
}

void Conmutador::setArmadaYAvisar(const std::string& escena) {
    if (escena == armada_) return;
    armada_ = escena;
    // El bus de preview de la salida en el motor (las vistas solo lo miran).
    if (!rt_.engine().armar(escena, salida_))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "aire", 0, rt_.engine().lastError());
    avisarEnDueno([e = QString::fromStdString(escena)](Conmutador& c) { emit c.armadaCambio(e); });
}

void Conmutador::aplicarAfv(const std::string& destino, int ms) {
    if (afv_.fuentes.empty()) return;
    bool recordo = false;
    // El fader de cada fuente EN ESTA salida (la mezcla de su bus).
    const auto fundidos = afv_.alCortar(rt_.engine().sceneSources(destino),
                                        [this](const std::string& f) { return rt_.engine().sourceVolume(f, salida_); }, &recordo);
    // Fundido en pasos de 30 ms, con la duración de la transición, en el hilo que cortó (el
    // plano de control no espera a que la interfaz se destrabe). Si la salida se borra en
    // el medio, el motor dice que no existe y listo.
    const int pasos = std::max(1, ms / 30);
    for (const auto& f : fundidos)
        for (int i = 1; i <= pasos; ++i) {
            const float v = f.desde + (f.hasta - f.desde) * static_cast<float>(i) / static_cast<float>(pasos);
            QTimer::singleShot(i * 30, [&rt = rt_, salida = salida_, n = f.fuente, v]() { rt.engine().setSourceVolume(n, v, salida); });
        }
    if (recordo) registro_.guardar();
}

std::string Conmutador::setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms) {
    return registro_.setTransicionEntrada(escena, tipo, ms);
}

nlohmann::json Conmutador::transicionEntrada(const std::string& escena) const { return registro_.transicionEntrada(escena); }

void Conmutador::setAfv(const std::string& fuente, bool sigue) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    if (sigue)
        afv_.fuentes.insert(fuente);
    else
        afv_.fuentes.erase(fuente);
    registro_.guardar();
    avisarEnDueno([](Conmutador& c) { emit c.afvCambio(); });
}

bool Conmutador::afv(const std::string& fuente) const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    return afv_.fuentes.count(fuente) > 0;
}

std::vector<std::string> Conmutador::afvFuentes() const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    return {afv_.fuentes.begin(), afv_.fuentes.end()};
}

Afv Conmutador::afvEstado() const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    return afv_;
}

// ============================================================================
// Conmutadores (el registro)
// ============================================================================

Conmutadores::Conmutadores(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    std::ifstream f(rt_.dataRoot() / "Config" / "transiciones.json");
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_object()) {
        transicionesEntrada_ = j.value("entrada", nlohmann::json::object());
        afvGuardado_[kPorOmision] = Afv::deJson(j); // lo de siempre: el de la por omisión
        const nlohmann::json otras = j.value("salidas", nlohmann::json::object()); // .items() de un temporal: UB
        for (const auto& [id, x] : otras.items())
            if (id != kPorOmision) afvGuardado_[id] = Afv::deJson(x);
    }
    porOmision_ = crear(kPorOmision);
    sincronizar();
    // Las salidas se crean y se borran en el runtime: acá se sigue lo que haya.
    observador_ = rt_.observar([this](AppRuntime::Cambio c) {
        if (c == AppRuntime::Cambio::Salidas) sincronizar();
    });
}

Conmutadores::~Conmutadores() {
    if (observador_ >= 0) rt_.dejarDeObservar(observador_);
}

Conmutador* Conmutadores::crear(const std::string& salida) {
    const auto g = afvGuardado_.find(salida);
    auto c = std::make_unique<Conmutador>(rt_, *this, salida, g != afvGuardado_.end() ? g->second : Afv{});
    Conmutador* p = c.get();
    conmutadores_[salida] = std::move(c);
    emit creado(p);
    return p;
}

Conmutador* Conmutadores::de(const std::string& salida) {
    // Con el turno: el mapa lo cambia sincronizar, también con él (y quien usa el que
    // devuelve desde el plano de control lo tiene por toda su operación: no se le va).
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    const auto it = conmutadores_.find(salida.empty() ? std::string(kPorOmision) : salida);
    return it == conmutadores_.end() ? nullptr : it->second.get();
}

std::vector<Conmutador*> Conmutadores::todos() const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    std::vector<Conmutador*> out;
    for (const std::string& id : rt_.engine().salidaIds())
        if (const auto it = conmutadores_.find(id); it != conmutadores_.end()) out.push_back(it->second.get());
    return out;
}

void Conmutadores::sincronizar() {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    const std::vector<std::string> ids = rt_.engine().salidaIds();
    bool cambio = false;
    for (const std::string& id : ids)
        if (conmutadores_.count(id) == 0) {
            crear(id);
            cambio = true;
        }
    for (auto it = conmutadores_.begin(); it != conmutadores_.end();) {
        if (it->first == kPorOmision || std::find(ids.begin(), ids.end(), it->first) != ids.end()) {
            ++it;
            continue;
        }
        // Su AFV queda guardado: si la salida vuelve, vuelve como estaba.
        afvGuardado_[it->first] = it->second->afvEstado();
        it->second.release()->deleteLater(); // puede estar en medio de una de sus señales
        it = conmutadores_.erase(it);
        cambio = true;
    }
    if (cambio) emit this->cambio();
}

nlohmann::json Conmutadores::transicionEntrada(const std::string& escena) const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    if (!transicionesEntrada_.contains(escena)) return nlohmann::json::object();
    return transicionesEntrada_[escena];
}

std::string Conmutadores::setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms) {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    static const std::vector<std::string> tipos{"", "cut", "fade", "swipe", "slide", "fade_to_color"};
    if (std::find(tipos.begin(), tipos.end(), tipo) == tipos.end())
        return ba::i18n::traducir("aire", "tipo: cut, fade, swipe, slide o fade_to_color (vacío = la global)");
    if (tipo.empty())
        transicionesEntrada_.erase(escena);
    else
        transicionesEntrada_[escena] = {{"tipo", tipo}, {"ms", ms}};
    guardar();
    return {};
}

void Conmutadores::guardar() const {
    const TurnoMotor::Guarda turno(rt_.engine().turno());
    // El AFV de cada salida: el de las que andan y el guardado de las que no están.
    std::map<std::string, Afv> afv = afvGuardado_;
    for (const auto& [id, c] : conmutadores_) afv[id] = c->afvEstado();
    nlohmann::json j = afv[kPorOmision].aJson(); // lo de siempre: un arranque viejo lo sigue entendiendo
    j["entrada"] = transicionesEntrada_;
    nlohmann::json otras = nlohmann::json::object();
    for (const auto& [id, a] : afv)
        if (id != kPorOmision) otras[id] = a.aJson();
    if (!otras.empty()) j["salidas"] = otras;
    // Aparte y reemplazar: un corte a mitad no deja el archivo trunco.
    QSaveFile o(QString::fromStdString((rt_.dataRoot() / "Config" / "transiciones.json").string()));
    if (!o.open(QIODevice::WriteOnly)) return;
    o.write(QByteArray::fromStdString(j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
    o.commit();
}

} // namespace ba::app::escenas
