// src/app/arranque/Configuraciones.cpp — ver Configuraciones.hpp

#include "arranque/Configuraciones.hpp"

#include "evento/EventoStore.hpp"
#include "evento/FotosConfig.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <fstream>
#include <map>

namespace ba::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path deU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string aU8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

json leerJson(const fs::path& p) {
    std::ifstream f(p);
    json j = json::parse(f, nullptr, false);
    return j.is_object() ? j : json::object(); // sin archivo (o roto): vacío, nunca null
}

void escribirJson(const fs::path& p, const json& j) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::uintmax_t bytesDe(const fs::path& dir) {
    std::uintmax_t n = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) n += it->file_size(ec);
    return n;
}

/// Copia una configuración entera, menos lo que no se lleva (grabaciones, lo que
/// compartieron los invitados, lo preparado para restaurar). `sinHistorial` (un evento
/// nuevo): tampoco sus eventos, lo exportado ni la sesión (orden del día, quórum, votos).
std::string copiar(const fs::path& de, const fs::path& a, bool conGrabaciones, bool sinHistorial = false) {
    std::error_code ec;
    fs::create_directories(a, ec);
    for (const auto& e : fs::directory_iterator(de, ec)) {
        const std::string n = aU8(e.path().filename());
        if (n == "configuracion.json" || n == "Shares" || n == "Logs" || n == "Runtime" || (!conGrabaciones && n == "Recordings")) continue;
        if (sinHistorial && (n == "Eventos" || n == "Recordings" || n == "Exportado" || n == "Papelera")) continue;
        std::error_code e2;
        if (e.is_directory())
            fs::copy(e.path(), a / e.path().filename(), fs::copy_options::recursive | fs::copy_options::skip_existing, e2);
        else
            fs::copy_file(e.path(), a / e.path().filename(), fs::copy_options::skip_existing, e2);
        if (e2) return "no se pudo copiar " + n + ": " + e2.message();
    }
    fs::remove_all(a / "Config" / ".restaurar", ec);
    if (sinHistorial)
        for (const char* f : {"orden-del-dia.json", "quorum.json", "votaciones-oficiales.json", "al-aire.json"})
            fs::remove(a / "Config" / f, ec);
    return {};
}

/// Lo de fábrica que trae el programa (Graficos/, Extensiones/) y le falta.
void sembrar(const fs::path& a) {
    for (const char* d : {"Graficos", "Extensiones"}) {
        std::error_code ec;
        const fs::path de = ba::platform::bundleRoot() / d;
        if (fs::is_directory(de, ec))
            fs::copy(de, a / d, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    }
}
/// Copia TOTAL de un evento (copia es copia: web, guion, padrón, medios, orden del día,
/// votaciones, historial, grabaciones, lo compartido…). Solo quedan afuera lo de la app
/// andando (Logs/, Runtime/), su ficha (configuracion.json: la nueva tiene la suya) y el
/// estado al aire (al-aire.json: una copia no sale al aire sola al abrirla). Va sumando
/// en `hechos` los bytes copiados (el progreso que ve el operador).
std::string copiarTodo(const fs::path& de, const fs::path& a, std::atomic<std::uintmax_t>* hechos) {
    std::error_code ec;
    fs::create_directories(a, ec);
    if (ec) return "no se pudo crear " + aU8(a) + ": " + ec.message();
    for (auto it = fs::recursive_directory_iterator(de, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), de, ec);
        if (ec) return "no se pudo copiar: " + ec.message();
        const std::string arriba = aU8(*rel.begin());
        if (arriba == "configuracion.json" || arriba == "Logs" || arriba == "Runtime" ||
            (it.depth() == 1 && arriba == "Config" && aU8(rel.filename()) == "al-aire.json") ||
            (arriba == "Config" && rel.string().find(".restaurar") != std::string::npos)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        std::error_code e2;
        if (it->is_symlink(e2))
            fs::copy_symlink(it->path(), a / rel, e2);
        else if (it->is_directory(e2))
            fs::create_directories(a / rel, e2);
        else if (it->is_regular_file(e2)) {
            fs::create_directories((a / rel).parent_path(), e2);
            fs::copy_file(it->path(), a / rel, fs::copy_options::overwrite_existing, e2);
            if (!e2 && hechos) *hechos += it->file_size(e2);
        }
        if (e2) return "no se pudo copiar " + aU8(rel) + ": " + e2.message();
    }
    return ec ? "no se pudo recorrer " + aU8(de) + ": " + ec.message() : std::string();
}
} // namespace

