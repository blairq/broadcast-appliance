// src/app/pantallas/Destinos.cpp — ver Destinos.hpp
#include "pantallas/Destinos.hpp"

#include <algorithm>

#include "i18n/I18nTexto.hpp"

namespace ba::app::pantallas {

using nlohmann::json;

const std::vector<Contenido>& contenidos() {
    static const std::vector<Contenido> c = {
        {"mensaje",
         QT_TRANSLATE_NOOP("pantallas", "Un mensaje"),
         QT_TRANSLATE_NOOP("pantallas", "Texto grande, para que se lea desde el fondo de la sala."),
         {{"texto", QT_TRANSLATE_NOOP("pantallas", "Texto"), QT_TRANSLATE_NOOP("pantallas", "Empezamos en 5 minutos"), true},
          {"pie", QT_TRANSLATE_NOOP("pantallas", "Abajo, más chico"), QT_TRANSLATE_NOOP("pantallas", "Sala principal"), false}},
         true,
         ""},
        {"qr",
         QT_TRANSLATE_NOOP("pantallas", "Un código QR"),
         QT_TRANSLATE_NOOP("pantallas", "El QR ocupa la pantalla entera: se escanea desde lejos. Uno del aparato (el del Público, el de "
         "la Conferencia…), uno guardado en «Códigos QR» con su estilo, o lo que se escriba."),
         {{"guardado", QT_TRANSLATE_NOOP("pantallas", "Qué QR"), "", false, "qrs"},
          {"texto", QT_TRANSLATE_NOOP("pantallas", "Qué codifica (si es a mano)"), "https://…", false},
          {"pie", QT_TRANSLATE_NOOP("pantallas", "Abajo, más chico"), QT_TRANSLATE_NOOP("pantallas", "Escaneá para entrar a la sala"), false}},
         true,
         ""},
        {"reloj",
         QT_TRANSLATE_NOOP("pantallas", "La hora"),
         QT_TRANSLATE_NOOP("pantallas", "La hora, grande. Para el fondo del escenario."),
         {{"pie", QT_TRANSLATE_NOOP("pantallas", "Abajo, más chico"), "", false}},
         true,
         ""},
        {"url",
         QT_TRANSLATE_NOOP("pantallas", "Una página web"),
         QT_TRANSLATE_NOOP("pantallas", "Cualquier dirección. Tiene que ser alcanzable DESDE el televisor."),
         {{"url", QT_TRANSLATE_NOOP("pantallas", "Dirección"), "http://…", false}},
         true,
         ""},
        {"prompter", QT_TRANSLATE_NOOP("pantallas", "El teleprompter"), QT_TRANSLATE_NOOP("pantallas", "La vista del orador: el discurso que esté cargado."), {}, true, ""},
        // La votación que esté abierta, con su QR para escanear desde la
        // butaca. No lleva campos: lo que muestra es lo que haya en la urna en
        // ese momento, y no hay nada que elegir — una urna por vez.
        {"votacion",
         QT_TRANSLATE_NOOP("pantallas", "La votación"),
         QT_TRANSLATE_NOOP("pantallas", "La que esté abierta, con las opciones y el QR para votar desde el teléfono. El resultado se "
         "ve solo cuando corresponde: con la urna cerrada, o abierta si se decidió mostrarlo. Si no "
         "hay ninguna, dice que no hay."),
         {},
         true,
         ""},
        {"tally",
         QT_TRANSLATE_NOOP("pantallas", "Tally de cámara"),
         QT_TRANSLATE_NOOP("pantallas", "Rojo si esa cámara está AL AIRE, verde si está armada para el próximo take. Se mira de reojo "
         "desde atrás de la cámara, así que es la pantalla entera de color. `fuente` es el nombre de la "
         "cámara o de la escena; vacío muestra el estado general, que sirve para la cabina."),
         {{"fuente", QT_TRANSLATE_NOOP("pantallas", "Cámara o escena"), QT_TRANSLATE_NOOP("pantallas", "Cam 1"), false}},
         true,
         ""},
        // El programa entra por el mismo camino que un invitado, así que el
        // enlace lo tiene que acuñar el gateway (lleva un permiso de ingreso):
        // por eso es el único contenido cuya URL no se arma acá.
        {"programa",
         QT_TRANSLATE_NOOP("pantallas", "Lo que sale al aire"),
         QT_TRANSLATE_NOOP("pantallas", "El programa en vivo por WebRTC: unas décimas de retardo, no los segundos del reproductor de "
         "fábrica del televisor. La pantalla entra a la conferencia como una más: si el lobby está prendido, "
         "hay que hacerla pasar, y ocupa un lugar mientras dure."),
         {},
         true,
         ""},
        // Una escena cualquiera, no la que está al aire: la pantalla del hall con
        // la cámara del recinto mientras el programa muestra otra cosa. Viaja
        // como el programa (WebRTC, enlace del gateway), pero por un canal propio
        // (BA.Escena.<k>): la escena se compone aparte y sigue aunque no esté al
        // aire. Varias pantallas pueden mostrar escenas distintas a la vez.
        {"escena",
         QT_TRANSLATE_NOOP("pantallas", "Una escena"),
         QT_TRANSLATE_NOOP("pantallas", "Cualquiera de las escenas armadas, en vivo, esté o no al aire. Varias pantallas pueden mostrar "
         "escenas distintas a la vez (hasta 4 escenas distintas; las pantallas con la misma escena "
         "comparten). Cada escena que se muestra es una composición más para la máquina."),
         {{"escena", QT_TRANSLATE_NOOP("pantallas", "Escena"), "", false, "escenas"}},
         true,
         ""},
    };
    return c;
}

const Contenido* contenido(const std::string& id) {
    for (const Contenido& c : contenidos())
        if (id == c.id) return &c;
    return nullptr;
}

std::string texto(const char* s) { return *s ? i18n::traducir("pantallas", s) : std::string(); }

namespace {

/// Un dato como texto (lo que se guarda y lo que va en la URL).
std::string cadena(const json& datos, const std::string& clave) {
    if (!datos.is_object()) return {};
    const auto it = datos.find(clave);
    if (it == datos.end() || it->is_null()) return {};
    return it->is_string() ? it->get<std::string>() : it->dump();
}

std::string recortar(const std::string& s) {
    const char* blancos = " \t\n\r\f\v";
    const auto a = s.find_first_not_of(blancos);
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(blancos) - a + 1);
}

/// Las primeras `n` letras (no bytes) de un texto en UTF-8, con «…» si sobraba.
std::string cortar(const std::string& s, std::size_t n) {
    std::size_t letras = 0, i = 0;
    for (; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) continue; // continuación de una letra
        if (letras == n) return s.substr(0, i) + "…";
        ++letras;
    }
    return s;
}

