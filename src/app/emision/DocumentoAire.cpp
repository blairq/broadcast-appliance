// src/app/emision/DocumentoAire.cpp — ver el .hpp

#include "emision/DocumentoAire.hpp"

#include "persist/ColaEscritura.hpp"

#include <utility>

namespace ba::app::salidas {

using json = nlohmann::json;

namespace {
std::string texto(const AlAire& a) { return aJson(a).dump(-1, ' ', false, json::error_handler_t::replace); }
} // namespace

DocumentoAire::DocumentoAire(std::filesystem::path runtime) : carpeta_(std::move(runtime)) {}

void DocumentoAire::setCarpeta(std::filesystem::path runtime) {
    std::lock_guard<std::mutex> l(mu_);
    carpeta_ = std::move(runtime);
}

DocumentoAire::Anterior DocumentoAire::leerAnterior() {
    Anterior r;
    // Por la cola: si algo quedó encolado y todavía no llegó al disco, eso es lo último.
    const ba::persist::Lectura l = ba::persist::cola().leerGeneraciones(archivo());
    r.estado = l.estado;
    r.descartadas = l.descartadas;
    const json j = json::parse(l.cuerpo, nullptr, false);
    if (l.estado == ba::persist::Lectura::Estado::Nada) {
        r.aire = {}; // primer arranque (o una versión 1 sin al-aire.json): las secciones sueltas igual
        r.aire.version = 1;
        r.aire.salidas.clear();
    } else if (j.is_object()) {
        r.aire = leerAlAire(j);
    } else {
        // Las dos generaciones rotas (o el archivo viejo roto): no se sabe qué había al
        // aire. Lo peor es arrancar callado con «nada al aire»: cuenta como un corte.
        r.danado = true;
        r.aire = {};
        r.aire.cerrado = false;
    }
    // La versión 1: el negro y la placa, y la línea, estaban en sus propios archivos.
    // Se leen una versión más (el primer volcado los borra).
    if (!r.danado && r.aire.version < kVersionAire) {
        std::string placa;
        const bool hayPlaca = ba::persist::cola().leer(archivoPlacaViejo(), placa);
        const ba::persist::Lectura ll = ba::persist::cola().leerGeneraciones(archivoLineaViejo());
        json linea;
        if (ll.estado == ba::persist::Lectura::Estado::Valida || ll.estado == ba::persist::Lectura::Estado::Vieja) {
            linea = json::parse(ll.cuerpo, nullptr, false);
            if (!linea.is_object()) r.lineaDanada = true;
        } else if (ll.estado == ba::persist::Lectura::Estado::Danada) {
            r.lineaDanada = true;
        }
        r.deVersionVieja = hayPlaca || ll.estado != ba::persist::Lectura::Estado::Nada;
        sumarSeccionesViejas(r.aire, hayPlaca ? json::parse(placa, nullptr, false) : json(), linea);
        // Una línea viva sin al-aire.json (nunca pasó: la escena siempre se escribe) igual
        // es de una corrida que no se cerró.
        if (l.estado == ba::persist::Lectura::Estado::Nada && r.aire.linea.is_object()) r.aire.cerrado = false;
    }
    std::lock_guard<std::mutex> g(mu_);
    doc_ = r.aire;
    doc_.version = kVersionAire;
    escrito_.clear();
    sucio_ = false;
    borrarViejos_ = r.deVersionVieja;
    return r;
}

bool DocumentoAire::ponerSalidas(std::map<std::string, Aire> salidas, bool cerrado) {
    std::unique_lock<std::mutex> l(mu_);
    AlAire nuevo = doc_;
    nuevo.salidas = std::move(salidas);
    nuevo.cerrado = cerrado;
    if (texto(nuevo) == texto(doc_)) return false;
    doc_ = std::move(nuevo);
    cambio(l);
    return true;
}

bool DocumentoAire::ponerLinea(json linea) {
    std::unique_lock<std::mutex> l(mu_);
    if (!linea.is_object()) linea = json();
    if (linea == doc_.linea) return false;
    doc_.linea = std::move(linea);
    cambio(l);
    return true;
}

void DocumentoAire::cambio(std::unique_lock<std::mutex>& l) {
    sucio_ = true;
    const std::function<void()> despertar = despertar_;
    l.unlock();
    // Sin dueño (las pruebas, la consola antes de arrancar), el que pone escribe.
    if (despertar)
        despertar();
    else
        volcar();
}

AlAire DocumentoAire::foto() const {
    std::lock_guard<std::mutex> l(mu_);
    return doc_;
}

bool DocumentoAire::hayAire() const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [id, s] : doc_.salidas)
        if (s.transmitiendo || s.grabando || !s.destinos.empty()) return true;
    return false;
}

void DocumentoAire::setDueno(std::function<void()> despertar) {
    std::lock_guard<std::mutex> l(mu_);
    despertar_ = std::move(despertar);
}

bool DocumentoAire::volcar() {
    std::lock_guard<std::mutex> l(mu_);
    if (!sucio_) return false;
    sucio_ = false;
    std::string t = texto(doc_);
    if (t == escrito_) return false;
    escrito_ = t;
    ++escrituras_;
    // Encolado CON el candado tomado: dos volcados de hilos distintos llegan a la cola en
    // el orden en que se armaron (la cola se queda con el último de cada archivo).
    ba::persist::cola().escribirGeneracion(archivo(), std::move(t));
    if (borrarViejos_) {
        // El documento ya tiene las secciones: los archivos de la versión 1 no vuelven a
        // aparecer desactualizados (van detrás de la escritura, con la misma prioridad).
        borrarViejos_ = false;
        ba::persist::cola().borrar(archivoPlacaViejo(), ba::persist::Prioridad::Aire);
        ba::persist::cola().borrar(archivoLineaViejo(), ba::persist::Prioridad::Aire);
    }
    return true;
}

std::uint64_t DocumentoAire::escrituras() const {
    std::lock_guard<std::mutex> l(mu_);
    return escrituras_;
}

} // namespace ba::app::salidas