Configuraciones::Configuraciones(std::function<QString()> reiniciar, QObject* parent)
    : QObject(parent), reiniciar_(std::move(reiniciar)) {
    progresoCopia_.setInterval(300);
    connect(&progresoCopia_, &QTimer::timeout, this, &Configuraciones::copiaCambio);
    // Cada vez que cambia la lista (crear, borrar, renombrar), se vuelve a medir.
    connect(this, &Configuraciones::cambio, this, [this]() {
        if (avisandoTamanos_) return; // el aviso de la medición misma
        if (midiendo_) otraVez_ = true; // cambió mientras se medía: al terminar, de nuevo
        else medir();
    });
    medir();
}

void Configuraciones::medir() {
    if (midiendo_.exchange(true)) return;
    QPointer<Configuraciones> yo(this);
    const fs::path raiz = datos() / "Configuraciones";
    QThreadPool::globalInstance()->start([yo, raiz]() {
        std::map<std::string, Tamano> t;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(raiz, ec)) {
            const std::string n = aU8(e.path().filename());
            if (!e.is_directory() || n.empty() || n[0] == '.') continue;
            t[n] = {bytesDe(e.path()), bytesDe(e.path() / "Recordings")};
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, t = std::move(t)]() mutable {
                if (!yo) return;
                {
                    std::lock_guard<std::mutex> l(yo->tamanosM_);
                    yo->tamanos_ = std::move(t);
                }
                yo->midiendo_ = false;
                yo->avisandoTamanos_ = true;
                emit yo->cambio();
                yo->avisandoTamanos_ = false;
                if (yo->otraVez_) {
                    yo->otraVez_ = false;
                    yo->medir();
                }
            },
            Qt::QueuedConnection);
    });
}

fs::path Configuraciones::datos() const { return ba::platform::dataRoot(); }
fs::path Configuraciones::carpeta(const std::string& nombre) const { return datos() / "Configuraciones" / deU8(nombre); }

std::string Configuraciones::nombreValido(const std::string& n) {
    if (n.empty() || n.size() > 60) return "el nombre tiene que tener entre 1 y 60 letras";
    if (n[0] == '.' || n.find_first_of("/\\:*?\"<>|") != std::string::npos || n.find("..") != std::string::npos)
        return "el nombre no puede tener / \\ : * ? \" < > | ni empezar con punto";
    return {};
}

std::string Configuraciones::activa() const { return ba::platform::configActiva(datos()); }

json Configuraciones::listar() const {
    json out = json::array();
    std::error_code ec;
    const std::string act = activa();
    for (const auto& e : fs::directory_iterator(datos() / "Configuraciones", ec)) {
        const std::string n = aU8(e.path().filename());
        if (!e.is_directory() || n.empty() || n[0] == '.') continue;
        const json meta = leerJson(e.path() / "configuracion.json");
        out.push_back({{"nombre", n},
                       {"activa", n == act},
                       {"descripcion", meta.value("descripcion", std::string())},
                       {"creada_ms", meta.value("creada_ms", 0LL)},
                       {"desde", meta.value("desde", std::string())},
                       {"plantilla", meta.value("plantilla", false)}});
        std::lock_guard<std::mutex> l(tamanosM_);
        if (const auto it = tamanos_.find(n); it != tamanos_.end()) {
            out.back()["bytes"] = it->second.total;
            out.back()["bytes_grabaciones"] = it->second.grabaciones;
        } else
            out.back()["midiendo"] = true; // todavía no se sabe cuánto ocupa
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a["nombre"] < b["nombre"]; });
    return out;
}

json Configuraciones::demos() const {
    json out = json::array();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(ba::platform::bundleRoot() / "Demos", ec)) {
        if (!e.is_directory()) continue;
        const json meta = leerJson(e.path() / "configuracion.json");
        out.push_back({{"nombre", aU8(e.path().filename())}, {"descripcion", meta.value("descripcion", std::string())}});
    }
    return out;
}

std::string Configuraciones::crear(const std::string& nombre, const std::string& desde, const std::string& descripcion,
                                   bool conGrabaciones) {
    const std::string e = crearSinAvisar(nombre, desde, descripcion, conGrabaciones);
    if (e.empty()) emit cambio();
    return e;
}

