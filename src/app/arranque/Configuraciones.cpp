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
} // namespace

Configuraciones::Configuraciones(std::function<QString()> reiniciar, QObject* parent)
    : QObject(parent), reiniciar_(std::move(reiniciar)) {
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
    emit cambio();
    return {};
}

std::string Configuraciones::nuevoEvento(const std::string& nombre, const std::string& desde,
                                         const std::string& descripcion) {
    if (const std::string e = nombreValido(nombre); !e.empty()) return e;
    const fs::path dst = carpeta(nombre);
    std::error_code ec;
    if (fs::exists(dst, ec)) return "ya hay un evento «" + nombre + "»";
    std::string err;
    std::string origen = desde.empty() ? std::string("cero") : desde;
    if (origen == "cero") {
        fs::create_directories(dst / "Config", ec);
        if (ec) return "no se pudo crear " + aU8(dst) + ": " + ec.message();
    } else if (origen.rfind("plantilla:", 0) == 0 || origen.rfind("demo:", 0) == 0) {
        const std::string n = origen.substr(origen.find(':') + 1);
        const fs::path demo = ba::platform::bundleRoot() / "Demos" / deU8(n);
        const fs::path de = fs::is_directory(demo, ec) ? demo : carpeta(n);
        if (!fs::is_directory(de, ec)) return "no hay ninguna plantilla «" + n + "»";
        err = copiar(de, dst, false, true);
    } else if (origen.rfind("evento:", 0) == 0) {
        err = crear(nombre, origen, descripcion); // la foto de Config/ de un evento cerrado
        if (err.empty()) {
            for (const char* d : {"Eventos", "Recordings", "Exportado"}) fs::remove_all(dst / d, ec);
            for (const char* f : {"orden-del-dia.json", "quorum.json", "votaciones-oficiales.json", "al-aire.json"})
                fs::remove(dst / "Config" / f, ec);
        }
    } else {
        const fs::path de = origen == "actual" ? ba::platform::configRoot() : carpeta(origen);
        if (!fs::is_directory(de, ec)) return "no hay ningún evento «" + origen + "»";
        err = copiar(de, dst, false, true);
    }
    if (!err.empty()) {
        fs::remove_all(dst, ec);
        return err;
    }
    sembrar(dst);
    // Su evento: el guion, el padrón, la web… en curso desde ya.
    evento::EventoStore store(dst / "Eventos");
    std::string e2;
    const std::string ruta = store.crear(nombre, &e2);
    if (ruta.empty()) {
        fs::remove_all(dst, ec);
        return "no se pudo crear su guion: " + e2;
    }
    store.iniciar(ruta, QDateTime::currentMSecsSinceEpoch(), &e2);
    escribirJson(dst / "configuracion.json", {{"descripcion", descripcion},
                                              {"creada_ms", QDateTime::currentMSecsSinceEpoch()},
                                              {"desde", origen},
                                              {"evento", ruta}});
    emit cambio();
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

json Configuraciones::plantillas() const {
    json out = json::array();
    for (const auto& d : demos())
        out.push_back({{"nombre", d.value("nombre", std::string())},
                       {"descripcion", d.value("descripcion", std::string())},
                       {"demo", true}});
    for (const auto& c : listar())
        if (c.value("plantilla", false))
            out.push_back({{"nombre", c.value("nombre", std::string())},
                           {"descripcion", c.value("descripcion", std::string())},
                           {"demo", false}});
    return out;
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
    if (const std::string e = nuevoEvento(n.toStdString(), desde.toStdString()); !e.empty())
        return QString::fromStdString(e);
    return abrir ? cambiarUi(n) : QString();
}
QString Configuraciones::marcarPlantillaUi(const QString& n, bool p) {
    return QString::fromStdString(marcarPlantilla(n.toStdString(), p));
}
QVariantList Configuraciones::plantillasUi() const { return aQvl(plantillas()); }
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
