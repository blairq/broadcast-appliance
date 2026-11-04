// src/app/escenas/Conmutador.cpp — el conmutador de escenas de cada salida (ver el .hpp)

#include "escenas/Conmutador.hpp"

#include "arranque/ArchivoConfig.hpp"

#include "i18n/I18nTexto.hpp"

#include "arranque/RuntimeMotor.hpp"
#include "diag/Registro.hpp"

#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace ba::app::escenas {

namespace {
constexpr const char* kPorOmision = BroadcastEngine::kSalidaPorOmision;
}

// ============================================================================
// Conmutador (uno por salida)
// ============================================================================

Conmutador::Conmutador(AppRuntime& rt, Conmutadores& registro, std::string salida, Afv afv, QObject* parent)
    : QObject(parent), rt_(rt), registro_(registro), salida_(std::move(salida)), afv_(std::move(afv)) {
    relojRampas_.setInterval(30);
    connect(&relojRampas_, &QTimer::timeout, this, &Conmutador::pasoRampas);
}

std::string Conmutador::alAire() const { return rt_.engine().currentScene(salida_); }

std::string Conmutador::alAireId() const { return rt_.engine().currentSceneId(salida_); }

std::string Conmutador::armada() const {
    const TurnoMotor::Guarda turno(rt_.turno());
    return rt_.engine().sceneName(armada_);
}

double Conmutador::tbarPosicion() const {
    const TurnoMotor::Guarda turno(rt_.turno());
    return tbarPos_;
}

void Conmutador::avisar(std::function<void(Conmutador&)> emitir, unsigned partesFoto) {
    rt_.fotoSucia(partesFoto);
    // La señal sale en el hilo del conmutador (el dueño): en el acto si la operación corre
    // ahí; si no (la interfaz que todavía corta directo), encolada. Si mientras tanto la
    // salida se borró (el conmutador se fue), no hay a quién avisar.
    QMetaObject::invokeMethod(
        this, [yo = QPointer<Conmutador>(this), emitir = std::move(emitir)]() {
            if (yo) emitir(*yo);
        },
        Qt::AutoConnection);
}

std::string Conmutador::cortar(const std::string& pedida, const std::string& tipoPedido, int msPedido, const char* via) {
    const TurnoMotor::Guarda turno(rt_.turno());
    const std::string antes = alAire();
    // Por su id (lo que llega puede ser el nombre o el id); al registro y a la señal, el nombre.
    const std::string id = rt_.engine().sceneId(pedida);
    const std::string escena = id.empty() ? pedida : rt_.engine().sceneName(id);
    const std::string a = id.empty() ? pedida : id;
    std::string tipo = tipoPedido;
    int ms = msPedido;
    // Sin pedido explícito: la transición de entrada de la escena (si tiene).
    if (tipo.empty()) {
        const nlohmann::json e = registro_.transicionEntrada(a);
        if (!e.empty()) {
            tipo = e.value("tipo", std::string());
            ms = e.value("ms", -1);
        }
    }
    const bool ok = tipo.empty() ? rt_.setScene(a, salida_) : rt_.setSceneCon(a, tipo, ms, salida_);
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
    aplicarAfv(a, tipo == "cut" ? 0 : (ms < 0 ? rt_.ajustesAire().transicionMs : ms));
    rt_.save(); // la escena al aire vuelve igual tras un corte de luz
    avisar([e = QString::fromStdString(escena)](Conmutador& c) { emit c.alAireCambio(e); }, FotoMotor::Salidas);
    return {};
}

std::string Conmutador::poner(const std::string& escena, const std::string& tipo, int ms) {
    return cortar(escena, tipo, ms, ba::i18n::traducir("aire", "corte directo").c_str());
}

std::string Conmutador::armar(const std::string& escena) {
    const TurnoMotor::Guarda turno(rt_.turno());
    const std::string id = rt_.engine().sceneId(escena);
    if (!escena.empty() && id.empty()) return ba::i18n::llenar(ba::i18n::traducir("aire", "no existe la escena «%1»"), {escena});
    if (!id.empty() && id == alAireId())
        // Armar lo que ya está al aire deja el TAKE sin sentido (cortaría a lo mismo).
        return ba::i18n::llenar(ba::i18n::traducir("aire", "«%1» ya está al aire: armá otra para el próximo take."),
                                {rt_.engine().sceneName(id)});
    setArmadaYAvisar(id);
    return {};
}