std::string Configuraciones::crearSinAvisar(const std::string& nombre, const std::string& desde,
                                            const std::string& descripcion, bool conGrabaciones) {
    if (const std::string e = nombreValido(nombre); !e.empty()) return e;
    const fs::path dst = carpeta(nombre);
    std::error_code ec;
    if (fs::exists(dst, ec)) return "ya hay una configuración «" + nombre + "»";
    std::string err;
    std::string desc = descripcion;
    if (desde.empty() || desde == "vacia") {
        fs::create_directories(dst / "Config", ec);
        if (ec) return "no se pudo crear " + aU8(dst) + ": " + ec.message();
    } else if (desde.rfind("demo:", 0) == 0) {
        const fs::path de = ba::platform::bundleRoot() / "Demos" / deU8(desde.substr(5));
        if (!fs::is_directory(de, ec)) return "no hay ninguna demo «" + desde.substr(5) + "»";
        err = copiar(de, dst, true);
        if (desc.empty()) desc = leerJson(de / "configuracion.json").value("descripcion", std::string());
    } else if (desde.rfind("evento:", 0) == 0) {
        // evento:<carpeta del evento>/<inicio|cierre|ultima>: su foto de Config/ + lo demás de la actual.
        const std::string resto = desde.substr(7);
        const auto barra = resto.rfind('/');
        if (barra == std::string::npos) return "evento:<carpeta>/<inicio|cierre|ultima>";
        const fs::path eventos = ba::platform::configRoot() / "Eventos";
        const json foto = leerJson(eventos / deU8(resto.substr(0, barra)) / "config" / (resto.substr(barra + 1) + ".json"));
        if (!foto.contains("archivos")) return "no encuentro esa foto de la configuración del evento";
        err = copiar(ba::platform::configRoot(), dst, false);
        if (err.empty()) {
            fs::remove_all(dst / "Config", ec);
            evento::FotosConfig fc(ba::platform::configRoot() / "Config", eventos / ".objetos");
            for (const auto& [rel, info] : foto["archivos"].items()) {
                const json contenido = fc.leer(info.value("sha", std::string()));
                if (contenido.is_null()) continue;
                // Lo tapado (secretos) toma el valor de la configuración de ahora.
                const json actual = leerJson(ba::platform::configRoot() / "Config" / deU8(rel));
                escribirJson(dst / "Config" / deU8(rel),
                             evento::FotosConfig::conTapadosDe(json{{"x", contenido}}, json{{"x", actual}})["x"]);
            }
        }
    } else {
        // "actual" u otra configuración por su nombre.
        const fs::path de = desde == "actual" ? ba::platform::configRoot() : carpeta(desde);
        if (!fs::is_directory(de, ec)) return "no hay ninguna configuración «" + desde + "»";
        err = copiar(de, dst, conGrabaciones);
    }
    if (!err.empty()) {
        fs::remove_all(dst, ec);
        return err;
    }
    sembrar(dst);
    escribirJson(dst / "configuracion.json", {{"descripcion", desc},
                                              {"creada_ms", QDateTime::currentMSecsSinceEpoch()},
                                              {"desde", desde.empty() ? std::string("vacia") : desde}});
    return {};
}

std::string Configuraciones::nuevoEvento(const std::string& nombre, const std::string& desde,
                                         const std::string& descripcion) {
    const std::string e = crearEvento(nombre, desde, descripcion, nullptr);
    if (e.empty()) emit cambio();
    return e;
}

std::string Configuraciones::origenDe(const std::string& desde, std::filesystem::path* de) const {
    std::error_code ec;
    const std::string origen = desde.empty() ? std::string("cero") : desde;
    if (origen == "cero" || origen.rfind("evento:", 0) == 0) return {};
    if (origen.rfind("plantilla:", 0) == 0 || origen.rfind("demo:", 0) == 0) {
        const std::string n = origen.substr(origen.find(':') + 1);
        const fs::path demo = ba::platform::bundleRoot() / "Demos" / deU8(n);
        // La copia propia marcada como plantilla (una del programa editada) le gana a la original.
        const bool propia = fs::is_directory(carpeta(n), ec) && leerJson(carpeta(n) / "configuracion.json").value("plantilla", false);
        *de = propia ? carpeta(n) : fs::is_directory(demo, ec) ? demo : carpeta(n);
        return fs::is_directory(*de, ec) ? std::string() : "no hay ninguna plantilla «" + n + "»";
    }
    *de = origen == "actual" ? ba::platform::configRoot() : carpeta(origen);
    return fs::is_directory(*de, ec) ? std::string() : "no hay ningún evento «" + origen + "»";
}

