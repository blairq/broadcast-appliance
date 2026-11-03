// ============================================================================
// src/app/vivos/Vivos.cpp — ver Vivos.hpp
// ============================================================================

#include "vivos/Vivos.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace ba::app::vivos {

namespace {


std::string str(const json& j, const char* k) {
    if (!j.is_object()) return {};
    const auto it = j.find(k);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool idValido(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (const char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    return true;
}

std::string sinBarraFinal(std::string u) {
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

/// La cuenta de un destino ("" = la principal).
std::string cuentaDe(const json& d) {
    const std::string c = str(d, "cuenta");
    return c.empty() ? std::string(kPrincipal) : c;
}

} // namespace

const std::vector<std::string>& plataformas() {
    static const std::vector<std::string> p{"youtube", "twitch", "facebook"};
    return p;
}

bool esPlataforma(const std::string& p) {
    const auto& l = plataformas();
    return std::find(l.begin(), l.end(), p) != l.end();
}

bool esGenerica(const std::string& p) { return p == "rtmp" || p == "srt" || p == "rist"; }

std::string nombrePlataforma(const std::string& p) {
    if (p == "youtube") return "YouTube";
    if (p == "twitch") return "Twitch";
    if (p == "facebook") return "Facebook";
    if (p == "rtmp") return "RTMP";
    if (p == "srt") return "SRT";
    if (p == "rist") return "RIST";
    return p;
}

std::string credencialDeCuenta(const std::string& plataforma, const std::string& cuenta) {
    if (cuenta.empty() || cuenta == kPrincipal) return plataforma + "-token";
    return plataforma + "-token-" + cuenta;
}

std::string estadoCuenta(bool configurada, bool conectada, bool conectando, bool sinPermiso, bool conectadaAntes) {
    if (!configurada) return "sin_app";
    if (conectando) return "conectando";
    if (conectada) return sinPermiso ? "sin_permiso" : "conectada";
    return conectadaAntes ? "vencida" : "sin_conectar";
}

std::string textoEstadoCuenta(const std::string& e) {
    if (e == "sin_app") return ba::i18n::traducir("vivos", "falta la app de la plataforma");
    if (e == "conectando") return ba::i18n::traducir("vivos", "esperando la autorización en el navegador");
    if (e == "conectada") return ba::i18n::traducir("vivos", "conectada");
    if (e == "sin_permiso") return ba::i18n::traducir("vivos", "conectada sin el permiso necesario: reautorizala");
    if (e == "vencida") return ba::i18n::traducir("vivos", "vencida: volvé a autorizarla");
    return ba::i18n::traducir("vivos", "sin conectar");
}

// --- RegistroCuentas -----------------------------------------------------------------------

void RegistroCuentas::cargar() {
    cuentas_.clear();
    siguiente_.clear();
    const json j = a_.leer ? a_.leer() : json();
    bool cambio = !j.is_object();
    if (j.is_object()) {
        const json siguiente = j.value("siguiente", json::object()); // items() de un temporal: UB
        for (const auto& [p, n] : siguiente.items())
            if (esPlataforma(p) && n.is_number_integer()) siguiente_[p] = n.get<int>();
        std::set<std::string> vistas;
        for (const json& c : j.value("cuentas", json::array())) {
            Cuenta x;
            x.plataforma = str(c, "plataforma");
            x.id = str(c, "id");
            if (!esPlataforma(x.plataforma) || !idValido(x.id) || !vistas.insert(x.plataforma + "/" + x.id).second) {
                cambio = true; // lo roto o repetido no se arrastra
                continue;
            }
            x.nombre = str(c, "nombre");
            x.canal = str(c, "canal");
            x.conectadaAntes = c.is_object() && c.value("conectada_antes", false);
            cuentas_.push_back(std::move(x));
        }
    }
    // Cada plataforma con su principal, primera (la de antes de las cuentas: su token es el
    // de siempre, así que la cuenta que había sigue ahí).
    std::vector<Cuenta> ordenadas;
    for (const std::string& p : plataformas()) {
        const auto pr = std::find_if(cuentas_.begin(), cuentas_.end(),
                                     [&](const Cuenta& c) { return c.plataforma == p && c.id == kPrincipal; });
        if (pr != cuentas_.end()) {
            ordenadas.push_back(*pr);
        } else {
            ordenadas.push_back(Cuenta{p, kPrincipal, ba::i18n::traducir("vivos", "Principal"), {}, false});
            cambio = true;
        }
        for (const Cuenta& c : cuentas_)
            if (c.plataforma == p && c.id != kPrincipal) ordenadas.push_back(c);
    }
    cuentas_ = std::move(ordenadas);
    // El próximo número no baja de lo que ya hay (un archivo sin «siguiente»).
    for (const Cuenta& c : cuentas_)
        if (c.id.rfind("cuenta-", 0) == 0) {
            int n = 0;
            if (std::sscanf(c.id.c_str() + 7, "%d", &n) == 1 && n >= siguiente_[c.plataforma]) siguiente_[c.plataforma] = n + 1;
        }
    if (cambio) guardar();
}

std::vector<Cuenta> RegistroCuentas::de(const std::string& plataforma) const {
    std::vector<Cuenta> out;
    for (const Cuenta& c : cuentas_)
        if (plataforma.empty() || c.plataforma == plataforma) out.push_back(c);
    return out;
}

const Cuenta* RegistroCuentas::buscar(const std::string& plataforma, const std::string& id) const {
    const std::string i = id.empty() ? std::string(kPrincipal) : id;
    for (const Cuenta& c : cuentas_)
        if (c.plataforma == plataforma && c.id == i) return &c;
    return nullptr;
}

Cuenta* RegistroCuentas::buscarMut(const std::string& plataforma, const std::string& id) {
    return const_cast<Cuenta*>(buscar(plataforma, id));
}

std::string RegistroCuentas::agregar(const std::string& plataforma, const std::string& nombre, std::string* idOut) {
    if (!esPlataforma(plataforma)) return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una plataforma «%1» (youtube, twitch o facebook)."), {plataforma});
    int& n = siguiente_[plataforma];
    if (n < 2) n = 2;
    std::string id;
    do {
        id = "cuenta-" + std::to_string(n++);
    } while (buscar(plataforma, id) != nullptr);
    Cuenta c{plataforma, id, nombre.empty() ? ba::i18n::llenar(ba::i18n::traducir("vivos", "Cuenta %1"), {id.substr(7)}) : nombre, {}, false};
    cuentas_.push_back(std::move(c));
    // Quedan juntas las de cada plataforma (la principal primera).
    std::stable_sort(cuentas_.begin(), cuentas_.end(), [](const Cuenta& a, const Cuenta& b) {
        const auto& l = plataformas();
        return std::find(l.begin(), l.end(), a.plataforma) < std::find(l.begin(), l.end(), b.plataforma);
    });
    if (!guardar()) return ba::i18n::traducir("vivos", "No se pudo guardar la lista de cuentas.");
    if (idOut != nullptr) *idOut = id;
    return {};
}

std::string RegistroCuentas::renombrar(const std::string& plataforma, const std::string& id, const std::string& nombre) {
    Cuenta* c = buscarMut(plataforma, id);
    if (c == nullptr) return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una cuenta «%1» de %2."), {id, nombrePlataforma(plataforma)});
    if (nombre.empty()) return ba::i18n::traducir("vivos", "Falta el nombre.");
    if (c->nombre == nombre) return {};
    c->nombre = nombre;
    return guardar() ? std::string() : ba::i18n::traducir("vivos", "No se pudo guardar la lista de cuentas.");
}

std::string RegistroCuentas::quitar(const std::string& plataforma, const std::string& id) {
    if (id.empty() || id == kPrincipal)
        return ba::i18n::traducir("vivos", "La cuenta principal no se quita: desconectala (o conectá otra en su lugar).");
    const auto it = std::find_if(cuentas_.begin(), cuentas_.end(),
                                 [&](const Cuenta& c) { return c.plataforma == plataforma && c.id == id; });
    if (it == cuentas_.end()) return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una cuenta «%1» de %2."), {id, nombrePlataforma(plataforma)});
    cuentas_.erase(it);
    return guardar() ? std::string() : ba::i18n::traducir("vivos", "No se pudo guardar la lista de cuentas.");
}

void RegistroCuentas::anotarConexion(const std::string& plataforma, const std::string& id, const std::string& canal) {
    Cuenta* c = buscarMut(plataforma, id);
    if (c == nullptr) return;
    if (c->conectadaAntes && (canal.empty() || c->canal == canal)) return;
    c->conectadaAntes = true;
    if (!canal.empty()) c->canal = canal;
    guardar();
}

void RegistroCuentas::anotarDesconexion(const std::string& plataforma, const std::string& id) {
    Cuenta* c = buscarMut(plataforma, id);
    if (c == nullptr || !c->conectadaAntes) return;
    c->conectadaAntes = false;
    guardar();
}

json RegistroCuentas::aJson() const {
    json l = json::array();
    for (const Cuenta& c : cuentas_)
        l.push_back({{"plataforma", c.plataforma}, {"id", c.id}, {"nombre", c.nombre}, {"canal", c.canal},
                     {"conectada_antes", c.conectadaAntes}});
    json sig = json::object();
    for (const auto& [p, n] : siguiente_) sig[p] = n;
    return {{"version", 1}, {"cuentas", l}, {"siguiente", sig}};
}

bool RegistroCuentas::guardar() { return !a_.escribir || a_.escribir(aJson()); }

// --- Destinos ------------------------------------------------------------------------------

std::string plataformaDe(const json& d) {
    const std::string p = str(d, "plataforma");
    if (!p.empty()) return p;
    const std::string u = str(d, "url");
    for (const char* e : {"srt", "rist"})
        if (u.rfind(std::string(e) + "://", 0) == 0) return e;
    return "rtmp";
}

std::string huella(const std::string& clave) {
    if (clave.empty()) return {};
    // FNV-1a de 64 bits: solo para comparar en memoria (nunca se guarda ni se muestra).
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : clave) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string vivoDe(const json& d, const std::string& huellaClave) {
    const std::string p = plataformaDe(d);
    if (p == "twitch") return "twitch:" + cuentaDe(d);
    if (esPlataforma(p)) {
        const std::string v = str(d, "vivo");
        if (!v.empty()) return p + ":" + cuentaDe(d) + ":" + v;
        // Sin vivo (no debería): como uno genérico.
    }
    const std::string u = sinBarraFinal(str(d, "url"));
    if (u.empty()) return {};
    return "url:" + u + "#" + huellaClave;
}

std::vector<Choque> choques(const json& destinos, const std::function<std::string(const std::string&)>& huellaDe) {
    std::vector<Choque> out;
    std::map<std::string, std::size_t> donde;
    if (!destinos.is_array()) return out;
    for (const json& d : destinos) {
        const std::string id = str(d, "id");
        const std::string v = vivoDe(d, huellaDe ? huellaDe(id) : std::string());
        if (v.empty()) continue;
        const auto it = donde.find(v);
        std::string sal = str(d, "salida");
        if (sal.empty()) sal = "principal";
        if (it == donde.end()) {
            donde[v] = out.size();
            out.push_back({v, {id}, {sal}});
        } else {
            Choque& c = out[it->second];
            c.destinos.push_back(id);
            if (std::find(c.salidas.begin(), c.salidas.end(), sal) == c.salidas.end()) c.salidas.push_back(sal);
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Choque& c) { return c.destinos.size() < 2; }), out.end());
    return out;
}

std::string chocaCon(const json& destinos, const json& nuevo, const std::string& huellaNuevo,
                     const std::function<std::string(const std::string&)>& huellaDe,
                     const std::function<std::string(const std::string&)>& nombreSalida) {
    const std::string idNuevo = str(nuevo, "id");
    const std::string v = vivoDe(nuevo, huellaNuevo.empty() && huellaDe ? huellaDe(idNuevo) : huellaNuevo);
    if (v.empty() || !destinos.is_array()) return {};
    for (const json& d : destinos) {
        const std::string id = str(d, "id");
        if (!idNuevo.empty() && id == idNuevo) continue; // se reemplaza a sí mismo
        if (vivoDe(d, huellaDe ? huellaDe(id) : std::string()) != v) continue;
        std::string sal = str(d, "salida");
        if (sal.empty()) sal = "principal";
        std::string nomSal = nombreSalida ? nombreSalida(sal) : std::string();
        if (nomSal.empty()) nomSal = sal;
        std::string salNuevo = str(nuevo, "salida");
        if (salNuevo.empty()) salNuevo = "principal";
        const std::string nombre = str(d, "nombre").empty() ? id : str(d, "nombre");
        if (salNuevo == sal)
            return ba::i18n::llenar(ba::i18n::traducir("vivos", "Ese vivo ya es el destino «%1» de esta misma salida («%2»): no hace falta otro."), {nombre, nomSal});
        return ba::i18n::llenar(ba::i18n::traducir("vivos", "Ese vivo ya lo usa el destino «%1» de la salida «%2». Dos salidas al mismo vivo se "
                                   "pisan (la plataforma recibe dos señales en la misma clave): elegí otro vivo, creá uno "
                                   "nuevo o quitá ese destino primero."),
                                {nombre, nomSal});
    }
    return {};
}

std::string estadoDestino(bool activo, bool reconectando, std::int64_t bytes, double kbps) {
    if (!activo) return "detenido";
    if (reconectando) return "reintentando";
    if (bytes <= 0 && kbps <= 0) return "conectando";
    return "en_vivo";
}

std::string textoEstadoDestino(const std::string& e) {
    if (e == "conectando") return ba::i18n::traducir("vivos", "conectando");
    if (e == "en_vivo") return ba::i18n::traducir("vivos", "en vivo");
    if (e == "reintentando") return ba::i18n::traducir("vivos", "cortado, reintentando");
    return ba::i18n::traducir("vivos", "detenido");
}

std::string armarDestino(const Asignacion& a, json& out) {
    std::string p = a.plataforma;
    if (p.empty()) p = a.url.empty() ? std::string() : plataformaDe(json{{"url", a.url}});
    if (p.empty()) return ba::i18n::traducir("vivos", "Falta la plataforma (youtube, twitch, facebook) o la URL (rtmp://, srt://…).");
    if (a.salida.empty()) return ba::i18n::traducir("vivos", "Falta la salida.");
    out = json::object();
    if (!a.id.empty()) out["id"] = a.id;
    out["salida"] = a.salida;
    out["respaldo"] = a.respaldo;
    if (esPlataforma(p)) {
        const std::string cuenta = a.cuenta.empty() ? std::string(kPrincipal) : a.cuenta;
        if (p != "twitch" && a.vivo.empty())
            return ba::i18n::llenar(ba::i18n::traducir("vivos", "Falta el vivo de %1: elegí uno de la lista (vivos_listar) o creá uno nuevo."), {nombrePlataforma(p)});
        out["plataforma"] = p;
        out["cuenta"] = cuenta;
        out["vivo"] = p == "twitch" ? std::string() : a.vivo;
        out["vivo_titulo"] = a.vivoTitulo;
        std::string nombre = a.nombre;
        if (nombre.empty()) {
            nombre = nombrePlataforma(p);
            if (!a.vivoTitulo.empty()) nombre += " · " + a.vivoTitulo;
        }
        out["nombre"] = nombre;
        if (!a.url.empty()) out["url"] = a.url; // la ingesta (la pone el servicio)
        return {};
    }
    if (!esGenerica(p)) return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una plataforma «%1»."), {p});
    const std::string& u = a.url;
    if (u.rfind("rtmp://", 0) != 0 && u.rfind("rtmps://", 0) != 0 && u.rfind("srt://", 0) != 0 && u.rfind("rist://", 0) != 0)
        return ba::i18n::traducir("vivos", "`url`: rtmp://, rtmps://, srt:// o rist://");
    out["url"] = u;
    out["plataforma"] = plataformaDe(json{{"url", u}});
    out["nombre"] = a.nombre.empty() ? sinBarraFinal(u) : a.nombre;
    return {};
}

// --- ServicioVivos -------------------------------------------------------------------------

json ServicioVivos::cuentas(const std::string& plataforma) const {
    json out = json::array();
    for (const Cuenta& c : p_.cuentas.de(plataforma)) {
        const Plataforma* pl = p_.plataforma ? p_.plataforma(c.plataforma) : nullptr;
        const bool conectada = pl != nullptr && pl->conectada(c.id);
        const std::string e = estadoCuenta(pl != nullptr && pl->configurada(), conectada, pl != nullptr && pl->conectando(c.id),
                                           pl != nullptr && pl->sinPermiso(c.id), c.conectadaAntes);
        std::string canal = pl != nullptr ? pl->canal(c.id) : std::string();
        if (canal.empty()) canal = c.canal;
        out.push_back({{"plataforma", c.plataforma},
                       {"id", c.id},
                       {"nombre", c.nombre},
                       {"canal", canal},
                       {"estado", e},
                       {"estado_texto", textoEstadoCuenta(e)},
                       {"principal", c.id == kPrincipal}});
    }
    return out;
}

std::string ServicioVivos::cuentaLista(const std::string& plataforma, const std::string& cuenta) const {
    if (!esPlataforma(plataforma)) return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una plataforma «%1» (youtube, twitch o facebook)."), {plataforma});
    const Cuenta* c = p_.cuentas.buscar(plataforma, cuenta);
    if (c == nullptr)
        return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una cuenta «%1» de %2 (cuentas_listar las lista)."),
                                {cuenta.empty() ? std::string(kPrincipal) : cuenta, nombrePlataforma(plataforma)});
    const Plataforma* pl = p_.plataforma ? p_.plataforma(plataforma) : nullptr;
    if (pl == nullptr || !pl->configurada())
        return ba::i18n::llenar(ba::i18n::traducir("vivos", "Falta la app de %1: seguí account_setup_guide."), {nombrePlataforma(plataforma)});
    if (!pl->conectada(c->id))
        return ba::i18n::llenar(c->conectadaAntes ? ba::i18n::traducir("vivos", "La cuenta «%1» de %2 está vencida: volvé a autorizarla (cuenta_conectar).")
                                                  : ba::i18n::traducir("vivos", "La cuenta «%1» de %2 no está conectada (cuenta_conectar)."),
                                {c->nombre, nombrePlataforma(plataforma)});
    return {};
}

void ServicioVivos::asignar(Asignacion a, std::function<void(const std::string&, const std::string&)> listo) {
    json d;
    if (std::string e = armarDestino(a, d); !e.empty()) return listo(e, {});
    if (p_.nombreSalida && p_.nombreSalida(a.salida).empty())
        return listo(ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una salida «%1»."), {a.salida}), {});
    const std::string p = str(d, "plataforma");
    const json guardados = p_.destinos ? p_.destinos() : json::array();
    // Un genérico: la huella de la clave nueva (o la guardada, si edita sin cambiarla).
    const std::string h = esPlataforma(p) ? std::string() : huella(a.clave);
    if (std::string e = chocaCon(guardados, d, h, p_.huellaDe, p_.nombreSalida); !e.empty()) return listo(e, {});
    if (!esPlataforma(p)) {
        if (!a.clave.empty()) d["clave"] = a.clave;
        return p_.guardar(d, std::move(listo));
    }
    if (std::string e = cuentaLista(p, str(d, "cuenta")); !e.empty()) return listo(e, {});
    Plataforma* pl = p_.plataforma(p);
    // La ingesta (en YouTube: crea y enlaza el stream si el vivo no tiene). La clave va
    // cifrada con el destino; arrancar y volver tras un corte no necesitan la red.
    std::weak_ptr<int> vida = vida_;
    pl->ingesta(str(d, "cuenta"), str(d, "vivo"), [this, vida, d, listo](const Resultado& r) mutable {
        if (vida.expired()) return;
        if (!r.ok) return listo(r.error.empty() ? ba::i18n::traducir("vivos", "La plataforma no dio la ingesta.") : r.error, {});
        const std::string url = str(r.datos, "url");
        const std::string clave = str(r.datos, "clave");
        if (url.empty() || clave.empty()) return listo(ba::i18n::traducir("vivos", "La plataforma no dio la dirección o la clave de ingesta."), {});
        d["url"] = url;
        d["clave"] = clave;
        if (r.datos.contains("estado_stream") || r.datos.contains("salud")) {
            json s = json::object();
            for (const char* k : {"estado_stream", "salud"})
                if (r.datos.contains(k)) s[k] = r.datos[k];
            const std::string id = str(d, "id");
            if (!id.empty()) salud_[id] = s;
        }
        p_.guardar(d, std::move(listo));
    });
}

json ServicioVivos::saludDe(const std::string& destino) const {
    const auto it = salud_.find(destino);
    return it != salud_.end() ? it->second : json::object();
}

json ServicioVivos::estado(const json& conEstado, const json& kbps, const std::string& salida) const {
    json out = json::array();
    if (!conEstado.is_array()) return out;
    // Los choques de lo guardado (un archivo editado a mano, o de antes del aviso).
    std::map<std::string, std::string> choqueDe;
    for (const Choque& c : choques(conEstado, p_.huellaDe)) {
        std::string salidas;
        for (const std::string& s : c.salidas) {
            std::string n = p_.nombreSalida ? p_.nombreSalida(s) : std::string();
            salidas += (salidas.empty() ? "" : ", ") + (n.empty() ? s : n);
        }
        for (const std::string& id : c.destinos)
            choqueDe[id] = ba::i18n::llenar(ba::i18n::traducir("vivos", "Manda al mismo vivo que otro destino (salidas: %1): se pisan."), {salidas});
    }
    for (const json& d : conEstado) {
        std::string sal = str(d, "salida");
        if (sal.empty()) sal = "principal";
        if (!salida.empty() && sal != salida) continue;
        const std::string id = str(d, "id");
        const std::string p = plataformaDe(d);
        const double k = kbps.is_object() && kbps.contains(id) && kbps[id].is_number() ? kbps[id].get<double>() : 0.0;
        const bool activo = d.value("activo", false);
        const bool reconectando = d.value("reconectando", false);
        const std::int64_t bytes = d.contains("bytes") && d["bytes"].is_number() ? d["bytes"].get<std::int64_t>() : 0;
        const std::string e = estadoDestino(activo, reconectando, bytes, k);
        json fila{{"id", id},
                  {"nombre", str(d, "nombre")},
                  {"salida", sal},
                  {"plataforma", p},
                  {"plataforma_nombre", nombrePlataforma(p)},
                  {"cuenta", esPlataforma(p) ? cuentaDe(d) : std::string()},
                  {"cuenta_nombre", std::string()},
                  {"vivo", str(d, "vivo")},
                  {"vivo_titulo", str(d, "vivo_titulo")},
                  {"estado", e},
                  {"estado_texto", textoEstadoDestino(e)},
                  {"kbps", static_cast<int>(k + 0.5)},
                  {"activo", activo},
                  {"reconectando", reconectando},
                  {"respaldo", d.value("respaldo", false)},
                  {"salud", activo ? saludDe(id) : json::object()},
                  {"choque", choqueDe.count(id) != 0 ? choqueDe[id] : std::string()}};
        if (esPlataforma(p))
            if (const Cuenta* c = p_.cuentas.buscar(p, cuentaDe(d))) fila["cuenta_nombre"] = c->nombre;
        out.push_back(std::move(fila));
    }
    return out;
}

void ServicioVivos::sondearSalud(const json& conEstado, std::function<void()> alCambiar) {
    std::set<std::string> activos;
    if (conEstado.is_array())
        for (const json& d : conEstado) {
            const std::string id = str(d, "id");
            const std::string p = plataformaDe(d);
            if (!d.value("activo", false) || !esPlataforma(p)) continue;
            activos.insert(id);
            Plataforma* pl = p_.plataforma ? p_.plataforma(p) : nullptr;
            if (pl == nullptr || !pl->conectada(cuentaDe(d))) continue;
            std::weak_ptr<int> vida = vida_;
            pl->salud(cuentaDe(d), str(d, "vivo"), [this, vida, id, alCambiar](const Resultado& r) {
                if (vida.expired() || !r.ok) return;
                json& s = salud_[id];
                if (s == r.datos) return;
                s = r.datos;
                if (alCambiar) alCambiar();
            });
        }
    // Lo de los que ya no salen se olvida (no queda «excelente» de un vivo cortado).
    bool olvido = false;
    for (auto it = salud_.begin(); it != salud_.end();)
        if (activos.count(it->first) == 0) {
            it = salud_.erase(it);
            olvido = true;
        } else {
            ++it;
        }
    if (olvido && alCambiar) alCambiar();
}

} // namespace ba::app::vivos