std::string Conmutador::take(const std::string& tipo, int ms) {
    const TurnoMotor::Guarda turno(rt_.turno());
    if (armada_.empty()) return ba::i18n::traducir("aire", "No hay ninguna escena armada: elegí una en el bus de preview.");
    const std::string entra = armada_;
    const std::string sale = alAireId();
    if (std::string e = cortar(entra, tipo, ms, "TAKE"); !e.empty()) return e;
    // El plano que sale del aire queda armado: en una mesa, después de un take
    // el preview tiene lo anterior y volver es un solo golpe.
    setArmadaYAvisar(sale);
    return {};
}

std::string Conmutador::tbar(double t, bool soltar) {
    const TurnoMotor::Guarda turno(rt_.turno());
    if (armada_.empty()) return ba::i18n::traducir("aire", "No hay ninguna escena armada para la T-bar.");
    const std::string entra = armada_;
    const std::string sale = alAireId();
    bool llego = false;
    if (!rt_.engine().transicionManual(entra, static_cast<float>(t), soltar, &llego, salida_)) return rt_.engine().lastError();
    tbarPos_ = soltar || llego ? 0.0 : std::clamp(t, 0.0, 1.0);
    rt_.fotoSucia(FotoMotor::Salidas); // por dónde va la T-bar
    if (llego) {
        rt_.anotarEscenaAlAire(entra, salida_);
        aplicarAfv(entra, 0);
        rt_.save();
        avisar([e = QString::fromStdString(rt_.engine().sceneName(entra))](Conmutador& c) { emit c.alAireCambio(e); },
               FotoMotor::Salidas);
        setArmadaYAvisar(sale);
    }
    return {};
}

void Conmutador::setArmadaYAvisar(const std::string& id) {
    if (id == armada_) return;
    armada_ = id;
    // El bus de preview de la salida en el motor (las vistas solo lo miran).
    if (!rt_.engine().armar(id, salida_))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "aire", 0, rt_.engine().lastError());
    avisar([e = QString::fromStdString(rt_.engine().sceneName(id))](Conmutador& c) { emit c.armadaCambio(e); }, FotoMotor::Salidas);
}

void Conmutador::aplicarAfv(const std::string& destino, int ms) {
    if (afv_.fuentes.empty()) return;
    bool recordo = false;
    // El fader de cada fuente EN ESTA salida (la mezcla de su bus).
    const auto fundidos = afv_.alCortar(rt_.engine().sceneSources(destino),
                                        [this](const std::string& f) { return rt_.engine().sourceVolume(f, salida_); }, &recordo);
    // Fundido con la duración de la transición, uno por fuente: el de un corte anterior que
    // todavía corre se reemplaza (sigue desde donde iba). El reloj es el del conmutador (el
    // dueño) y cada paso va a la agenda del dueño, con el turno (no espera a que la interfaz
    // se destrabe). Si la salida se borra en el medio, el motor dice que no existe y listo.
    const auto ahora = std::chrono::steady_clock::now();
    for (const auto& f : fundidos) rampas_[f.fuente] = Rampa{f.desde, f.hasta, ahora, std::max(0, ms)};
    pasoRampas(); // un corte (0 ms) queda en el acto
    // El reloj, en el hilo del conmutador (el corte puede venir de la interfaz que corta directo).
    if (!rampas_.empty())
        QMetaObject::invokeMethod(
            this, [this]() {
                if (!rampas_.empty() && !relojRampas_.isActive()) relojRampas_.start();
            },
            Qt::AutoConnection);
    if (recordo) registro_.guardar();
}

void Conmutador::pasoRampas() {
    // Con el turno (rampas_ la cambia también un corte); si la interfaz lo tiene, este paso
    // se saltea: el dueño no la espera, y el próximo sigue por la hora.
    const TurnoMotor& turno = rt_.turno();
    if (!turno.tomarHasta(0)) return;
    struct Soltar {
        const TurnoMotor& t;
        ~Soltar() { t.soltar(); }
    } soltar{turno};
    const auto ahora = std::chrono::steady_clock::now();
    for (auto it = rampas_.begin(); it != rampas_.end();) {
        const Rampa& r = it->second;
        const double ms = std::chrono::duration<double, std::milli>(ahora - r.inicio).count();
        const double t = r.ms <= 0 ? 1.0 : std::clamp(ms / r.ms, 0.0, 1.0);
        const float v = r.desde + (r.hasta - r.desde) * static_cast<float>(t);
        rt_.despuesEnDueno([&rt = rt_, salida = salida_, n = it->first, v]() { rt.engine().setSourceVolume(n, v, salida); });
        it = t >= 1.0 ? rampas_.erase(it) : std::next(it);
    }
    if (rampas_.empty()) relojRampas_.stop();
}

std::string Conmutador::setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms) {
    return registro_.setTransicionEntrada(escena, tipo, ms);
}

nlohmann::json Conmutador::transicionEntrada(const std::string& escena) const { return registro_.transicionEntrada(escena); }