std::string Configuraciones::crearEvento(const std::string& nombre, const std::string& desde,
                                         const std::string& descripcion, std::atomic<std::uintmax_t>* hechos) {
    if (const std::string e = nombreValido(nombre); !e.empty()) return e;
    const fs::path dst = carpeta(nombre);
    std::error_code ec;
    if (fs::exists(dst, ec)) return "ya hay un evento «" + nombre + "»";
    std::string err;
    const std::string origen = desde.empty() ? std::string("cero") : desde;
    fs::path de;
    if (origen == "cero") {
        fs::create_directories(dst / "Config", ec);
        if (ec) return "no se pudo crear " + aU8(dst) + ": " + ec.message();
    } else if (origen.rfind("evento:", 0) == 0) {
        // Cómo estaba un evento cerrado: la foto de su Config/ (no es una copia del evento).
        err = crearSinAvisar(nombre, origen, descripcion);
        if (err.empty()) {
            for (const char* d : {"Eventos", "Recordings", "Exportado"}) fs::remove_all(dst / d, ec);
            for (const char* f : {"orden-del-dia.json", "quorum.json", "votaciones-oficiales.json", "al-aire.json"})
                fs::remove(dst / "Config" / f, ec);
        }
    } else {
        // Copiando el actual, otro evento o una plantilla: COPIA TOTAL.
        if (const std::string e = origenDe(origen, &de); !e.empty()) return e;
        err = copiarTodo(de, dst, hechos);
    }
    if (!err.empty()) {
        fs::remove_all(dst, ec);
        return err;
    }
    sembrar(dst);
    // Su evento en curso: el de la copia (su guion, su web, su padrón…); si no hay uno en
    // curso, el que trae (una plantilla trae su guion sin marcarlo en curso: antes se
    // creaba uno vacío y el guion de la plantilla no llegaba); si no trae ninguno (de
    // cero, de una foto), uno nuevo con su nombre.
    evento::EventoStore store(dst / "Eventos");
    std::string ruta = store.enCurso();
    if (ruta.empty()) {
        ruta = store.ultimoTocado();
        std::string e2;
        if (!ruta.empty()) store.iniciar(ruta, QDateTime::currentMSecsSinceEpoch(), &e2);
    }
    if (ruta.empty()) {
        std::string e2;
        ruta = store.crear(nombre, &e2);
        if (ruta.empty()) {
            fs::remove_all(dst, ec);
            return "no se pudo crear su guion: " + e2;
        }
        store.iniciar(ruta, QDateTime::currentMSecsSinceEpoch(), &e2);
    }
    escribirJson(dst / "configuracion.json", {{"descripcion", descripcion},
                                              {"creada_ms", QDateTime::currentMSecsSinceEpoch()},
                                              {"desde", origen},
                                              {"evento", ruta}});
    return {};
}

QString Configuraciones::nuevoEventoAparte(const QString& nombre, const QString& desde, bool abrir,
                                           std::function<void(QString)> listo) {
    const std::string n = nombre.toStdString(), d = desde.toStdString();
    if (!copia_.isEmpty()) return "ya se está copiando «" + copia_.value("nombre").toString() + "»";
    if (const std::string e = nombreValido(n); !e.empty()) return QString::fromStdString(e);
    std::error_code ec;
    if (fs::exists(carpeta(n), ec)) return "ya hay un evento «" + nombre + "»";
    fs::path de;
    if (const std::string e = origenDe(d, &de); !e.empty()) return QString::fromStdString(e);
    // Lo que va a copiar (para el progreso): lo medido, o se mide ahora.
    std::uintmax_t total = 0;
    if (!de.empty()) {
        std::lock_guard<std::mutex> l(tamanosM_);
        const auto it = tamanos_.find(aU8(de.filename()));
        if (it != tamanos_.end()) total = it->second.total;
    }
    copia_ = QVariantMap{{"nombre", nombre}, {"hechos", 0.0}, {"total", static_cast<double>(total)}};
    emit copiaCambio();
    progresoCopia_.start();
    QPointer<Configuraciones> yo(this);
    auto hechos = std::make_shared<std::atomic<std::uintmax_t>>(0);
    copiaContador_ = hechos;
    QThreadPool::globalInstance()->start([yo, n, d, abrir, hechos, listo = std::move(listo), total, de]() mutable {
        std::uintmax_t t = total;
        if (t == 0 && !de.empty()) t = bytesDe(de);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [yo, t]() { if (yo && !yo->copia_.isEmpty()) yo->copia_["total"] = static_cast<double>(t); },
            Qt::QueuedConnection);
        const std::string e = yo ? yo->crearEvento(n, d, {}, hechos.get()) : std::string("se cerró la app");
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, n, e, abrir, listo = std::move(listo)]() {
                if (!yo) return;
                yo->progresoCopia_.stop();
                yo->copia_.clear();
                yo->copiaContador_.reset();
                emit yo->copiaCambio();
                QString res = QString::fromStdString(e);
                if (e.empty()) {
                    emit yo->cambio();
                    if (abrir) {
                        if (const std::string e2 = yo->activar(n); !e2.empty()) res = QString::fromStdString(e2);
                    }
                }
                emit yo->nuevoEventoListo(QString::fromStdString(n), res, abrir && res.isEmpty());
                if (listo) listo(res);
                if (abrir && res.isEmpty()) yo->reiniciar();
            },
            Qt::QueuedConnection);
    });
    return {};
}