json datosGuardados(const json& datos) {
    json out = json::object();
    for (auto it = datos.begin(); datos.is_object() && it != datos.end(); ++it) out[it.key()] = cadena(datos, it.key());
    return out;
}

json datosLeidos(const json& j) {
    json out = json::object();
    for (auto it = j.begin(); j.is_object() && it != j.end(); ++it)
        if (it.value().is_string()) out[it.key()] = it.value();
    return out;
}

} // namespace

std::string codificar(const std::string& s) {
    static const char* h = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
            c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += h[c >> 4];
            out += h[c & 15];
        }
    }
    return out;
}

std::string url(const std::string& tipo, const json& datos, const std::string& base) {
    const Contenido* c = contenido(tipo);
    if (c == nullptr || !c->disponible) return {};
    if (tipo == "url") return recortar(cadena(datos, "url"));
    // El programa lleva un permiso de ingreso que acuña el gateway: la URL no
    // se puede armar acá. `mostrar` la pide y recién después manda.
    if (esEnVivo(tipo)) return i18n::traducir("PantallasViewModel", "(la pide el gateway al mandar)");
    if (base.empty()) return {};
    if (tipo == "prompter") return base + "/prompter/";
    // La votacion tiene pagina propia porque es la unica que CONSULTA: las
    // demas son estaticas y lo que muestran viaja en la URL. Le van la clave
    // del publico —para poder leer la urna— y la direccion que va adentro del
    // QR, que es la misma que escanean los telefonos.
    if (tipo == "votacion") {
        std::string u = base + "/pantalla/votacion/";
        const std::string clave = cadena(datos, "clavePublico");
        const std::string entrada = cadena(datos, "entrada");
        if (!clave.empty()) u += "?k=" + clave;
        if (!entrada.empty()) u += std::string(clave.empty() ? "?" : "&") + "entrada=" + codificar(entrada);
        return u;
    }
    // El tally tiene pagina propia: es la pantalla ENTERA de color y no se
    // parece en nada a las demas.
    if (tipo == "tally") {
        const std::string f = recortar(cadena(datos, "fuente"));
        return base + "/pantalla/tally/" + (f.empty() ? std::string() : "?fuente=" + codificar(f));
    }
    std::string u = base + "/pantalla/?tipo=" + tipo;
    for (const Campo& f : c->campos) {
        if (std::string(f.opciones) == "qrs") continue; // lo elegido ya se volvió texto y estilo
        const std::string v = cadena(datos, f.clave);
        if (v.empty()) continue;
        u += std::string("&") + f.clave + "=" + codificar(v);
    }
    // El estilo de un código guardado viaja ya armado (ba::qr::aConsulta), no
    // campo por campo: el que sabe qué lleva un estilo es el generador, y si
    // se copiara la lista acá habría dos listas que mantener de acuerdo. Esa
    // es exactamente la forma del error que estamos arreglando.
    const std::string estilo = cadena(datos, "estilo");
    if (!estilo.empty()) u += "&" + estilo;
    return u;
}