void Conmutador::setAfv(const std::string& fuente, bool sigue) {
    const TurnoMotor::Guarda turno(rt_.turno());
    if (sigue)
        afv_.fuentes.insert(fuente);
    else
        afv_.fuentes.erase(fuente);
    registro_.guardar();
    avisar([](Conmutador& c) { emit c.afvCambio(); }, FotoMotor::Salidas);
}

bool Conmutador::afv(const std::string& fuente) const {
    const TurnoMotor::Guarda turno(rt_.turno());
    return afv_.fuentes.count(fuente) > 0;
}

std::vector<std::string> Conmutador::afvFuentes() const {
    const TurnoMotor::Guarda turno(rt_.turno());
    return {afv_.fuentes.begin(), afv_.fuentes.end()};
}

Afv Conmutador::afvEstado() const {
    const TurnoMotor::Guarda turno(rt_.turno());
    return afv_;
}

// ============================================================================
// Conmutadores (el registro)
// ============================================================================

Conmutadores::Conmutadores(AppRuntime& rt, const std::vector<std::string>& salidas, QObject* parent)
    : QObject(parent), rt_(rt) {
    const nlohmann::json j = config::leerObjeto(ba::persist::archivoConfig("transiciones.json"));
    if (!j.empty()) {
        // {id: {tipo, ms, escena}}; uno de antes, {nombre: {tipo, ms}}: lo pasa a ids
        // reconciliarEscenas (ya en el dueño, con las escenas a la vista).
        transicionesEntrada_ = j.value("entrada", nlohmann::json::object());
        afvGuardado_[kPorOmision] = Afv::deJson(j); // lo de siempre: el de la por omisión
        const nlohmann::json otras = j.value("salidas", nlohmann::json::object()); // .items() de un temporal: UB
        for (const auto& [id, x] : otras.items())
            if (id != kPorOmision) afvGuardado_[id] = Afv::deJson(x);
    }
    porOmision_ = crear(kPorOmision);
    // Los de las salidas que ya hay (las que dice la foto): se arma en la interfaz y todavía
    // no se mudó al dueño, así que no le pregunta al motor (sincronizar sí, ya en el dueño).
    for (const std::string& id : salidas)
        if (conmutadores_.count(id) == 0) crear(id);
    // Las salidas se crean y se borran en el runtime: acá se sigue lo que haya (el aviso
    // llega en el dueño, que es donde vive este registro).
    observador_ = rt_.observar([this](AppRuntime::Cambio c) {
        if (c == AppRuntime::Cambio::Salidas) sincronizar();
        if (c == AppRuntime::Cambio::Escenas) reconciliarEscenas();
    });
    // Lo suyo en la foto del motor: la armada, el AFV y la T-bar de cada salida y las
    // transiciones de entrada (en el dueño, con el turno: el mapa lo cambia sincronizar,
    // también con él).
    aporte_ = rt_.aportarALaFoto([this](FotoMotor& f, unsigned partes) {
        if ((partes & FotoMotor::Salidas) != 0)
            for (FotoMotor::Salida& s : f.salidas)
                if (const auto it = conmutadores_.find(s.id); it != conmutadores_.end()) {
                    const Conmutador& c = *it->second;
                    s.armada = rt_.engine().sceneName(c.armada_);
                    s.afv.assign(c.afv_.fuentes.begin(), c.afv_.fuentes.end());
                    s.tbar = c.tbarPos_;
                }
        // A la interfaz, por el nombre de ahora ({escena: {tipo, ms}}).
        if ((partes & FotoMotor::Escenas) != 0) {
            const TablaEscenas t = rt_.engine().tablaEscenas();
            nlohmann::json porNombre = nlohmann::json::object();
            for (const auto& [k, v] : transicionesEntrada_.items())
                if (const std::string n = t.nombre(k); !n.empty() && v.is_object())
                    porNombre[n] = {{"tipo", v.value("tipo", std::string())}, {"ms", v.value("ms", -1)}};
            f.transicionesEntrada = porNombre;
        }
    });
    // Lo leído, a ids, ya en el dueño (un evento encolado se muda con el registro).
    QMetaObject::invokeMethod(this, [this]() { reconciliarEscenas(); }, Qt::QueuedConnection);
}

Conmutadores::~Conmutadores() {
    if (aporte_ >= 0) rt_.dejarDeAportar(aporte_);
    if (observador_ >= 0) rt_.dejarDeObservar(observador_);
}