json Configuraciones::separarEventos(const fs::path& datos) {
    json hecho = json::array();
    json marcas = leerJson(datos / "migraciones.json");
    if (marcas.value("un-evento-por-carpeta", false)) return hecho;
    std::error_code ec;
    const fs::path raiz = datos / "Configuraciones";
    const fs::path respaldo = datos / "Respaldos" / ("eventos-antes-de-separar-" + std::to_string(QDateTime::currentSecsSinceEpoch()));
    std::vector<fs::path> carpetas;
    for (const auto& e : fs::directory_iterator(raiz, ec))
        if (e.is_directory() && !aU8(e.path().filename()).empty() && aU8(e.path().filename())[0] != '.')
            carpetas.push_back(e.path());
    for (const fs::path& c : carpetas) {
        evento::EventoStore st(c / "Eventos");
        const std::string enCurso = st.enCurso();
        // Los eventos «propios» (no las jornadas que se crean solas).
        std::vector<evento::Ficha> propios;
        for (const auto& f : st.listar())
            if (!st.datos(f.carpeta).automatico) propios.push_back(f);
        if (propios.size() <= 1) continue;
        // Se queda el en curso; si el en curso es una jornada (o ninguno), el último tocado.
        std::string queda = enCurso;
        if (std::none_of(propios.begin(), propios.end(), [&](const evento::Ficha& f) { return f.carpeta == queda; }))
            queda = st.ultimoTocado();
        if (std::none_of(propios.begin(), propios.end(), [&](const evento::Ficha& f) { return f.carpeta == queda; }))
            queda = propios.front().carpeta;
        fs::create_directories(respaldo / c.filename(), ec);
        fs::copy(c / "Eventos", respaldo / c.filename() / "Eventos", fs::copy_options::recursive, ec);
        for (const auto& f : propios) {
            if (f.carpeta == queda) continue;
            // El nombre de la carpeta nueva: el del evento (válido y sin pisar otra).
            std::string base = f.nombre.empty() ? aU8(fs::path(deU8(f.carpeta)).filename()) : f.nombre;
            for (char& ch : base)
                if (std::string("/\\:*?\"<>|").find(ch) != std::string::npos) ch = '-';
            while (!base.empty() && base[0] == '.') base.erase(0, 1);
            if (base.size() > 50) base.resize(50);
            if (base.empty()) base = "Evento";
            std::string nombre = base;
            for (int i = 2; fs::exists(raiz / deU8(nombre), ec); ++i) nombre = base + " (" + std::to_string(i) + ")";
            const fs::path dst = raiz / deU8(nombre);
            if (const std::string e = copiar(c, dst, false, true); !e.empty()) {
                fs::remove_all(dst, ec);
                hecho.push_back({{"evento", f.nombre}, {"de", aU8(c.filename())}, {"error", e}});
                continue;
            }
            const fs::path hoja = fs::path(deU8(f.carpeta)).filename();
            fs::create_directories(dst / "Eventos", ec);
            fs::rename(c / "Eventos" / deU8(f.carpeta), dst / "Eventos" / hoja, ec);
            if (ec) { // otro disco: copiar y borrar
                ec.clear();
                fs::copy(c / "Eventos" / deU8(f.carpeta), dst / "Eventos" / hoja, fs::copy_options::recursive, ec);
                if (!ec) fs::remove_all(c / "Eventos" / deU8(f.carpeta), ec);
            }
            // Las fotos de Config/ de sus capítulos (por contenido).
            fs::copy(c / "Eventos" / ".objetos", dst / "Eventos" / ".objetos",
                     fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
            evento::EventoStore nuevo(dst / "Eventos");
            const std::string h = aU8(hoja);
            if (nuevo.datos(h).estado != "terminado") nuevo.iniciar(h, QDateTime::currentMSecsSinceEpoch());
            escribirJson(dst / "configuracion.json", {{"descripcion", "Separado de «" + aU8(c.filename()) + "»"},
                                                      {"creada_ms", QDateTime::currentMSecsSinceEpoch()},
                                                      {"desde", aU8(c.filename())},
                                                      {"evento", h}});
            hecho.push_back({{"evento", f.nombre}, {"de", aU8(c.filename())}, {"a", nombre}});
        }
    }
    marcas["un-evento-por-carpeta"] = true;
    escribirJson(datos / "migraciones.json", marcas);
    return hecho;
}

std::string Configuraciones::marcarPlantilla(const std::string& nombre, bool plantilla) {
    std::error_code ec;
    if (!fs::is_directory(carpeta(nombre), ec)) return "no hay ningún evento «" + nombre + "»";
    json meta = leerJson(carpeta(nombre) / "configuracion.json");
    meta["plantilla"] = plantilla;
    escribirJson(carpeta(nombre) / "configuracion.json", meta);
    emit cambio();
    return {};
}

json Configuraciones::plantillasAdmin() const {
    const json ajustes = leerJson(datos() / "plantillas.json");
    const json orden = ajustes.value("orden", json::array()), ocultas = ajustes.value("ocultas", json::array());
    const json nombres = ajustes.value("nombres", json::object()), descs = ajustes.value("descripciones", json::object());
    const auto en = [](const json& arr, const std::string& v) { return std::find(arr.begin(), arr.end(), v) != arr.end(); };
    std::map<std::string, json> t; // por carpeta
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(ba::platform::bundleRoot() / "Demos", ec)) {
        if (!e.is_directory()) continue;
        const json meta = leerJson(e.path() / "configuracion.json");
        const std::string id = aU8(e.path().filename());
        t[id] = {{"id", id}, {"descripcion", meta.value("descripcion", std::string())},
                 {"datos", meta.value("plantillaDatos", json::object())}, {"programa", true}, {"propia", false}};
    }
    const std::string act = activa();
    for (const auto& c : listar()) {
        if (!c.value("plantilla", false)) continue;
        const std::string id = c.value("nombre", std::string());
        const json meta = leerJson(carpeta(id) / "configuracion.json");
        json& x = t[id];
        if (!x.is_object()) x = {{"id", id}, {"programa", false}, {"datos", json::object()}};
        x["propia"] = true;
        if (!meta.value("descripcion", std::string()).empty()) x["descripcion"] = meta.value("descripcion", std::string());
        if (meta.contains("plantillaDatos") && meta["plantillaDatos"].is_object()) x["datos"] = meta["plantillaDatos"];
    }
    std::vector<json> l;
    for (auto& [id, x] : t) {
        const json d = x.value("datos", json::object());
        json o{{"id", id},
               {"carpeta", id},
               {"nombre", nombres.value(id, id)},
               {"descripcion", descs.value(id, x.value("descripcion", std::string()))},
               {"escenario", d.value("escenario", std::string())},
               {"icono", d.value("icono", std::string("▣"))},
               {"visible", !en(ocultas, id)},
               {"programa", x.value("programa", false)},
               {"propia", x.value("propia", false)},
               {"editada", x.value("programa", false) && x.value("propia", false)},
               {"activa", id == act}};
        // Lo ordenado a mano primero (en ese orden); después el orden de fábrica.
        const auto it = std::find(orden.begin(), orden.end(), id);
        o["_pos"] = it != orden.end() ? static_cast<int>(it - orden.begin()) : 1000 + d.value("orden", 500);
        l.push_back(o);
    }
    std::stable_sort(l.begin(), l.end(), [](const json& a, const json& b) { return a["_pos"].get<int>() < b["_pos"].get<int>(); });
    json out = json::array();
    for (auto& o : l) {
        o.erase("_pos");
        out.push_back(o);
    }
    return out;
}