std::string que(const std::string& tipo, const json& datos, std::size_t largo) {
    const Contenido* c = contenido(tipo);
    if (c == nullptr) return tipo;
    std::string q = texto(c->titulo);
    for (const Campo& f : c->campos) {
        const std::string v = recortar(cadena(datos, f.clave));
        if (!v.empty()) return q + " — " + cortar(v, largo);
    }
    return q;
}

// --- Lo guardado ------------------------------------------------------------------------------

json Destinos::aJson() const {
    json o = json::array();
    for (const Destino& d : destinos_) {
        // Las descubiertas también se guardan: así la página no arranca
        // vacía mientras se busca, y el operador ve lo que había ayer.
        json x{{"id", d.id}, {"nombre", d.nombre}, {"modelo", d.modelo}, {"puerto", d.puerto}, {"fija", d.fija}};
        // LA IP NO SE GUARDA para las que se descubren: el DHCP se la
        // cambia y guardarla solo sirve para hablarle a la dirección
        // equivocada. La identidad es el nombre de servicio mDNS, que no
        // cambia, y la dirección se vuelve a encontrar en cada búsqueda.
        // Las agregadas A MANO son la excepción: ahí la dirección ES lo
        // único que tenemos.
        if (d.fija) x["host"] = d.host;
        if (d.nombrePropio) x["nombrePropio"] = true;
        if (!d.fijadoTipo.empty()) {
            x["fijadoTipo"] = d.fijadoTipo;
            x["fijadoDatos"] = datosGuardados(d.fijadoDatos);
        }
        o.push_back(std::move(x));
    }
    json car = json::array();
    for (const Carrusel& c : carruseles_) {
        json pasos = json::array();
        for (const Paso& p : c.pasos)
            pasos.push_back(json{{"tipo", p.tipo}, {"datos", datosGuardados(p.datos)}, {"segundos", p.segundos}});
        car.push_back(json{{"id", c.id}, {"nombre", c.nombre}, {"transicion", c.transicion}, {"pasos", pasos}});
    }
    // Se guardan los dos juntos: las pantallas y lo que rota en ellas son
    // la misma configuración de sala.
    return {{"pantallas", o}, {"carruseles", car}};
}

