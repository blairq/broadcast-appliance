// ============================================================================
// src/app/evento/Corrida.cpp — ver Corrida.hpp
// ============================================================================
#include "evento/Corrida.hpp"

#include "evento/LeerJson.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>

namespace ba::evento::linea {

using nlohmann::json;

const char* nombreDe(EstadoCorrida e) {
    switch (e) {
        case EstadoCorrida::Parada: return "parada";
        case EstadoCorrida::Corriendo: return "corriendo";
        case EstadoCorrida::Pausada: return "pausada";
        case EstadoCorrida::Terminada: return "terminada";
    }
    return "parada";
}

namespace {

/// Las salidas por las que pasa un paso ("*": todas).
std::vector<std::string> salidasDe(const Linea& l, const Paso& p) {
    if (p.salida == "*") return l.salidasEfectivas();
    return {l.salidaDe(p)};
}

} // namespace

Corrida::Corrida(Linea l, Entrada base) : linea_(std::move(l)), base_(std::move(base)) {
    // Lo vivo es de la corrida: de la base solo valen el arranque, la medianoche, el
    // fin planeado y las cámaras de cada escena.
    base_.instancias.clear();
    base_.salteados.clear();
    base_.holds.clear();
    base_.proximos.clear();
}

void Corrida::setLinea(Linea l) {
    linea_ = std::move(l);
    for (const Insercion& i : inserciones_)
        aplicar(i);
}

void Corrida::aplicar(const Insercion& ins) {
    if (linea_.paso(ins.paso.id) != nullptr) return;
    for (Bloque& b : linea_.bloques)
        for (std::size_t i = 0; i < b.pasos.size(); ++i)
            if (b.pasos[i].id == ins.despuesDe) {
                b.pasos.insert(b.pasos.begin() + static_cast<long>(i) + 1, ins.paso);
                return;
            }
    // Lo que tenía adelante ya no está (se editó la línea): va al final.
    if (linea_.bloques.empty()) linea_.bloques.push_back(Bloque{});
    linea_.bloques.back().pasos.push_back(ins.paso);
}

Entrada Corrida::entrada(int64_t ahora) const {
    Entrada e = base_;
    e.ahoraMs = ahora;
    e.instancias = instancias_;
    e.salteados = salteados_;
    e.holds = holdsPaso_;
    for (const std::string& s : holdCorte_)
        e.holds.push_back({s, {}});
    // En pausa nada entra solo: es un HOLD de todas las salidas que no cuenta como desvío.
    if (estado_ == EstadoCorrida::Pausada)
        for (const std::string& s : linea_.salidasEfectivas())
            e.holds.push_back({s, {}});
    for (const auto& [s, p] : proximos_)
        e.proximos.push_back({s, p});
    return e;
}

void Corrida::anotar(int64_t ahora, const char* que, json datos) {
    if (!datos.is_object()) datos = json::object();
    datos["ms"] = ahora;
    datos["que"] = que;
    recorrido_.push_back(std::move(datos));
}

std::vector<json> Corrida::sacarRecorrido() {
    std::vector<json> v;
    v.swap(recorrido_);
    return v;
}

int64_t Corrida::pausadoMs(int64_t ahora) const {
    return pausadoMs_ + (estado_ == EstadoCorrida::Pausada ? std::max<int64_t>(0, ahora - pausadoDesdeMs_) : 0);
}

// --- Lo emitido ------------------------------------------------------------------------

int Corrida::indiceAlAire(const std::string& salida) const {
    for (int i = static_cast<int>(instancias_.size()) - 1; i >= 0; --i) {
        const Instancia& x = instancias_[static_cast<std::size_t>(i)];
        if (x.salida == salida) return x.hastaMs == 0 ? i : -1;
    }
    return -1;
}

const Instancia* Corrida::alAire(const std::string& salida) const {
    const int i = indiceAlAire(salida);
    return i < 0 ? nullptr : &instancias_[static_cast<std::size_t>(i)];
}

bool Corrida::disparada(const std::string& salida, const std::string& pieza) const {
    const int i = indiceAlAire(salida);
    return i >= 0 && disparadas_.count({static_cast<std::size_t>(i), pieza}) > 0;
}

std::vector<std::string> Corrida::activas(const std::string& salida) const {
    std::vector<std::string> v;
    for (const Viva& x : vivas_)
        if (x.salida == salida) v.push_back(x.pieza);
    return v;
}

const Pieza* Corrida::pieza(const std::string& paso, const std::string& id) const {
    if (paso.empty()) {
        for (const ItemEstante& it : linea_.estante)
            if (it.pieza && (it.pieza->id == id || it.id == id)) return &*it.pieza;
        return nullptr;
    }
    const Paso* p = linea_.paso(paso);
    if (p == nullptr) return nullptr;
    for (const Pieza& z : p->piezas)
        if (z.id == id) return &z;
    return nullptr;
}

// --- Moverse ---------------------------------------------------------------------------

void Corrida::entrar(const std::string& salida, const std::string& paso, ComoEntro como, int64_t ahora, std::vector<Accion>& acc) {
    if (const int i = indiceAlAire(salida); i >= 0) {
        Instancia& x = instancias_[static_cast<std::size_t>(i)];
        x.hastaMs = ahora;
        anotar(ahora, "sale", {{"salida", salida}, {"paso", x.paso}, {"desdeMs", x.desdeMs}});
    }
    instancias_.push_back({salida, paso, ahora, 0, como});
    // El próximo movido a mano ya entró (o se descarta), y el HOLD era del paso que se fue.
    proximos_.erase(salida);
    holdsPaso_.erase(std::remove_if(holdsPaso_.begin(), holdsPaso_.end(), [&](const RefPaso& h) { return h.salida == salida; }),
                     holdsPaso_.end());
    armado_.erase(salida);
    acc.push_back({Accion::Que::EntraPaso, salida, paso, {}, {}, false, {}});
    anotar(ahora, "entra", {{"salida", salida}, {"paso", paso}, {"como", nombreDe(como)}});
}

void Corrida::disparar(const std::string& salida, const std::string& paso, const std::string& pieza, int64_t ahora, int64_t hasta,
                       bool reposicion, std::vector<Accion>& acc) {
    // La misma pieza otra vez (repetir, un adlib de nuevo) reemplaza a la que estaba.
    vivas_.erase(std::remove_if(vivas_.begin(), vivas_.end(),
                                [&](const Viva& v) { return v.salida == salida && v.paso == paso && v.pieza == pieza; }),
                 vivas_.end());
    vivas_.push_back({salida, paso, pieza, ahora, hasta});
    Accion a{Accion::Que::Pieza, salida, paso, pieza, {}, reposicion, {}};
    acc.push_back(a);
    anotar(ahora, reposicion ? "repone" : "pieza", {{"salida", salida}, {"paso", paso}, {"pieza", pieza}});
}

void Corrida::procesar(int64_t ahora, bool avanzar, std::vector<Accion>& acc) {
    Resolucion r;
    // Un paso que entra solo puede hacer entrar otro en el acto (un «sigue» sin
    // duración): se resuelve de nuevo hasta que no entre nada más (con tope).
    for (int vuelta = 0;; ++vuelta) {
        r = resolver(ahora);
        if (!avanzar || vuelta >= 64) break;
        const ProximoEvento* ev = nullptr;
        for (const ProximoEvento& x : r.proximosEventos)
            if (x.que == ProximoEvento::Que::EntraPaso && x.ms <= ahora) {
                ev = &x;
                break;
            }
        if (ev == nullptr) break;
        entrar(ev->salida, ev->paso, ev->como, ahora, acc);
    }

    // Las piezas del paso al aire que llegaron a su hora.
    for (const PiezaResuelta& z : r.piezas) {
        const int i = indiceAlAire(z.salida);
        if (i < 0 || instancias_[static_cast<std::size_t>(i)].paso != z.paso || z.desdeMs > ahora) continue;
        if (!disparadas_.insert({static_cast<std::size_t>(i), z.pieza}).second) continue;
        if (z.terminadaEn(ahora)) {
            // Su momento ya pasó entero (un cue de un segundo con el programa trabado).
            anotar(ahora, "perdida", {{"salida", z.salida}, {"paso", z.paso}, {"pieza", z.pieza}});
            continue;
        }
        disparar(z.salida, z.paso, z.pieza, ahora, kNunca, false, acc);
    }

    // Las vivas que se terminaron: lo dice el resolvedor (la vida de su paso, su
    // bloque, su duración); las que no conoce (del estante, repuestas al saltar)
    // viven lo suyo.
    for (auto it = vivas_.begin(); it != vivas_.end();) {
        bool sigue = it->hastaMs > ahora;
        for (const PiezaResuelta& z : r.piezas)
            if (z.salida == it->salida && z.paso == it->paso && z.pieza == it->pieza) sigue = !z.terminadaEn(ahora);
        if (sigue) {
            ++it;
            continue;
        }
        acc.push_back({Accion::Que::Saca, it->salida, it->paso, it->pieza, {}, false, {}});
        anotar(ahora, "saca", {{"salida", it->salida}, {"paso", it->paso}, {"pieza", it->pieza}});
        it = vivas_.erase(it);
    }

    // El lookahead: la escena del próximo, armada en el PVW de su salida.
    for (const SalidaResuelta& sr : r.salidas) {
        const Paso* p = sr.proximo.empty() ? nullptr : linea_.paso(sr.proximo);
        if (p == nullptr) continue;
        std::string escena;
        for (const Pieza& z : p->piezas) {
            const Pista* pi = linea_.pista(z.pista);
            if (z.tipo != "escena" || (pi && !pi->salida.empty() && pi->salida != sr.salida)) continue;
            escena = leerTexto(z.datos, "escena");
            if (!escena.empty()) break;
        }
        if (escena.empty() || armado_[sr.salida] == escena) continue;
        armado_[sr.salida] = escena;
        acc.push_back({Accion::Que::Armar, sr.salida, p->id, {}, escena, false, {}});
    }
}

std::vector<Accion> Corrida::arrancar(int64_t ahora) {
    if (estado_ == EstadoCorrida::Corriendo || estado_ == EstadoCorrida::Pausada) return {};
    estado_ = EstadoCorrida::Corriendo;
    arranqueMs_ = ahora;
    pausadoMs_ = 0;
    instancias_.clear();
    salteados_.clear();
    holdsPaso_.clear();
    proximos_.clear();
    disparadas_.clear();
    armado_.clear();
    anotar(ahora, "arranca");
    return go(salidaPorOmision(), ahora);
}

std::vector<Accion> Corrida::go(const std::string& salida, int64_t ahora) {
    std::vector<Accion> acc;
    if (estado_ == EstadoCorrida::Parada) return arrancar(ahora);
    if (estado_ == EstadoCorrida::Terminada) return acc;
    if (retomando_) soltarCorte("go", ahora); // con GO se suelta tal cual estaba
    const Resolucion r = resolver(ahora);
    const SalidaResuelta* sr = r.salida(salida);
    if (sr == nullptr) return acc;
    if (sr->proximo.empty()) {
        // No hay más: la salida termina. La corrida, cuando ya no queda nada al aire.
        if (const int i = indiceAlAire(salida); i >= 0) {
            instancias_[static_cast<std::size_t>(i)].hastaMs = ahora;
            anotar(ahora, "sale", {{"salida", salida}, {"paso", instancias_[static_cast<std::size_t>(i)].paso}});
        }
        acc.push_back({Accion::Que::Termina, salida, {}, {}, {}, false, {}});
        anotar(ahora, "termina", {{"salida", salida}});
        bool queda = false;
        for (const std::string& s : linea_.salidasEfectivas())
            queda = queda || indiceAlAire(s) >= 0;
        if (!queda) {
            std::vector<Accion> fin = terminar(ahora);
            acc.insert(acc.end(), fin.begin(), fin.end());
        }
        return acc;
    }
    entrar(salida, sr->proximo, ComoEntro::Go, ahora, acc);
    procesar(ahora, estado_ == EstadoCorrida::Corriendo, acc);
    return acc;
}

bool Corrida::setHold(const std::string& salida, bool on, int64_t ahora) {
    const Instancia* a = alAire(salida);
    if (a == nullptr) return false;
    const RefPaso h{salida, a->paso};
    const auto it = std::find(holdsPaso_.begin(), holdsPaso_.end(), h);
    if (on && it == holdsPaso_.end()) holdsPaso_.push_back(h);
    if (!on && it != holdsPaso_.end()) holdsPaso_.erase(it);
    anotar(ahora, on ? "hold" : "suelta", {{"salida", salida}, {"paso", a->paso}});
    return true;
}

bool Corrida::hold(const std::string& salida) const {
    if (holdCorte_.count(salida)) return true;
    const Instancia* a = alAire(salida);
    return a != nullptr && std::find(holdsPaso_.begin(), holdsPaso_.end(), RefPaso{salida, a->paso}) != holdsPaso_.end();
}

bool Corrida::moverProximo(const std::string& salida, int delta, int64_t ahora) {
    const Resolucion r = resolver(ahora);
    const SalidaResuelta* sr = r.salida(salida);
    if (sr == nullptr || delta == 0) return false;
    const auto& ps = sr->pasos;
    const int n = static_cast<int>(ps.size());
    int cur = -1;
    for (int i = 0; i < n; ++i)
        if (ps[static_cast<std::size_t>(i)].paso == sr->proximo) cur = i;
    if (cur < 0)
        for (int i = 0; i < n; ++i)
            if (ps[static_cast<std::size_t>(i)].paso == sr->alAire) cur = i;
    const auto saltado = [&](const std::string& p) {
        return std::find(salteados_.begin(), salteados_.end(), RefPaso{salida, p}) != salteados_.end();
    };
    int i = cur;
    for (int paso = delta > 0 ? 1 : -1, faltan = std::abs(delta); faltan > 0;) {
        i += paso;
        if (i < 0 || i >= n) return false;
        const std::string& id = ps[static_cast<std::size_t>(i)].paso;
        if (id == sr->alAire || saltado(id)) continue;
        --faltan;
    }
    proximos_[salida] = ps[static_cast<std::size_t>(i)].paso;
    anotar(ahora, "proximo", {{"salida", salida}, {"paso", proximos_[salida]}});
    return true;
}

bool Corrida::saltar(const std::string& salida, int64_t ahora) {
    const Resolucion r = resolver(ahora);
    const SalidaResuelta* sr = r.salida(salida);
    if (sr == nullptr || sr->proximo.empty()) return false;
    salteados_.push_back({salida, sr->proximo});
    if (proximos_.count(salida) && proximos_[salida] == sr->proximo) proximos_.erase(salida);
    anotar(ahora, "salta", {{"salida", salida}, {"paso", sr->proximo}});
    return true;
}

std::vector<Accion> Corrida::irA(const std::string& salida, const std::string& paso, int64_t ahora, std::string* error) {
    std::vector<Accion> acc;
    const Paso* p = linea_.paso(paso);
    if (p == nullptr || !linea_.vaPor(*p, salida)) {
        if (error) *error = ba::i18n::llenar(ba::i18n::traducir("linea", "no hay un paso «%1» en la salida «%2»"), {paso, salida});
        return acc;
    }
    if (estado_ == EstadoCorrida::Parada) {
        estado_ = EstadoCorrida::Corriendo;
        arranqueMs_ = ahora;
        anotar(ahora, "arranca");
    }
    if (estado_ == EstadoCorrida::Terminada) estado_ = EstadoCorrida::Corriendo;
    if (retomando_) soltarCorte("ir", ahora);
    // El «load to time»: lo que el plan tendría puesto al llegar ahí.
    const Carga c = cargarEn(linea_, base_, salida, paso, activas(salida));
    for (const std::string& id : c.sobran)
        for (auto it = vivas_.begin(); it != vivas_.end();) {
            if (it->salida != salida || it->pieza != id) {
                ++it;
                continue;
            }
            acc.push_back({Accion::Que::Saca, salida, it->paso, id, {}, false, {}});
            anotar(ahora, "saca", {{"salida", salida}, {"paso", it->paso}, {"pieza", id}});
            it = vivas_.erase(it);
        }
    const std::vector<std::string> ya = activas(salida);
    for (const Reposicion& d : c.deberianEstar) {
        if (std::find(ya.begin(), ya.end(), d.pieza.pieza) != ya.end()) continue;
        switch (d.accion) {
            case SiSePerdio::Ejecutar: disparar(salida, d.pieza.paso, d.pieza.pieza, ahora, kNunca, true, acc); break;
            case SiSePerdio::Preguntar:
                acc.push_back({Accion::Que::Avisar, salida, d.pieza.paso, d.pieza.pieza, {}, false,
                               ba::i18n::llenar(ba::i18n::traducir("linea", "«%1» tendría que estar hecho a esta altura: ¿lo hago?"),
                                                {d.pieza.pieza})});
                break;
            case SiSePerdio::Saltar: break;
        }
    }
    anotar(ahora, "ir", {{"salida", salida}, {"paso", paso}});
    entrar(salida, paso, ComoEntro::Go, ahora, acc);
    procesar(ahora, estado_ == EstadoCorrida::Corriendo, acc);
    return acc;
}

std::vector<Accion> Corrida::repetir(const std::string& salida, int64_t ahora) {
    std::vector<Accion> acc;
    const Instancia* a = alAire(salida);
    if (a == nullptr) return acc;
    const std::string paso = a->paso;
    anotar(ahora, "repite", {{"salida", salida}, {"paso", paso}});
    entrar(salida, paso, ComoEntro::Go, ahora, acc);
    procesar(ahora, estado_ == EstadoCorrida::Corriendo, acc);
    return acc;
}

std::vector<Accion> Corrida::adlib(const std::string& salida, const std::string& id, int64_t ahora, std::string* error) {
    std::vector<Accion> acc;
    if (const int i = indiceAlAire(salida); i >= 0) {
        const std::string& paso = instancias_[static_cast<std::size_t>(i)].paso;
        if (pieza(paso, id) != nullptr) {
            disparadas_.insert({static_cast<std::size_t>(i), id});
            disparar(salida, paso, id, ahora, kNunca, false, acc);
            procesar(ahora, false, acc);
            return acc;
        }
    }
    for (const ItemEstante& it : linea_.estante) {
        if (!it.pieza || (it.id != id && it.pieza->id != id)) continue;
        const int64_t hasta = it.pieza->dura.modo == Dura::Modo::Ms ? ahora + it.pieza->dura.ms : kNunca;
        disparar(salida, {}, it.pieza->id, ahora, hasta, false, acc);
        return acc;
    }
    if (error)
        *error = ba::i18n::llenar(
            ba::i18n::traducir("linea", "«%1» no es una pieza del paso al aire ni del estante"), {id});
    return acc;
}

std::vector<Accion> Corrida::insertar(const std::string& salida, const std::string& item, bool alAireYa, int64_t ahora,
                                      std::string* error) {
    std::vector<Accion> acc;
    const ItemEstante* it = nullptr;
    for (const ItemEstante& x : linea_.estante)
        if (x.id == item && x.paso) it = &x;
    if (it == nullptr) {
        if (error) *error = ba::i18n::llenar(ba::i18n::traducir("linea", "no hay un paso «%1» en el estante"), {item});
        return acc;
    }
    // Una copia con ids propios: lo planeado no se toca y se puede insertar dos veces.
    Insercion ins;
    ins.paso = *it->paso;
    ins.paso.id = "ins" + std::to_string(++insertados_) + "-" + it->paso->id;
    ins.paso.salida = salida;
    for (Pieza& z : ins.paso.piezas)
        z.id = ins.paso.id + "-" + z.id;
    const Resolucion r = resolver(ahora);
    const SalidaResuelta* sr = r.salida(salida);
    if (const Instancia* a = alAire(salida)) {
        ins.despuesDe = a->paso;
    } else if (sr != nullptr && !sr->proximo.empty()) {
        // Antes del próximo: detrás del que lo precede en su bloque (o al principio).
        for (const Bloque& b : linea_.bloques)
            for (std::size_t i = 0; i < b.pasos.size(); ++i)
                if (b.pasos[i].id == sr->proximo && i > 0) ins.despuesDe = b.pasos[i - 1].id;
    }
    aplicar(ins);
    inserciones_.push_back(ins);
    proximos_[salida] = ins.paso.id;
    anotar(ahora, "inserta", {{"salida", salida}, {"paso", ins.paso.id}, {"item", item}, {"alAire", alAireYa}});
    if (alAireYa) return go(salida, ahora);
    procesar(ahora, false, acc);
    return acc;
}

void Corrida::pausar(int64_t ahora) {
    if (estado_ != EstadoCorrida::Corriendo) return;
    estado_ = EstadoCorrida::Pausada;
    pausadoDesdeMs_ = ahora;
    anotar(ahora, "pausa");
}

std::vector<Accion> Corrida::reanudar(int64_t ahora) {
    std::vector<Accion> acc;
    if (estado_ != EstadoCorrida::Pausada) return acc;
    // Lo que estaba al aire retoma su cuenta donde la dejó: se corre lo pausado (lo
    // que entró durante la pausa, desde que entró). Así lo de adentro del paso (el
    // zócalo a los 5 s) y su fin previsto no se comen la pausa.
    for (Instancia& x : instancias_) {
        if (x.hastaMs != 0) continue;
        x.desdeMs += ahora - std::max(pausadoDesdeMs_, x.desdeMs);
    }
    pausadoMs_ += ahora - pausadoDesdeMs_;
    estado_ = EstadoCorrida::Corriendo;
    anotar(ahora, "sigue", {{"pausaMs", ahora - pausadoDesdeMs_}});
    procesar(ahora, true, acc);
    return acc;
}

std::vector<Accion> Corrida::terminar(int64_t ahora) {
    std::vector<Accion> acc;
    if (estado_ == EstadoCorrida::Parada || estado_ == EstadoCorrida::Terminada) return acc;
    if (estado_ == EstadoCorrida::Pausada) pausadoMs_ += ahora - pausadoDesdeMs_;
    for (Instancia& x : instancias_)
        if (x.hastaMs == 0) {
            x.hastaMs = ahora;
            anotar(ahora, "sale", {{"salida", x.salida}, {"paso", x.paso}});
        }
    estado_ = EstadoCorrida::Terminada;
    holdCorte_.clear();
    retomando_ = false;
    pideDescripcion_ = false;
    // Lo que se iba con su paso, se va (el resto queda: la música del evento sigue).
    procesar(ahora, false, acc);
    anotar(ahora, "fin");
    return acc;
}

// --- El tiempo -------------------------------------------------------------------------

std::vector<Accion> Corrida::tick(int64_t ahora) {
    std::vector<Accion> acc;
    if (estado_ != EstadoCorrida::Corriendo) return acc;
    procesar(ahora, true, acc);
    return acc;
}

std::vector<Accion> Corrida::alDia(int64_t ahora) {
    std::vector<Accion> acc;
    if (estado_ == EstadoCorrida::Corriendo || estado_ == EstadoCorrida::Pausada) procesar(ahora, false, acc);
    return acc;
}

int64_t Corrida::proximoMs(int64_t ahora, const std::string& salida) const {
    if (estado_ != EstadoCorrida::Corriendo) return -1;
    const Resolucion r = resolver(ahora);
    int64_t mejor = kNunca;
    for (const ProximoEvento& ev : r.proximosEventos)
        if (ev.ms > ahora && (salida.empty() || ev.salida == salida)) mejor = std::min(mejor, ev.ms);
    for (const Viva& v : vivas_)
        if (v.hastaMs != kNunca && v.hastaMs > ahora && (salida.empty() || v.salida == salida)) mejor = std::min(mejor, v.hastaMs);
    return mejor == kNunca ? -1 : mejor - ahora;
}

int Corrida::entraSoloEnSeg(const std::string& salida, int64_t ahora) const {
    if (estado_ != EstadoCorrida::Corriendo) return 0;
    const Resolucion r = resolver(ahora);
    for (const ProximoEvento& ev : r.proximosEventos) {
        if (ev.que != ProximoEvento::Que::EntraPaso || ev.salida != salida || ev.ms <= ahora) continue;
        const int64_t ms = ev.ms - ahora;
        return ms > kAvisoSeg * 1000LL ? 0 : static_cast<int>((ms + 999) / 1000);
    }
    return 0;
}

// --- Estado vivo y retoma ----------------------------------------------------------------

json Corrida::aJsonVivo(int64_t ahora) const {
    EstadoAire e;
    e.escritoMs = ahora;
    const Resolucion r = resolver(ahora);
    for (const std::string& s : linea_.salidasEfectivas()) {
        SalidaAire sa;
        sa.salida = s;
        if (const Instancia* a = alAire(s)) {
            sa.alAire = a->paso;
            sa.alAireDesdeMs = a->desdeMs;
        }
        const auto it = proximos_.find(s);
        if (it != proximos_.end()) sa.proximo = it->second;
        sa.hold = hold(s);
        e.salidas.push_back(sa);
    }
    for (const Viva& v : vivas_)
        e.piezas.push_back({v.pieza, v.salida, v.desdeMs});
    e.instancias = instancias_;
    e.salteados = salteados_;
    e.pideDescripcion = pideDescripcion_;
    e.descripcion = descripcion_;
    json j = e.aJson();
    j["estado"] = nombreDe(estado_);
    j["arranqueMs"] = arranqueMs_;
    j["pausadoMs"] = pausadoMs(ahora);
    json hs = json::array();
    for (const RefPaso& h : holdsPaso_)
        hs.push_back({{"salida", h.salida}, {"paso", h.paso}});
    j["holdsPaso"] = hs;
    json ins = json::array();
    for (const Insercion& i : inserciones_)
        ins.push_back({{"paso", i.paso.aJson()}, {"despuesDe", i.despuesDe}});
    j["inserciones"] = ins;
    j["insertados"] = insertados_;
    j["retomando"] = retomando_;
    return j;
}

std::vector<Accion> Corrida::retomar(const json& vivo, int64_t ahora) {
    std::vector<Accion> acc;
    const EstadoAire g = EstadoAire::deJson(vivo);
    inserciones_.clear();
    if (vivo.contains("inserciones") && vivo["inserciones"].is_array())
        for (const json& x : vivo["inserciones"]) {
            if (!x.is_object() || !x.contains("paso")) continue;
            inserciones_.push_back({Paso::deJson(x["paso"]), leerTexto(x, "despuesDe")});
            aplicar(inserciones_.back());
        }
    insertados_ = static_cast<int>(leerNumero(vivo, "insertados", static_cast<int64_t>(inserciones_.size())));

    // Lo que se pierde no es lo emitido: vuelve como estaba.
    instancias_ = g.instancias;
    salteados_ = g.salteados;
    proximos_.clear();
    for (const SalidaAire& s : g.salidas)
        if (!s.proximo.empty()) proximos_[s.salida] = s.proximo;
    holdsPaso_.clear();
    if (vivo.contains("holdsPaso") && vivo["holdsPaso"].is_array())
        for (const json& h : vivo["holdsPaso"])
            holdsPaso_.push_back({leerTexto(h, "salida"), leerTexto(h, "paso")});
    arranqueMs_ = leerNumero(vivo, "arranqueMs", g.escritoMs);
    pausadoMs_ = leerNumero(vivo, "pausadoMs");
    estado_ = EstadoCorrida::Corriendo;
    disparadas_.clear();
    vivas_.clear();
    armado_.clear();

    // Todo en HOLD hasta que alguien cuente qué está pasando (3.8).
    const Retoma rt = retomarTrasCorte(linea_, g, entrada(ahora));
    holdCorte_.clear();
    for (const SalidaAire& s : rt.estado.salidas)
        holdCorte_.insert(s.salida);
    retomando_ = true;
    pideDescripcion_ = true;
    descripcion_.clear();
    resumen_ = rt.resumen;

    // Lo del paso al aire que ya había pasado antes del corte no se vuelve a disparar.
    const Resolucion r = resolver(ahora);
    for (const PiezaResuelta& z : r.piezas) {
        const int i = indiceAlAire(z.salida);
        if (i >= 0 && instancias_[static_cast<std::size_t>(i)].paso == z.paso && z.desdeMs <= g.escritoMs)
            disparadas_.insert({static_cast<std::size_t>(i), z.pieza});
    }
    for (const Reposicion& x : rt.reponer) {
        disparar(x.pieza.salida, x.pieza.paso, x.pieza.pieza, ahora, kNunca, true, acc);
        vivas_.back().desdeMs = x.pieza.desdeMs;
    }
    for (const Reposicion& x : rt.perdidas) {
        if (const int i = indiceAlAire(x.pieza.salida); i >= 0 && instancias_[static_cast<std::size_t>(i)].paso == x.pieza.paso)
            disparadas_.insert({static_cast<std::size_t>(i), x.pieza.pieza});
        switch (x.accion) {
            case SiSePerdio::Ejecutar:
                if (x.sigueActiva) disparar(x.pieza.salida, x.pieza.paso, x.pieza.pieza, ahora, kNunca, true, acc);
                break;
            case SiSePerdio::Preguntar:
                acc.push_back({Accion::Que::Avisar, x.pieza.salida, x.pieza.paso, x.pieza.pieza, {}, false,
                               ba::i18n::llenar(ba::i18n::traducir("linea", "«%1» tocaba durante el corte y no se hizo: ¿lo hago?"),
                                                {x.pieza.pieza})});
                break;
            case SiSePerdio::Saltar: break;
        }
    }
    for (const std::string& id : rt.sacar)
        for (const PiezaAire& p : g.piezas)
            if (p.pieza == id) acc.push_back({Accion::Que::Saca, p.salida, {}, id, {}, false, {}});
    anotar(ahora, "corte", {{"desdeMs", g.escritoMs}, {"hastaMs", ahora}, {"resumen", resumen_}});
    procesar(ahora, false, acc);
    return acc;
}

void Corrida::describir(const std::string& texto, int64_t ahora) {
    descripcion_ = texto;
    pideDescripcion_ = false;
    anotar(ahora, "descripcion", {{"texto", texto}});
}

void Corrida::soltarCorte(const std::string& decision, int64_t ahora) {
    holdCorte_.clear();
    retomando_ = false;
    pideDescripcion_ = false;
    anotar(ahora, "decision", {{"texto", decision}});
}

std::vector<Accion> Corrida::soltar(const std::string& decision, int64_t ahora) {
    std::vector<Accion> acc;
    if (!retomando_) return acc;
    soltarCorte(decision, ahora);
    procesar(ahora, estado_ == EstadoCorrida::Corriendo, acc);
    return acc;
}

json Corrida::estadoJson(int64_t ahora, const std::function<std::string(int64_t)>& hora) const {
    const auto H = [&](int64_t ms) -> json {
        if (ms == kNunca) return nullptr;
        return hora ? json(hora(ms)) : json(ms);
    };
    const Resolucion r = resolver(ahora);
    json salidas = json::array();
    for (const SalidaResuelta& sr : r.salidas) {
        json js{{"salida", sr.salida},
                {"hold", hold(sr.salida)},
                {"holdCorte", holdCorte_.count(sr.salida) > 0},
                {"fin", H(sr.finMs)},
                {"overUnderSeg", sr.overUnderMs / 1000},
                {"alAire", nullptr},
                {"proximo", nullptr},
                {"proximoHardStart", nullptr},
                {"activas", activas(sr.salida)}};
        if (sr.finPlaneadoMs > 0) js["finPlaneado"] = H(sr.finPlaneadoMs);
        if (const PasoResuelto* p = sr.paso(sr.alAire)) {
            const Paso* m = linea_.paso(p->paso);
            const int64_t lleva = ahora - p->frenteMs;
            js["alAire"] = {{"paso", p->paso},
                            {"nombre", m ? m->nombre : std::string()},
                            {"bloque", p->bloque},
                            {"desde", H(p->frenteMs)},
                            {"llevaSeg", lleva / 1000},
                            {"previstoSeg", m ? m->duracionPlanMs() / 1000 : 0},
                            {"restanteSeg", m ? (m->duracionPlanMs() - lleva) / 1000 : 0}};
            for (const BloqueResuelto& b : sr.bloques)
                if (b.bloque == p->bloque) js["bloque"] = {{"id", b.bloque}, {"overUnderSeg", b.overUnderMs / 1000}};
        }
        if (const PasoResuelto* p = sr.paso(sr.proximo)) {
            const Paso* m = linea_.paso(p->paso);
            js["proximo"] = {{"paso", p->paso},
                             {"nombre", m ? m->nombre : std::string()},
                             {"entra", nombreDe(p->como)},
                             {"automatico", p->automatico},
                             {"frente", H(p->frenteMs)},
                             {"entraSoloEnSeg", entraSoloEnSeg(sr.salida, ahora)}};
            if (proximos_.count(sr.salida)) js["proximo"]["movidoAMano"] = true;
        }
        for (const HardStart& h : sr.hardStarts) {
            if (h.horaMs < ahora) continue;
            js["proximoHardStart"] = {
                {"paso", h.paso}, {"hora", H(h.horaMs)}, {"enSeg", (h.horaMs - ahora) / 1000}, {"gapSeg", h.gapMs / 1000}, {"politica", nombreDe(h.politica)}};
            break;
        }
        salidas.push_back(std::move(js));
    }
    json out{{"estado", nombreDe(estado_)},
             {"salidas", salidas},
             {"conflictos", r.conflictos.size()},
             {"retoma", {{"activa", retomando_}, {"pideDescripcion", pideDescripcion_}, {"descripcion", descripcion_}}}};
    if (retomando_) out["retoma"]["resumen"] = resumen_;
    if (arranqueMs_ > 0) {
        out["arranque"] = H(arranqueMs_);
        out["transcurridoSeg"] = (ahora - arranqueMs_ - pausadoMs(ahora)) / 1000;
    }
    if (estado_ == EstadoCorrida::Pausada) out["pausadaSeg"] = (ahora - pausadoDesdeMs_) / 1000;
    return out;
}

} // namespace ba::evento::linea