json Configuraciones::plantillas() const {
    json out = json::array();
    for (const auto& p : plantillasAdmin())
        if (p.value("visible", true))
            out.push_back({{"nombre", p["nombre"]}, {"carpeta", p["carpeta"]}, {"descripcion", p["descripcion"]},
                           {"demo", p["programa"]}, {"escenario", p["escenario"]}, {"icono", p["icono"]}});
    return out;
}

std::string Configuraciones::plantillaAjustar(const std::string& id, const json& cambios) {
    bool hay = false;
    for (const auto& p : plantillasAdmin()) hay = hay || p.value("id", std::string()) == id;
    if (!hay) return "no hay una plantilla «" + id + "»";
    json a = leerJson(datos() / "plantillas.json");
    if (!a.is_object()) a = json::object();
    if (cambios.contains("nombre") && cambios["nombre"].is_string()) {
        const std::string n = cambios["nombre"];
        if (n.empty() || n == id) a["nombres"].erase(id);
        else a["nombres"][id] = n;
    }
    if (cambios.contains("descripcion") && cambios["descripcion"].is_string()) a["descripciones"][id] = cambios["descripcion"];
    if (cambios.contains("visible") && cambios["visible"].is_boolean()) {
        json oc = a.value("ocultas", json::array());
        oc.erase(std::remove(oc.begin(), oc.end(), id), oc.end());
        if (!cambios["visible"].get<bool>()) oc.push_back(id);
        a["ocultas"] = oc;
    }
    escribirJson(datos() / "plantillas.json", a);
    emit cambio();
    return {};
}