void Destinos::deJson(const json& raiz) {
    destinos_.clear();
    carruseles_.clear();
    if (!raiz.is_object()) return;
    for (const json& c : raiz.value("carruseles", json::array())) {
        if (!c.is_object()) continue;
        Carrusel x;
        x.id = c.value("id", "");
        x.nombre = c.value("nombre", "");
        x.transicion = c.value("transicion", "fundido");
        for (const json& p : c.value("pasos", json::array())) {
            if (!p.is_object()) continue;
            Paso paso;
            paso.tipo = p.value("tipo", "");
            paso.segundos = p.value("segundos", 15);
            paso.datos = datosLeidos(p.value("datos", json::object()));
            if (!paso.tipo.empty()) x.pasos.push_back(std::move(paso));
        }
        if (!x.id.empty()) carruseles_.push_back(std::move(x));
    }
    const json o = raiz.value("pantallas", json::array());
    if (!o.is_array()) return;
    for (const json& x : o) {
        if (!x.is_object()) continue;
        Destino d;
        d.id = x.value("id", "");
        d.nombre = x.value("nombre", "");
        d.modelo = x.value("modelo", "");
        d.host = x.value("host", "");
        d.puerto = x.value("puerto", kPuertoCast);
        d.fija = x.value("fija", false);
        d.nombrePropio = x.value("nombrePropio", false);
        d.fijadoTipo = x.value("fijadoTipo", "");
        // Sin dirección todavía (descubierta): queda a la espera de la
        // búsqueda de arranque, que le pone la de hoy.
        if (!d.fija && d.host.empty()) d.estado = "buscando";
        d.fijadoDatos = datosLeidos(x.value("fijadoDatos", json::object()));
        // Una agregada a mano sin dirección no sirve para nada; una
        // descubierta sin dirección sí: la búsqueda se la va a dar.
        if (d.id.empty() || (d.fija && d.host.empty())) continue;
        destinos_.push_back(std::move(d));
    }
}

Destino* Destinos::porId(const std::string& id) {
    for (Destino& d : destinos_)
        if (d.id == id) return &d;
    return nullptr;
}
const Destino* Destinos::porId(const std::string& id) const { return const_cast<Destinos*>(this)->porId(id); }

Carrusel* Destinos::carrusel(const std::string& id) {
    for (Carrusel& c : carruseles_)
        if (c.id == id) return &c;
    return nullptr;
}
const Carrusel* Destinos::carrusel(const std::string& id) const { return const_cast<Destinos*>(this)->carrusel(id); }

int Destinos::mostrando() const {
    return static_cast<int>(std::count_if(destinos_.begin(), destinos_.end(), [](const Destino& d) { return d.estado == "mostrando"; }));
}

bool Destinos::muestraPrograma() const {
    return std::any_of(destinos_.begin(), destinos_.end(),
                       [](const Destino& d) { return d.contenido == "programa" && (d.estado == "mostrando" || d.trabajando); });
}

std::set<int> Destinos::canalesUsados() const {
    std::set<int> usados;
    for (const Destino& d : destinos_)
        if (d.canalEscena >= 0 && (d.trabajando || (d.contenido == "escena" && d.estado == "mostrando"))) usados.insert(d.canalEscena);
    return usados;
}

bool Destinos::pidiendoCanal() const {
    return std::any_of(destinos_.begin(), destinos_.end(), [](const Destino& d) { return d.pidiendoCanal; });
}

// --- Lo que hace el operador ------------------------------------------------------------------

std::string Destinos::agregar(const std::string& nombre, const std::string& host, int puerto, std::string* idNuevo) {
    const std::string h = recortar(host);
    if (h.empty()) return i18n::traducir("PantallasViewModel", "Poné la dirección IP del televisor.");
    const std::string n = recortar(nombre).empty() ? h : recortar(nombre);
    const std::string id = "manual:" + h;
    if (porId(id) != nullptr) return i18n::traducir("PantallasViewModel", "Ya está en la lista.");
    Destino d;
    d.id = id;
    d.nombre = n;
    d.host = h;
    d.puerto = puerto > 0 ? puerto : kPuertoCast;
    d.fija = true;
    destinos_.push_back(std::move(d));
    if (idNuevo != nullptr) *idNuevo = id;
    return {};
}

std::string Destinos::olvidar(const std::string& id) {
    const auto it = std::find_if(destinos_.begin(), destinos_.end(), [&](const Destino& d) { return d.id == id; });
    if (it == destinos_.end()) return i18n::traducir("PantallasViewModel", "No la encuentro.");
    if (it->trabajando) return i18n::traducir("PantallasViewModel", "Esperá a que termine lo que está haciendo.");
    destinos_.erase(it);
    return {};
}