Conmutador* Conmutadores::crear(const std::string& salida) {
    const auto g = afvGuardado_.find(salida);
    // Hijo del registro: se muda con él al hilo del dueño.
    auto c = std::make_unique<Conmutador>(rt_, *this, salida, g != afvGuardado_.end() ? g->second : Afv{}, this);
    Conmutador* p = c.get();
    connect(p, &Conmutador::alAireCambio, this,
            [this, id = QString::fromStdString(salida)](const QString& escena) { emit alAireCambio(id, escena); });
    conmutadores_[salida] = std::move(c);
    rt_.fotoSucia(FotoMotor::Salidas);
    emit creado(QString::fromStdString(salida));
    return p;
}

Conmutador* Conmutadores::de(const std::string& salida) {
    // Con el turno: el mapa lo cambia sincronizar, también con él (y quien usa el que
    // devuelve desde el plano de control lo tiene por toda su operación: no se le va).
    const TurnoMotor::Guarda turno(rt_.turno());
    const auto it = conmutadores_.find(salida.empty() ? std::string(kPorOmision) : salida);
    return it == conmutadores_.end() ? nullptr : it->second.get();
}

std::vector<Conmutador*> Conmutadores::todos() const {
    const TurnoMotor::Guarda turno(rt_.turno());
    std::vector<Conmutador*> out;
    for (const std::string& id : rt_.engine().salidaIds())
        if (const auto it = conmutadores_.find(id); it != conmutadores_.end()) out.push_back(it->second.get());
    return out;
}

void Conmutadores::sincronizar() {
    const TurnoMotor::Guarda turno(rt_.turno());
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

void Conmutadores::reconciliarEscenas() {
    const TurnoMotor::Guarda turno(rt_.turno());
    const TablaEscenas t = rt_.engine().tablaEscenas();
    // La armada va por el id: un renombre no la toca (el nombre que muestra la foto sí
    // cambia: abajo); la de una escena que ya no está, se va.
    for (auto& [id, c] : conmutadores_)
        if (!c->armada_.empty() && !t.hay(c->armada_)) {
            c->armada_.clear();
            c->avisar([](Conmutador& x) { emit x.armadaCambio(QString()); }, FotoMotor::Salidas);
        }
    if (seguirClaves(transicionesEntrada_, t)) guardar();
    rt_.fotoSucia(FotoMotor::Escenas | FotoMotor::Salidas); // los nombres de ahora
}

nlohmann::json Conmutadores::transicionEntrada(const std::string& escena) const {
    const TurnoMotor::Guarda turno(rt_.turno());
    // Por el id; lo leído que todavía no pasó a ids (reconciliarEscenas), por el nombre.
    const std::string id = rt_.engine().sceneId(escena);
    for (const std::string& k : {id, rt_.engine().sceneName(escena)})
        if (!k.empty() && transicionesEntrada_.contains(k) && transicionesEntrada_[k].is_object()) {
            const nlohmann::json& v = transicionesEntrada_[k];
            return {{"tipo", v.value("tipo", std::string())}, {"ms", v.value("ms", -1)}};
        }
    return nlohmann::json::object();
}

std::string Conmutadores::setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms) {
    const TurnoMotor::Guarda turno(rt_.turno());
    static const std::vector<std::string> tipos{"", "cut", "fade", "swipe", "slide", "fade_to_color"};
    if (std::find(tipos.begin(), tipos.end(), tipo) == tipos.end())
        return ba::i18n::traducir("aire", "tipo: cut, fade, swipe, slide o fade_to_color (vacío = la global)");
    const std::string id = rt_.engine().sceneId(escena);
    if (id.empty()) return ba::i18n::llenar(ba::i18n::traducir("aire", "no existe la escena «%1»"), {escena});
    const std::string nombre = rt_.engine().sceneName(id);
    transicionesEntrada_.erase(nombre); // una de antes, bajo el nombre
    if (tipo.empty())
        transicionesEntrada_.erase(id);
    else
        transicionesEntrada_[id] = {{"tipo", tipo}, {"ms", ms}, {"escena", nombre}};
    guardar();
    rt_.fotoSucia(FotoMotor::Escenas);
    return {};
}

void Conmutadores::guardar() const {
    const TurnoMotor::Guarda turno(rt_.turno());
    // El AFV de cada salida: el de las que andan y el guardado de las que no están.
    std::map<std::string, Afv> afv = afvGuardado_;
    for (const auto& [id, c] : conmutadores_) afv[id] = c->afvEstado();
    nlohmann::json j = afv[kPorOmision].aJson(); // lo de siempre: un arranque viejo lo sigue entendiendo
    j["entrada"] = transicionesEntrada_;
    nlohmann::json otras = nlohmann::json::object();
    for (const auto& [id, a] : afv)
        if (id != kPorOmision) otras[id] = a.aJson();
    if (!otras.empty()) j["salidas"] = otras;
    config::guardar(ba::persist::archivoConfig("transiciones.json"), j);
}

} // namespace ba::app::escenas