std::string Configuraciones::plantillaMover(const std::string& id, int delta) {
    std::vector<std::string> ids;
    for (const auto& p : plantillasAdmin()) ids.push_back(p.value("id", std::string()));
    const auto it = std::find(ids.begin(), ids.end(), id);
    if (it == ids.end()) return "no hay una plantilla «" + id + "»";
    const int i = static_cast<int>(it - ids.begin()), j = std::clamp(i + delta, 0, static_cast<int>(ids.size()) - 1);
    if (i == j) return {};
    std::swap(ids[static_cast<std::size_t>(i)], ids[static_cast<std::size_t>(j)]);
    json a = leerJson(datos() / "plantillas.json");
    if (!a.is_object()) a = json::object();
    a["orden"] = ids;
    escribirJson(datos() / "plantillas.json", a);
    emit cambio();
    return {};
}

std::string Configuraciones::plantillaParaEditar(const std::string& id) {
    std::error_code ec;
    if (!fs::is_directory(carpeta(id), ec)) {
        // Del programa: una copia propia, marcada como plantilla con sus datos (le gana a la original).
        const fs::path demo = ba::platform::bundleRoot() / "Demos" / deU8(id);
        if (!fs::is_directory(demo, ec)) return "no hay una plantilla «" + id + "»";
        if (const std::string e = crearSinAvisar(id, "demo:" + id, "", false); !e.empty()) return e;
        const json original = leerJson(demo / "configuracion.json");
        json meta = leerJson(carpeta(id) / "configuracion.json");
        meta["plantilla"] = true;
        if (original.contains("plantillaDatos")) meta["plantillaDatos"] = original["plantillaDatos"];
        if (meta.value("descripcion", std::string()).empty()) meta["descripcion"] = original.value("descripcion", std::string());
        escribirJson(carpeta(id) / "configuracion.json", meta);
    }
    if (id != activa())
        if (const std::string e = activar(id); !e.empty()) return e;
    emit cambio();
    return {};
}

std::string Configuraciones::plantillaRestablecer(const std::string& id) {
    for (const auto& p : plantillasAdmin())
        if (p.value("id", std::string()) == id) {
            if (!p.value("editada", false)) return "«" + id + "» no tiene cambios propios para sacar";
            if (p.value("activa", false)) return "está abierta: pasá a otro evento antes de restablecerla";
            return borrar(id);
        }
    return "no hay una plantilla «" + id + "»";
}

std::string Configuraciones::plantillaDesdeActual(const std::string& nombre, const std::string& escenario) {
    if (const std::string e = crearSinAvisar(nombre, "actual", "", false); !e.empty()) return e;
    json meta = leerJson(carpeta(nombre) / "configuracion.json");
    meta["plantilla"] = true;
    if (!escenario.empty()) meta["plantillaDatos"] = {{"escenario", escenario}};
    escribirJson(carpeta(nombre) / "configuracion.json", meta);
    emit cambio();
    return {};
}

std::string Configuraciones::activar(const std::string& nombre) {
    std::error_code ec;
    if (!fs::is_directory(carpeta(nombre), ec)) return "no hay ninguna configuración «" + nombre + "»";
    escribirJson(datos() / "configuraciones.json", {{"activa", nombre}});
    emit cambio();
    return {};
}

std::string Configuraciones::renombrar(const std::string& de, const std::string& a) {
    if (const std::string e = nombreValido(a); !e.empty()) return e;
    if (de == activa()) return "la activa no se renombra (cambiá a otra primero)";
    std::error_code ec;
    if (!fs::is_directory(carpeta(de), ec)) return "no hay ninguna configuración «" + de + "»";
    if (fs::exists(carpeta(a), ec)) return "ya hay una configuración «" + a + "»";
    fs::rename(carpeta(de), carpeta(a), ec);
    if (ec) return "no se pudo renombrar: " + ec.message();
    emit cambio();
    return {};
}