std::string Destinos::renombrar(const std::string& id, const std::string& nombre) {
    Destino* d = porId(id);
    if (d == nullptr) return i18n::traducir("PantallasViewModel", "No la encuentro.");
    const std::string n = recortar(nombre);
    if (n.empty()) return i18n::traducir("PantallasViewModel", "Ponele un nombre.");
    d->nombre = n;
    // Que la próxima búsqueda no le pise el nombre. NO la marca como «a mano»:
    // eso haría que se guardara su IP, y las IP cambian.
    d->nombrePropio = true;
    return {};
}

std::string Destinos::sePuedeFijar(const std::string& id, const std::string& tipo) const {
    if (porId(id) == nullptr) return i18n::traducir("PantallasViewModel", "No la encuentro.");
    // Un carrusel también se puede dejar fijo, y es el caso principal: «el
    // televisor del hall siempre con esto».
    if (tipo.rfind("carrusel:", 0) == 0)
        return carrusel(tipo.substr(9)) == nullptr ? i18n::traducir("PantallasViewModel", "No encuentro ese carrusel.") : std::string();
    const Contenido* c = contenido(tipo);
    if (c == nullptr) return i18n::llenar(i18n::traducir("PantallasViewModel", "No sé mostrar «%1»."), {tipo});
    if (!c->disponible) return texto(c->porque);
    return {};
}

std::string Destinos::fijar(const std::string& id, const std::string& tipo, const json& datos) {
    if (std::string e = sePuedeFijar(id, tipo); !e.empty()) return e;
    Destino* d = porId(id);
    d->fijadoTipo = tipo;
    d->fijadoDatos = datos.is_object() ? datos : json::object();
    d->fallos = 0;
    d->proximoIntentoMs = 0;
    return {};
}

std::string Destinos::fijarLoQueMuestra(const std::string& id) {
    Destino* d = porId(id);
    if (d == nullptr) return i18n::traducir("PantallasViewModel", "No la encuentro.");
    if (d->contenido.empty())
        return i18n::traducir("PantallasViewModel", "Esta pantalla no está mostrando nada nuestro: mandale algo primero.");
    d->fijadoTipo = d->contenido;
    d->fijadoDatos = d->ultimosDatos;
    d->fallos = 0;
    return {};
}

std::string Destinos::desfijar(const std::string& id) {
    Destino* d = porId(id);
    if (d == nullptr) return i18n::traducir("PantallasViewModel", "No la encuentro.");
    d->fijadoTipo.clear();
    d->fijadoDatos = json::object();
    return {};
}

// --- Carruseles -------------------------------------------------------------------------------

namespace {
/// Como QString::number(n, 36): en minúsculas.
std::string base36(std::int64_t n) {
    if (n == 0) return "0";
    const bool neg = n < 0;
    std::uint64_t u = neg ? static_cast<std::uint64_t>(-n) : static_cast<std::uint64_t>(n);
    std::string s;
    while (u > 0) {
        s.insert(s.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[u % 36]);
        u /= 36;
    }
    return neg ? "-" + s : s;
}
} // namespace

std::string Destinos::carruselNuevo(const std::string& nombre, std::int64_t ahora) {
    Carrusel c;
    c.id = "car" + base36(ahora);
    c.nombre = recortar(nombre).empty() ? "Carrusel" : recortar(nombre);
    carruseles_.push_back(c);
    return c.id;
}

std::string Destinos::carruselBorrar(const std::string& id) {
    const auto it = std::find_if(carruseles_.begin(), carruseles_.end(), [&](const Carrusel& c) { return c.id == id; });
    if (it == carruseles_.end()) return i18n::traducir("PantallasViewModel", "No lo encuentro.");
    carruseles_.erase(it);
    return {};
}

std::string Destinos::carruselRenombrar(const std::string& id, const std::string& nombre) {
    Carrusel* c = carrusel(id);
    if (c == nullptr) return i18n::traducir("PantallasViewModel", "No lo encuentro.");
    if (recortar(nombre).empty()) return i18n::traducir("PantallasViewModel", "Ponele un nombre.");
    c->nombre = recortar(nombre);
    return {};
}

std::string Destinos::carruselTransicion(const std::string& id, const std::string& transicion) {
    Carrusel* c = carrusel(id);
    if (c == nullptr) return i18n::traducir("PantallasViewModel", "No lo encuentro.");
    c->transicion = transicion == "corte" ? "corte" : "fundido";
    return {};
}

std::string Destinos::pasoAgregar(const std::string& carruselId, const std::string& tipo, const json& datos, int segundos) {
    Carrusel* c = carrusel(carruselId);
    if (c == nullptr) return i18n::traducir("PantallasViewModel", "No encuentro ese carrusel.");
    const Contenido* con = contenido(tipo);
    if (con == nullptr) return i18n::llenar(i18n::traducir("PantallasViewModel", "No sé mostrar «%1»."), {tipo});
    // Lo que va en vivo entra con un permiso de UN SOLO USO que acuña el
    // gateway al mandarlo: un paso de carrusel es una URL fija y no lo tiene.
    if (esEnVivo(tipo))
        return i18n::traducir("PantallasViewModel", "Lo que va en vivo (el programa o una escena) no entra en un carrusel: mandalo directo a la "
                                                    "pantalla.");
    if (!con->disponible) return texto(con->porque);
    Paso p;
    p.tipo = tipo;
    p.datos = datos.is_object() ? datos : json::object();
    p.segundos = std::clamp(segundos <= 0 ? 15 : segundos, 3, 3600);
    c->pasos.push_back(std::move(p));
    return {};
}

std::string Destinos::pasoQuitar(const std::string& carruselId, int indice) {
    Carrusel* c = carrusel(carruselId);
    if (c == nullptr) return i18n::traducir("PantallasViewModel", "No encuentro ese carrusel.");
    if (indice < 0 || indice >= static_cast<int>(c->pasos.size())) return i18n::traducir("PantallasViewModel", "Ese paso no está.");
    c->pasos.erase(c->pasos.begin() + indice);
    return {};
}

std::string Destinos::pasoMover(const std::string& carruselId, int indice, int aIndice) {
    Carrusel* c = carrusel(carruselId);
    if (c == nullptr) return i18n::traducir("PantallasViewModel", "No encuentro ese carrusel.");
    const int n = static_cast<int>(c->pasos.size());
    if (indice < 0 || indice >= n || aIndice < 0 || aIndice >= n) return i18n::traducir("PantallasViewModel", "Ese paso no está.");
    Paso p = c->pasos[static_cast<std::size_t>(indice)];
    c->pasos.erase(c->pasos.begin() + indice);
    c->pasos.insert(c->pasos.begin() + aIndice, std::move(p));
    return {};
}

std::string Destinos::pasoSegundos(const std::string& carruselId, int indice, int segundos) {
    Carrusel* c = carrusel(carruselId);
    if (c == nullptr) return i18n::traducir("PantallasViewModel", "No encuentro ese carrusel.");
    if (indice < 0 || indice >= static_cast<int>(c->pasos.size())) return i18n::traducir("PantallasViewModel", "Ese paso no está.");
    c->pasos[static_cast<std::size_t>(indice)].segundos = std::clamp(segundos, 3, 3600);
    return {};
}

std::string Destinos::destinoDeCarrusel(const std::string& id) const {
    for (const Destino& d : destinos_)
        if (d.contenido == "carrusel:" + id) return d.id;
    return destinos_.empty() ? std::string() : destinos_.front().id;
}

// --- Lo que vuelve de los aparatos ----------------------------------------------------------

void Destinos::alBuscar(const std::vector<Encontrado>& encontradas, std::int64_t ahora) {
    for (const Encontrado& e : encontradas) {
        Destino* d = porId(e.id);
        if (d == nullptr) {
            destinos_.push_back(Destino{});
            d = &destinos_.back();
            d->id = e.id;
        }
        // La dirección se refresca SIEMPRE (para eso se busca). El nombre, solo si el
        // operador no le puso uno.
        if (!d->nombrePropio) d->nombre = e.nombre;
        d->modelo = e.modelo;
        d->host = e.host;
        d->puerto = e.puerto;
        d->vistaMs = ahora;
    }
    // Un aparato que vuelve a aparecer (prendido de nuevo, o con otra IP) retoma lo suyo
    // enseguida, sin esperar la ronda.
    for (Destino& d : destinos_) d.proximoIntentoMs = 0;
}