std::string Configuraciones::describir(const std::string& nombre, const std::string& descripcion) {
    std::error_code ec;
    if (!fs::is_directory(carpeta(nombre), ec)) return "no hay ninguna configuración «" + nombre + "»";
    json meta = leerJson(carpeta(nombre) / "configuracion.json");
    meta["descripcion"] = descripcion;
    escribirJson(carpeta(nombre) / "configuracion.json", meta);
    emit cambio();
    return {};
}

std::string Configuraciones::borrar(const std::string& nombre) {
    if (nombre == activa()) return "la activa no se borra (cambiá a otra primero)";
    std::error_code ec;
    if (nombreValido(nombre) != "" || !fs::is_directory(carpeta(nombre), ec)) return "no hay ninguna configuración «" + nombre + "»";
    const fs::path pap = datos() / "Configuraciones" / ".papelera" /
                         deU8(nombre + "-" + std::to_string(QDateTime::currentSecsSinceEpoch()));
    fs::create_directories(pap.parent_path(), ec);
    fs::rename(carpeta(nombre), pap, ec);
    if (ec) return "no se pudo mover a la papelera: " + ec.message();
    emit cambio();
    return {};
}

namespace {
QVariantList aQvl(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}
} // namespace

QVariantList Configuraciones::listaUi() const { return aQvl(listar()); }
QVariantList Configuraciones::demosUi() const { return aQvl(demos()); }
QString Configuraciones::crearUi(const QString& n, const QString& d, const QString& desc) {
    return QString::fromStdString(crear(n.toStdString(), d.toStdString(), desc.toStdString()));
}
QString Configuraciones::nuevoEventoUi(const QString& n, const QString& desde, bool abrir) {
    // Una copia puede ser de varios GB (grabaciones): en otro hilo, con progreso (`copia`).
    if (n.toStdString() == activa() && abrir) return "ya es el evento abierto";
    return nuevoEventoAparte(n, desde, abrir, {});
}
QString Configuraciones::marcarPlantillaUi(const QString& n, bool p) {
    return QString::fromStdString(marcarPlantilla(n.toStdString(), p));
}
QVariantList Configuraciones::plantillasUi() const { return aQvl(plantillas()); }
QVariantList Configuraciones::plantillasAdminUi() const { return aQvl(plantillasAdmin()); }
QString Configuraciones::plantillaAjustarUi(const QString& id, const QVariantMap& c) {
    return QString::fromStdString(plantillaAjustar(id.toStdString(), json::parse(QJsonDocument::fromVariant(c).toJson().toStdString(), nullptr, false)));
}
QString Configuraciones::plantillaMoverUi(const QString& id, int d) { return QString::fromStdString(plantillaMover(id.toStdString(), d)); }
QString Configuraciones::plantillaEditarUi(const QString& id) {
    if (const std::string e = plantillaParaEditar(id.toStdString()); !e.empty()) return QString::fromStdString(e);
    return reiniciar();
}
QString Configuraciones::plantillaRestablecerUi(const QString& id) { return QString::fromStdString(plantillaRestablecer(id.toStdString())); }
QString Configuraciones::plantillaBorrarUi(const QString& id) {
    for (const auto& p : plantillasAdmin())
        if (p.value("id", std::string()) == id.toStdString()) {
            if (p.value("programa", false)) return "las del programa no se borran: ocultala (o restablecela si la editaste)";
            if (p.value("activa", false)) return "está abierta: pasá a otro evento antes de borrarla";
            return QString::fromStdString(borrar(id.toStdString()));
        }
    return "no hay una plantilla «" + id + "»";
}
QString Configuraciones::plantillaDesdeActualUi(const QString& n, const QString& esc) {
    return QString::fromStdString(plantillaDesdeActual(n.trimmed().toStdString(), esc.toStdString()));
}
QString Configuraciones::cambiarUi(const QString& n) {
    if (n.toStdString() == activa()) return "ya es la activa";
    if (const std::string e = activar(n.toStdString()); !e.empty()) return QString::fromStdString(e);
    return reiniciar();
}
QString Configuraciones::renombrarUi(const QString& de, const QString& a) {
    return QString::fromStdString(renombrar(de.toStdString(), a.toStdString()));
}
QString Configuraciones::describirUi(const QString& n, const QString& d) {
    return QString::fromStdString(describir(n.toStdString(), d.toStdString()));
}
QString Configuraciones::borrarUi(const QString& n) { return QString::fromStdString(borrar(n.toStdString())); }

} // namespace ba::app