void Destinos::alConsultar(const std::string& id, const Estado& r, std::int64_t ahora) {
    Destino* d = porId(id);
    if (d == nullptr) return;
    d->trabajando = false;
    d->vistaMs = ahora;
    if (!r.ok) {
        d->estado = "apagada";
        d->error = r.error;
        d->contenido.clear();
        d->que.clear();
        return;
    }
    d->error.clear();
    d->volumen = r.volumen;
    d->silenciado = r.silenciado;
    d->puedeMostrar = r.puedeMostrar;
    if (r.appId == kAppDashCast) {
        d->estado = "mostrando";
    } else if (r.appId.empty()) {
        d->estado = "libre";
        d->contenido.clear();
        d->que.clear();
    } else {
        // Hay otra cosa corriendo (Netflix, YouTube): se avisa en vez de pisarla sin preguntar.
        d->estado = "ocupada";
        d->contenido.clear();
        d->que = r.appNombre.empty() ? r.appId : r.appNombre;
    }
}

bool Destinos::alMostrar(const std::string& id, bool ok, const std::string& error, const std::string& tipo,
                         const std::string& queTexto, std::int64_t ahora) {
    Destino* d = porId(id);
    if (d == nullptr) return false;
    d->trabajando = false;
    d->vistaMs = ahora;
    if (ok) {
        d->estado = "mostrando";
        d->contenido = tipo;
        d->que = queTexto;
        d->error.clear();
        d->fallos = 0;
        return false;
    }
    d->estado = "error";
    d->error = error;
    ++d->fallos;
    // Un aviso por cada reintento del restaurado sería una lluvia de avisos por un
    // televisor apagado: solo el primero.
    return d->fijadoTipo.empty() || d->fallos == 1;
}

void Destinos::alParar(const std::string& id, bool ok, const std::string& error, std::int64_t ahora) {
    Destino* d = porId(id);
    if (d == nullptr) return;
    d->trabajando = false;
    d->vistaMs = ahora;
    d->contenido.clear();
    d->que.clear();
    d->canalEscena = -1;
    if (ok) {
        d->estado = "libre";
        d->error.clear();
    } else {
        d->estado = "error";
        d->error = error;
    }
}

void Destinos::alSilenciar(const std::string& id, bool ok, double volumen, bool silenciado) {
    Destino* d = porId(id);
    if (d == nullptr) return;
    d->trabajando = false;
    if (ok) {
        d->volumen = volumen;
        d->silenciado = silenciado;
    }
}

std::vector<Destinos::Restaurar> Destinos::paraRestaurar(std::int64_t ahora) {
    std::vector<Restaurar> out;
    for (Destino& d : destinos_) {
        if (d.fijadoTipo.empty() || d.trabajando) continue;
        // Ya está mostrando lo que tiene que mostrar: nada que hacer.
        if (d.estado == "mostrando" && d.contenido == d.fijadoTipo) continue;
        // Ocupada con otra cosa (alguien puso Netflix): NO se le pisa sola. Que lo decida
        // una persona; para eso está el botón.
        if (d.estado == "ocupada") continue;
        if (ahora < d.proximoIntentoMs) continue;
        // La espera crece con los fallos: un televisor apagado se reintenta cada vez
        // menos, hasta una vez cada cinco minutos.
        d.proximoIntentoMs = ahora + std::min<std::int64_t>(15000LL * (1 + d.fallos), 300000LL);
        out.push_back({d.id, d.fijadoTipo, d.fijadoDatos});
    }
    return out;
}

// --- Los canales de escena del motor ---------------------------------------------------------

std::vector<std::pair<int, std::string>> aSoltar(const CanalesDelMotor& c, const std::set<int>& usados) {
    std::vector<std::pair<int, std::string>> out;
    for (int k = 0; k < c.cantidad; ++k) {
        const std::string escena = c.escena(k);
        if (escena.empty() || c.conDuenos(k) || usados.count(k) > 0) continue; // las vistas de los remotos
        out.emplace_back(k, escena);
    }
    return out;
}

int canalPara(const CanalesDelMotor& c, const std::string& escena) {
    for (int k = 0; k < c.cantidad; ++k)
        if (c.escena(k) == escena) return k;
    for (int k = 0; k < c.cantidad; ++k)
        if (c.escena(k).empty()) return k;
    return -1;
}

} // namespace ba::app::pantallas
