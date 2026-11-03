// ============================================================================
// src/app/pantallas/Distribuciones.cpp — ver Distribuciones.hpp
// ============================================================================
#include "pantallas/Distribuciones.hpp"

#include "arranque/ArchivoConfigQt.hpp"
#include "pantallas/Catalogo.hpp"
#include "pantallas/Monitores.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace ba::app {
namespace {

/// Como mucho tantas cosas en una ventana (lo mismo que VentanasModel).
constexpr int kMaxCosas = 9;
/// Como mucho tantas ventanas en una distribución: más ya no es una distribución.
constexpr int kMaxVentanas = 32;

bool disposicionValida(const QString& d) { return d == "grid" || d == "focus" || d == "pip" || d == "row"; }

double recortar(double v, double lo, double hi) { return std::clamp(v, lo, hi); }

QRectF rectDe(const QVariantMap& m) {
    return QRectF(m.value("x").toDouble(), m.value("y").toDouble(), m.value("ancho").toDouble(),
                  m.value("alto").toDouble());
}

QVariantMap mapaDeRect(const QRectF& r) {
    return {{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
}

} // namespace

// --- Cuentas ------------------------------------------------------------------------

namespace distribucion {

QRect aAbsoluta(const QRectF& r, const QRect& m) {
    const int x = m.x() + static_cast<int>(std::lround(r.x() * m.width()));
    const int y = m.y() + static_cast<int>(std::lround(r.y() * m.height()));
    const int w = std::max(1, static_cast<int>(std::lround(r.width() * m.width())));
    const int h = std::max(1, static_cast<int>(std::lround(r.height() * m.height())));
    return QRect(x, y, w, h);
}

QRectF aRelativa(const QRect& g, const QRect& m) {
    if (m.width() <= 0 || m.height() <= 0) return QRectF(0, 0, 1, 1);
    const double x = recortar((g.x() - m.x()) / static_cast<double>(m.width()), 0.0, 1.0);
    const double y = recortar((g.y() - m.y()) / static_cast<double>(m.height()), 0.0, 1.0);
    double w = g.width() / static_cast<double>(m.width());
    double h = g.height() / static_cast<double>(m.height());
    w = recortar(w, 0.0, 1.0 - x);
    h = recortar(h, 0.0, 1.0 - y);
    // Una ventana que caía casi entera afuera: que por lo menos se vea.
    if (w <= 0.0) w = std::min(0.25, 1.0);
    if (h <= 0.0) h = std::min(0.25, 1.0);
    return QRectF(std::min(x, 1.0 - w), std::min(y, 1.0 - h), w, h);
}

QRect geometriaEn(const VentanaDistribucion& v, const QRect& m) {
    const QRect& a = v.absoluta;
    if (a.width() > 0 && a.height() > 0 && a.width() <= m.width() && a.height() <= m.height()) {
        QRect g = a.translated(m.topLeft());
        if (g.right() > m.right()) g.moveRight(m.right());
        if (g.bottom() > m.bottom()) g.moveBottom(m.bottom());
        if (g.left() < m.left()) g.moveLeft(m.left());
        if (g.top() < m.top()) g.moveTop(m.top());
        return g;
    }
    return aAbsoluta(v.relativa, m);
}

int monitorPara(const VentanaDistribucion& v, const std::vector<MonitorVisto>& hay, bool* falta) {
    if (falta != nullptr) *falta = false;
    if (hay.empty()) {
        if (falta != nullptr) *falta = !v.monitor.isEmpty();
        return -1;
    }
    for (std::size_t i = 0; i < hay.size(); ++i)
        if (!v.monitor.isEmpty() && hay[i].id == v.monitor) return static_cast<int>(i);
    if (falta != nullptr) *falta = !v.monitor.isEmpty();
    for (std::size_t i = 0; i < hay.size(); ++i)
        if (!hay[i].principal) return static_cast<int>(i);
    return 0;
}

QString ventanaDeMapa(const QVariantMap& m, VentanaDistribucion& v,
                      const std::function<QString(const QString&)>& resolverMonitor) {
    if (m.contains("nombre")) v.nombre = m.value("nombre").toString().trimmed();
    if (m.contains("cosas")) {
        v.cosas.clear();
        const QVariant c = m.value("cosas");
        if (c.typeId() == QMetaType::QString) v.cosas << c.toString();
        else
            for (const QVariant& x : c.toList()) v.cosas << x.toString();
    } else if (m.contains("cosa")) {
        v.cosas = QStringList{m.value("cosa").toString()};
    }
    for (const char* k : {"disposicion", "distribucion", "layout"})
        if (m.contains(k)) {
            const QString d = m.value(k).toString();
            if (!disposicionValida(d))
                return QCoreApplication::translate("pantallas", "«%1» no es una disposición: grid, focus, pip o row.").arg(d);
            v.disposicion = d;
            break;
        }
    if (m.contains("monitor")) {
        const QString quien = m.value("monitor").toString().trimmed();
        if (quien.isEmpty()) {
            v.monitor.clear();
        } else {
            const QString id = resolverMonitor ? resolverMonitor(quien) : quien;
            if (id.isEmpty())
                return QCoreApplication::translate("pantallas", "No conozco el monitor «%1»: decí su id, su número de orden o su nombre (monitor_listar).")
                    .arg(quien);
            v.monitor = id;
        }
    }
    // El lugar relativo: lo que venga, sobre lo que había.
    QRectF r = v.relativa;
    bool tocaLugar = false;
    if (m.contains("x")) { r.moveLeft(m.value("x").toDouble()); tocaLugar = true; }
    if (m.contains("y")) { r.moveTop(m.value("y").toDouble()); tocaLugar = true; }
    if (m.contains("ancho")) { r.setWidth(m.value("ancho").toDouble()); tocaLugar = true; }
    if (m.contains("alto")) { r.setHeight(m.value("alto").toDouble()); tocaLugar = true; }
    if (tocaLugar) {
        constexpr double eps = 1e-6;
        if (r.x() < -eps || r.y() < -eps || r.width() <= 0 || r.height() <= 0 || r.x() + r.width() > 1 + eps ||
            r.y() + r.height() > 1 + eps)
            return QCoreApplication::translate("pantallas", "El lugar va relativo al monitor, de 0 a 1: x e y desde la esquina de arriba a la izquierda, "
                       "ancho y alto mayores que 0, y x+ancho e y+alto hasta 1 (la mitad izquierda es x 0, y 0, ancho 0.5, alto 1).");
        v.relativa = QRectF(std::max(0.0, r.x()), std::max(0.0, r.y()), std::min(r.width(), 1.0 - std::max(0.0, r.x())),
                            std::min(r.height(), 1.0 - std::max(0.0, r.y())));
    }
    if (m.contains("absoluta")) {
        const QVariant a = m.value("absoluta");
        const QVariantMap am = a.toMap();
        if (!a.isValid() || a.isNull() || am.isEmpty()) {
            v.absoluta = QRect();
        } else {
            const QRect g(am.value("x").toInt(), am.value("y").toInt(), am.value("ancho").toInt(), am.value("alto").toInt());
            if (g.width() <= 0 || g.height() <= 0 || g.x() < 0 || g.y() < 0)
                return QCoreApplication::translate("pantallas", "`absoluta` va en píxeles desde la esquina del monitor: x e y desde 0, ancho y alto mayores que 0.");
            v.absoluta = g;
        }
    }
    if (m.contains("pantallaCompleta")) v.pantallaCompleta = m.value("pantallaCompleta").toBool();
    if (m.contains("encima")) v.encima = m.value("encima").toBool();
    return {};
}

QVariantMap mapaDeVentana(const VentanaDistribucion& v) {
    QVariantMap m{{"nombre", v.nombre},
                  {"cosas", v.cosas},
                  {"disposicion", v.disposicion},
                  {"monitor", v.monitor},
                  {"x", v.relativa.x()},
                  {"y", v.relativa.y()},
                  {"ancho", v.relativa.width()},
                  {"alto", v.relativa.height()},
                  {"pantallaCompleta", v.pantallaCompleta},
                  {"encima", v.encima}};
    if (v.absoluta.width() > 0 && v.absoluta.height() > 0)
        m["absoluta"] = QVariantMap{{"x", v.absoluta.x()},
                                    {"y", v.absoluta.y()},
                                    {"ancho", v.absoluta.width()},
                                    {"alto", v.absoluta.height()}};
    return m;
}

} // namespace distribucion

using namespace distribucion;

// --- El modelo ------------------------------------------------------------------------

DistribucionesModel::DistribucionesModel(std::filesystem::path archivo, CatalogoVisible* catalogo,
                                         MonitoresModel* monitores, VentanasModel* ventanas, QObject* parent)
    : ListaConClave<Distribucion>(parent), archivo_(std::move(archivo)), catalogo_(catalogo), monitores_(monitores),
      ventanas_(ventanas) {
    cargar();
    // Dónde iría cada ventana y los títulos dependen de los monitores y del catálogo:
    // la vista se entera por el modelo, no preguntando.
    const auto todoCambio = [this]() {
        if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {VentanasRole, ResumenRole});
        emit cambio();
    };
    if (monitores_ != nullptr)
        connect(monitores_, &MonitoresModel::cambio, this, [this, todoCambio]() {
            todoCambio();
            // Se enchufó o se desenchufó un monitor: lo aplicado se vuelve a ubicar.
            reaplicar();
        });
    if (catalogo_ != nullptr) connect(catalogo_, &CatalogoVisible::cambio, this, todoCambio);
}

QObject* DistribucionesModel::lienzosQml() const {
    // Es un miembro: EscritorioViewModel le pone CppOwnership (acá no se arrastra QtQml,
    // así el test del modelo no lo necesita).
    return const_cast<LienzosModel*>(&lienzos_);
}

// --- Los lienzos (ventanas madre) -----------------------------------------------------------

QVariant LienzosModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const Lienzo& l = filas_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case LienzoIdRole: return l.id;
        case GeometriaRole: return l.geometria;
        case PantallaCompletaRole: return l.pantallaCompleta;
        case EncimaRole: return l.encima;
        case OrdenRole: return l.orden;
        case PiezasRole: {
            QVariantList ps;
            for (const Pieza& p : l.piezas)
                ps << QVariantMap{{"nombre", p.nombre}, {"cosas", p.cosas}, {"disposicion", p.disposicion},
                                  {"x", p.rect.x()},    {"y", p.rect.y()},  {"ancho", p.rect.width()},
                                  {"alto", p.rect.height()}};
            return ps;
        }
        default: return {};
    }
}

QHash<int, QByteArray> LienzosModel::roleNames() const {
    return {{LienzoIdRole, "lienzoId"}, {GeometriaRole, "geometria"}, {PantallaCompletaRole, "pantallaCompleta"},
            {EncimaRole, "encima"},     {OrdenRole, "orden"},         {PiezasRole, "piezas"}};
}

QList<int> LienzosModel::rolesQueCambian(const Lienzo& a, const Lienzo& b) const {
    QList<int> r;
    if (a.geometria != b.geometria) r << GeometriaRole;
    if (a.pantallaCompleta != b.pantallaCompleta) r << PantallaCompletaRole;
    if (a.encima != b.encima) r << EncimaRole;
    if (a.orden != b.orden) r << OrdenRole;
    if (a.piezas != b.piezas) r << PiezasRole;
    return r;
}

bool LienzosModel::poner(std::vector<Lienzo> nuevos) {
    const bool hubo = aplicar(std::move(nuevos));
    if (hubo) emit cambio();
    return hubo;
}

int LienzosModel::cerrarTodo() {
    const int n = rowCount();
    if (n > 0) poner({});
    return n;
}

QVariant DistribucionesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const Distribucion& d = filas_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return d.id;
        case NombreRole: return d.nombre;
        case AlAbrirRole: return d.alAbrir;
        case VentanasRole: return mapaDe(d).value("ventanas");
        case CantidadVentanasRole: return static_cast<int>(d.ventanas.size());
        case ResumenRole: return mapaDe(d).value("resumen");
        case AplicadaRole: return d.id == aplicada_;
        default: return {};
    }
}

QHash<int, QByteArray> DistribucionesModel::roleNames() const {
    return {{IdRole, "distribucionId"},
            {NombreRole, "nombre"},
            {AlAbrirRole, "alAbrir"},
            {VentanasRole, "ventanas"},
            {CantidadVentanasRole, "cantidadVentanas"},
            {ResumenRole, "resumen"},
            {AplicadaRole, "aplicada"}};
}

// --- Monitores ------------------------------------------------------------------------

std::vector<MonitorVisto> DistribucionesModel::monitoresVistos() const {
    if (hayPrueba_) return prueba_;
    std::vector<MonitorVisto> out;
    if (monitores_ == nullptr) return out;
    for (const QVariant& x : monitores_->conectadosEnOrden()) {
        const QVariantMap m = x.toMap();
        out.push_back({m.value("id").toString(),
                       QRect(m.value("x").toInt(), m.value("y").toInt(), m.value("ancho").toInt(), m.value("alto").toInt()),
                       m.value("principal").toBool(), m.value("orden").toInt(), m.value("nombre").toString()});
    }
    return out;
}

void DistribucionesModel::setMonitoresDePrueba(std::vector<MonitorVisto> m) {
    hayPrueba_ = true;
    prueba_ = std::move(m);
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {VentanasRole, ResumenRole});
    emit cambio();
    reaplicar(); // como cuando MonitoresModel avisa

}

QRect DistribucionesModel::limites() const {
    QRect l;
    for (const MonitorVisto& m : monitoresVistos()) l = l.united(m.geometria);
    return l;
}

double DistribucionesModel::proporcion() const {
    const QRect l = limites();
    return l.height() > 0 ? static_cast<double>(l.width()) / l.height() : 16.0 / 9.0;
}

QVariantList DistribucionesModel::monitores() const {
    const QRect l = limites();
    QVariantList out;
    for (const MonitorVisto& m : monitoresVistos()) {
        const QRectF d = l.width() > 0 && l.height() > 0
                             ? QRectF((m.geometria.x() - l.x()) / static_cast<double>(l.width()),
                                      (m.geometria.y() - l.y()) / static_cast<double>(l.height()),
                                      m.geometria.width() / static_cast<double>(l.width()),
                                      m.geometria.height() / static_cast<double>(l.height()))
                             : QRectF();
        out << QVariantMap{{"id", m.id},
                           {"orden", m.orden},
                           {"nombre", m.nombre},
                           {"principal", m.principal},
                           {"ancho", m.geometria.width()},
                           {"alto", m.geometria.height()},
                           {"dibujo", mapaDeRect(d)}};
    }
    return out;
}

QString DistribucionesModel::resolverMonitor(const QString& quien) const {
    for (const MonitorVisto& m : monitoresVistos())
        if (m.id == quien || QString::number(m.orden) == quien || m.nombre.compare(quien, Qt::CaseInsensitive) == 0)
            return m.id;
    if (monitores_ != nullptr) return monitores_->resolver(quien);
    return {};
}

// --- Lectura ----------------------------------------------------------------------------

int DistribucionesModel::filaDe(const QString& id) const {
    for (std::size_t i = 0; i < filas_.size(); ++i)
        if (filas_[i].id == id) return static_cast<int>(i);
    for (std::size_t i = 0; i < filas_.size(); ++i)
        if (filas_[i].nombre.compare(id, Qt::CaseInsensitive) == 0) return static_cast<int>(i);
    return -1;
}

const Distribucion* DistribucionesModel::buscar(const QString& idONombre) const {
    const int r = filaDe(idONombre.trimmed());
    return r < 0 ? nullptr : &filas_[static_cast<std::size_t>(r)];
}

QVariantMap DistribucionesModel::mapaDe(const Distribucion& d) const {
    const std::vector<MonitorVisto> hay = monitoresVistos();
    const QRect l = limites();
    QVariantList vs;
    QSet<QString> usados;
    for (const VentanaDistribucion& v : d.ventanas) {
        QVariantMap m = mapaDeVentana(v);
        bool falta = false;
        const int i = monitorPara(v, hay, &falta);
        m["faltaMonitor"] = falta;
        QStringList titulos;
        for (const QString& c : v.cosas) titulos << (catalogo_ != nullptr ? catalogo_->tituloDe(c) : c);
        m["titulo"] = v.nombre.isEmpty() ? titulos.join(QStringLiteral(" + ")) : v.nombre;
        m["queMuestra"] = titulos.join(QStringLiteral(" + "));
        if (i >= 0) {
            const MonitorVisto& mon = hay[static_cast<std::size_t>(i)];
            usados.insert(mon.id);
            m["enMonitor"] = mon.id;
            m["enMonitorOrden"] = mon.orden;
            const QRect g = v.pantallaCompleta ? mon.geometria : geometriaEn(v, mon.geometria);
            if (l.width() > 0 && l.height() > 0)
                m["dibujo"] = mapaDeRect(QRectF((g.x() - l.x()) / static_cast<double>(l.width()),
                                                (g.y() - l.y()) / static_cast<double>(l.height()),
                                                g.width() / static_cast<double>(l.width()),
                                                g.height() / static_cast<double>(l.height())));
        } else {
            m["enMonitor"] = QString();
        }
        vs << m;
    }
    const int n = static_cast<int>(d.ventanas.size());
    const QString resumen = QCoreApplication::translate("pantallas", "%n ventana(s)", nullptr, n) +
                            (usados.isEmpty() ? QString() : QStringLiteral(" · ") + QCoreApplication::translate("pantallas", "%n monitor(es)", nullptr, static_cast<int>(usados.size())));
    return {{"id", d.id},           {"nombre", d.nombre},   {"alAbrir", d.alAbrir}, {"ventanas", vs},
            {"cantidad", n},        {"resumen", resumen},   {"aplicada", d.id == aplicada_}};
}

QVariantMap DistribucionesModel::porId() const {
    QVariantMap out;
    for (const Distribucion& d : filas_) out.insert(d.id, mapaDe(d));
    return out;
}

QVariantList DistribucionesModel::plantillas() {
    return {QVariantMap{{"id", "mosaico-2x1"},
                        {"titulo", QCoreApplication::translate("pantallas", "Mosaico 2×1")},
                        {"ayuda", QCoreApplication::translate("pantallas", "Dos ventanas lado a lado en un monitor (de entrada, el programa y el previo).")}},
            QVariantMap{{"id", "mosaico-2x2"},
                        {"titulo", QCoreApplication::translate("pantallas", "Mosaico 2×2")},
                        {"ayuda", QCoreApplication::translate("pantallas", "Cuatro ventanas en cuadrícula en un monitor.")}},
            QVariantMap{{"id", "una-por-monitor"},
                        {"titulo", QCoreApplication::translate("pantallas", "Una por monitor")},
                        {"ayuda", QCoreApplication::translate("pantallas", "Una cosa a pantalla completa en cada monitor que no sea el principal.")}},
            QVariantMap{{"id", "programa-previo-multiview"},
                        {"titulo", QCoreApplication::translate("pantallas", "Programa + previo + multivista")},
                        {"ayuda", QCoreApplication::translate("pantallas", "En un monitor: el programa y el previo arriba, la multivista abajo.")}}};
}

// --- Armar --------------------------------------------------------------------------------

QString DistribucionesModel::validarCosas(QStringList& cosas) const {
    QStringList limpias;
    for (const QString& c : cosas) {
        QString t = c.trimmed();
        if (t.isEmpty()) continue;
        if (catalogo_ != nullptr) {
            const CatalogoVisible::Cosa* cosa = catalogo_->buscar(t);
            if (cosa == nullptr)
                return QCoreApplication::translate("pantallas", "No hay nada que se llame «%1» en lo que se puede mostrar (catalogo_listar).").arg(t);
            if (cosa->dibujo.isEmpty())
                return QCoreApplication::translate("pantallas", "«%1» no se puede mostrar en una ventana (solo en una TV de la red).").arg(cosa->titulo);
            t = catalogo_->claveDe(t);
        }
        if (!limpias.contains(t)) limpias << t;
    }
    if (limpias.isEmpty()) return QCoreApplication::translate("pantallas", "Cada ventana tiene que mostrar algo: `cosas` con ids de catalogo_listar.");
    if (limpias.size() > kMaxCosas) return QCoreApplication::translate("pantallas", "Como mucho %1 cosas en una ventana: más ya no se leen.").arg(kMaxCosas);
    cosas = limpias;
    return {};
}

QString DistribucionesModel::parsearVentanas(const QVariantList& lista, std::vector<VentanaDistribucion>& out) const {
    if (lista.size() > kMaxVentanas) return QCoreApplication::translate("pantallas", "Como mucho %1 ventanas en una distribución.").arg(kMaxVentanas);
    out.clear();
    const auto resolver = [this](const QString& q) { return resolverMonitor(q); };
    for (int i = 0; i < lista.size(); ++i) {
        VentanaDistribucion v;
        QString e = ventanaDeMapa(lista[i].toMap(), v, resolver);
        if (e.isEmpty()) e = validarCosas(v.cosas);
        if (!e.isEmpty()) return QCoreApplication::translate("pantallas", "Ventana %1: %2").arg(i + 1).arg(e);
        out.push_back(std::move(v));
    }
    return {};
}

QString DistribucionesModel::idNuevo(const QString& nombre) const {
    QString base;
    for (const QChar c : nombre.toLower()) {
        if (c.isLetterOrNumber()) base += c;
        else if (!base.endsWith('-')) base += '-';
    }
    while (base.endsWith('-')) base.chop(1);
    while (base.startsWith('-')) base.remove(0, 1);
    if (base.isEmpty()) base = QStringLiteral("distribucion");
    QString id = base;
    for (int n = 2; std::any_of(filas_.begin(), filas_.end(), [&](const Distribucion& d) { return d.id == id; }); ++n)
        id = base + "-" + QString::number(n);
    return id;
}

void DistribucionesModel::poner(Distribucion d) {
    std::vector<Distribucion> nuevas = filas_;
    if (d.alAbrir)
        for (Distribucion& o : nuevas)
            if (o.id != d.id) o.alAbrir = false;
    auto it = std::find_if(nuevas.begin(), nuevas.end(), [&](const Distribucion& o) { return o.id == d.id; });
    if (it == nuevas.end()) nuevas.push_back(d);
    else *it = d;
    if (ListaConClave<Distribucion>::aplicar(std::move(nuevas))) {
        emit cambio();
        guardar();
    }
    // La que está puesta se edita EN VIVO: sus ventanas siguen al editor.
    if (d.id == aplicada_) reaplicar();
}

QString DistribucionesModel::crear(const QString& nombrePedido, const QVariantList& ventanas) {
    const QString nombre = nombrePedido.trimmed();
    if (nombre.isEmpty()) return avisarError(QCoreApplication::translate("pantallas", "Ponele un nombre a la distribución.")), QString();
    for (const Distribucion& o : filas_)
        if (o.nombre.compare(nombre, Qt::CaseInsensitive) == 0)
            return avisarError(QCoreApplication::translate("pantallas", "Ya hay una distribución que se llama «%1»: editala, o elegí otro nombre.").arg(nombre)),
                   QString();
    Distribucion d;
    if (const QString e = parsearVentanas(ventanas, d.ventanas); !e.isEmpty()) return avisarError(e), QString();
    d.nombre = nombre;
    d.id = idNuevo(nombre);
    poner(d);
    return d.id;
}

bool DistribucionesModel::editar(const QString& idPedido, const QVariantMap& cambios) {
    const int r = filaDe(idPedido);
    if (r < 0) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(idPedido));
    Distribucion d = filas_[static_cast<std::size_t>(r)];
    if (cambios.contains("nombre")) {
        const QString nombre = cambios.value("nombre").toString().trimmed();
        if (nombre.isEmpty()) return sinError(QCoreApplication::translate("pantallas", "Ponele un nombre a la distribución."));
        for (const Distribucion& o : filas_)
            if (o.id != d.id && o.nombre.compare(nombre, Qt::CaseInsensitive) == 0)
                return sinError(QCoreApplication::translate("pantallas", "Ya hay una distribución que se llama «%1».").arg(nombre));
        d.nombre = nombre;
    }
    if (cambios.contains("ventanas"))
        if (const QString e = parsearVentanas(cambios.value("ventanas").toList(), d.ventanas); !e.isEmpty())
            return sinError(e);
    if (cambios.contains("alAbrir")) d.alAbrir = cambios.value("alAbrir").toBool();
    poner(d);
    return true;
}

QString DistribucionesModel::duplicar(const QString& id, const QString& nombrePedido) {
    const Distribucion* o = buscar(id);
    if (o == nullptr) return avisarError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id)), QString();
    QString nombre = nombrePedido.trimmed();
    if (nombre.isEmpty()) {
        const QString base = QCoreApplication::translate("pantallas", "%1 (copia)").arg(o->nombre);
        nombre = base;
        for (int n = 2; buscar(nombre) != nullptr; ++n) nombre = base + " " + QString::number(n);
    }
    if (buscar(nombre) != nullptr)
        return avisarError(QCoreApplication::translate("pantallas", "Ya hay una distribución que se llama «%1».").arg(nombre)), QString();
    Distribucion d = *o;
    d.nombre = nombre;
    d.id = idNuevo(nombre);
    d.alAbrir = false;
    poner(d);
    return d.id;
}

bool DistribucionesModel::renombrar(const QString& id, const QString& nombre) {
    return editar(id, QVariantMap{{"nombre", nombre}});
}

bool DistribucionesModel::setAlAbrir(const QString& id, bool si) { return editar(id, QVariantMap{{"alAbrir", si}}); }

bool DistribucionesModel::borrar(const QString& id) {
    const int r = filaDe(id);
    if (r < 0) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id));
    const QString cual = filas_[static_cast<std::size_t>(r)].id;
    std::vector<Distribucion> nuevas = filas_;
    nuevas.erase(nuevas.begin() + r);
    // Sus ventanas quedan como están (borrar la receta no cierra lo que hay puesto).
    if (aplicada_ == cual) aplicada_.clear();
    ListaConClave<Distribucion>::aplicar(std::move(nuevas));
    emit cambio();
    guardar();
    return true;
}

QString DistribucionesModel::crearDePlantilla(const QString& plantilla, const QString& nombrePedido,
                                              const QString& monitorPedido, const QStringList& cosasPedidas) {
    const std::vector<MonitorVisto> hay = monitoresVistos();
    QString monitor;
    if (!monitorPedido.trimmed().isEmpty()) {
        monitor = resolverMonitor(monitorPedido.trimmed());
        if (monitor.isEmpty())
            return avisarError(QCoreApplication::translate("pantallas", "No conozco el monitor «%1» (monitor_listar).").arg(monitorPedido)), QString();
    } else {
        VentanaDistribucion sinMonitor;
        const int i = monitorPara(sinMonitor, hay);
        if (i >= 0) monitor = hay[static_cast<std::size_t>(i)].id;
    }
    // Lo de entrada: el programa, el previo, la primera multivista y la primera escena.
    QString multiview, escena;
    if (catalogo_ != nullptr)
        for (const CatalogoVisible::Cosa& c : catalogo_->cosas()) {
            if (multiview.isEmpty() && c.grupo == "multiview") multiview = c.id;
            if (escena.isEmpty() && c.grupo == "escena") escena = c.id;
        }
    QStringList porOmision{"programa", "preview"};
    if (!multiview.isEmpty()) porOmision << multiview;
    if (!escena.isEmpty()) porOmision << escena;
    const QStringList cosas = cosasPedidas.isEmpty() ? porOmision : cosasPedidas;

    const auto ventana = [&](const QString& cosa, double x, double y, double w, double h, const QString& mon,
                             bool completa = false) {
        return QVariantMap{{"cosas", QStringList{cosa}}, {"monitor", mon}, {"x", x}, {"y", y},
                           {"ancho", w},                {"alto", h},       {"pantallaCompleta", completa}};
    };
    QVariantList vs;
    QString titulo;
    if (plantilla == "mosaico-2x1") {
        for (int i = 0; i < std::min<qsizetype>(2, cosas.size()); ++i) vs << ventana(cosas[i], 0.5 * i, 0, 0.5, 1, monitor);
    } else if (plantilla == "mosaico-2x2") {
        for (int i = 0; i < std::min<qsizetype>(4, cosas.size()); ++i)
            vs << ventana(cosas[i], 0.5 * (i % 2), 0.5 * (i / 2), 0.5, 0.5, monitor);
    } else if (plantilla == "una-por-monitor") {
        std::vector<MonitorVisto> destino;
        for (const MonitorVisto& m : hay)
            if (!m.principal) destino.push_back(m);
        if (destino.empty()) destino = hay;
        if (destino.empty()) return avisarError(QCoreApplication::translate("pantallas", "No hay monitores conectados.")), QString();
        for (std::size_t i = 0; i < destino.size() && i < static_cast<std::size_t>(cosas.size()); ++i)
            vs << ventana(cosas[static_cast<int>(i)], 0, 0, 1, 1, destino[i].id, true);
    } else if (plantilla == "programa-previo-multiview") {
        const QString p = cosasPedidas.value(0, "programa"), pr = cosasPedidas.value(1, "preview"),
                      mv = cosasPedidas.value(2, multiview);
        const double alto = mv.isEmpty() ? 1.0 : 0.5;
        vs << ventana(p, 0, 0, 0.5, alto, monitor) << ventana(pr, 0.5, 0, 0.5, alto, monitor);
        if (!mv.isEmpty()) vs << ventana(mv, 0, 0.5, 1, 0.5, monitor);
    } else {
        return avisarError(QCoreApplication::translate("pantallas", "No hay una plantilla «%1»: mosaico-2x1, mosaico-2x2, una-por-monitor o "
                               "programa-previo-multiview.")
                               .arg(plantilla)),
               QString();
    }
    for (const QVariant& p : plantillas())
        if (p.toMap().value("id") == plantilla) titulo = p.toMap().value("titulo").toString();
    QString nombre = nombrePedido.trimmed();
    if (nombre.isEmpty()) {
        nombre = titulo;
        for (int n = 2; buscar(nombre) != nullptr; ++n) nombre = titulo + " " + QString::number(n);
    }
    return crear(nombre, vs);
}

// --- Sus ventanas ---------------------------------------------------------------------------

bool DistribucionesModel::agregarVentana(const QString& id, const QVariantMap& mapa) {
    const int r = filaDe(id);
    if (r < 0) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id));
    Distribucion d = filas_[static_cast<std::size_t>(r)];
    if (static_cast<int>(d.ventanas.size()) >= kMaxVentanas)
        return sinError(QCoreApplication::translate("pantallas", "Como mucho %1 ventanas en una distribución.").arg(kMaxVentanas));
    VentanaDistribucion v;
    QString e = ventanaDeMapa(mapa, v, [this](const QString& q) { return resolverMonitor(q); });
    if (e.isEmpty()) e = validarCosas(v.cosas);
    if (!e.isEmpty()) return sinError(e);
    d.ventanas.push_back(v);
    poner(d);
    return true;
}

bool DistribucionesModel::quitarVentana(const QString& id, int indice) {
    const int r = filaDe(id);
    if (r < 0) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id));
    Distribucion d = filas_[static_cast<std::size_t>(r)];
    if (indice < 0 || indice >= static_cast<int>(d.ventanas.size()))
        return sinError(QCoreApplication::translate("pantallas", "Esa distribución no tiene una ventana %1.").arg(indice + 1));
    d.ventanas.erase(d.ventanas.begin() + indice);
    poner(d);
    return true;
}

bool DistribucionesModel::setVentana(const QString& id, int indice, const QVariantMap& cambios) {
    const int r = filaDe(id);
    if (r < 0) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id));
    Distribucion d = filas_[static_cast<std::size_t>(r)];
    if (indice < 0 || indice >= static_cast<int>(d.ventanas.size()))
        return sinError(QCoreApplication::translate("pantallas", "Esa distribución no tiene una ventana %1.").arg(indice + 1));
    VentanaDistribucion& v = d.ventanas[static_cast<std::size_t>(indice)];
    QString e = ventanaDeMapa(cambios, v, [this](const QString& q) { return resolverMonitor(q); });
    if (e.isEmpty()) e = validarCosas(v.cosas);
    if (!e.isEmpty()) return sinError(e);
    poner(d);
    return true;
}

bool DistribucionesModel::moverEnDibujo(const QString& id, int indice, double x, double y, double ancho, double alto) {
    const int r = filaDe(id);
    if (r < 0) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id));
    Distribucion d = filas_[static_cast<std::size_t>(r)];
    if (indice < 0 || indice >= static_cast<int>(d.ventanas.size()))
        return sinError(QCoreApplication::translate("pantallas", "Esa distribución no tiene una ventana %1.").arg(indice + 1));
    const std::vector<MonitorVisto> hay = monitoresVistos();
    const QRect l = limites();
    if (hay.empty() || l.isEmpty()) return sinError(QCoreApplication::translate("pantallas", "No hay monitores conectados."));
    const QRect g(l.x() + static_cast<int>(std::lround(x * l.width())), l.y() + static_cast<int>(std::lround(y * l.height())),
                  std::max(1, static_cast<int>(std::lround(ancho * l.width()))),
                  std::max(1, static_cast<int>(std::lround(alto * l.height()))));
    // El monitor donde cae su centro; si cae entre dos, el más cercano.
    const QPoint c = g.center();
    std::size_t elegido = 0;
    double mejor = 1e18;
    for (std::size_t i = 0; i < hay.size(); ++i) {
        const QRect& m = hay[i].geometria;
        if (m.contains(c)) {
            elegido = i;
            break;
        }
        const double dx = std::max({m.left() - c.x(), 0, c.x() - m.right()});
        const double dy = std::max({m.top() - c.y(), 0, c.y() - m.bottom()});
        if (dx * dx + dy * dy < mejor) {
            mejor = dx * dx + dy * dy;
            elegido = i;
        }
    }
    const MonitorVisto& mon = hay[elegido];
    VentanaDistribucion& v = d.ventanas[static_cast<std::size_t>(indice)];
    v.monitor = mon.id;
    v.relativa = aRelativa(g, mon.geometria);
    // Si tenía píxeles fijados, los píxeles siguen al arrastre.
    if (v.absoluta.width() > 0) {
        const QRect a = aAbsoluta(v.relativa, mon.geometria);
        v.absoluta = QRect(a.topLeft() - mon.geometria.topLeft(), a.size());
    }
    poner(d);
    return true;
}

// --- Usarla ------------------------------------------------------------------------------

std::vector<Lienzo> DistribucionesModel::resolver(const Distribucion& d, QStringList* avisos) const {
    const std::vector<MonitorVisto> hay = monitoresVistos();
    // Por monitor (en el orden en que aparecen en la distribución): sus piezas en
    // píxeles del escritorio y, después, relativas al rectángulo que las encierra.
    struct Grupo {
        int monitor = -1;
        QString id;
        std::vector<std::pair<Pieza, QRect>> piezas;
        bool encima = false;
    };
    std::vector<Grupo> grupos;
    for (const VentanaDistribucion& v : d.ventanas) {
        bool falta = false;
        const int i = monitorPara(v, hay, &falta);
        Pieza p;
        for (const QString& c : v.cosas) p.cosas << (catalogo_ != nullptr ? catalogo_->claveDe(c) : c);
        // Lo que ya no existe (una escena borrada) no se dibuja: se avisa.
        if (catalogo_ != nullptr) {
            QStringList quedan;
            for (const QString& c : p.cosas) {
                const CatalogoVisible::Cosa* cosa = catalogo_->buscar(c);
                if (cosa != nullptr && !cosa->dibujo.isEmpty()) quedan << c;
                else if (avisos != nullptr)
                    *avisos << QCoreApplication::translate("pantallas", "«%1» ya no se puede mostrar: se saltea.").arg(c);
            }
            p.cosas = quedan;
        }
        if (p.cosas.isEmpty()) continue;
        p.nombre = v.nombre;
        p.disposicion = v.disposicion;
        p.pantallaCompleta = v.pantallaCompleta;
        QRect g;
        QString id = v.monitor;
        if (i >= 0) {
            const MonitorVisto& m = hay[static_cast<std::size_t>(i)];
            id = m.id;
            g = v.pantallaCompleta ? m.geometria : geometriaEn(v, m.geometria);
            if (falta && avisos != nullptr) {
                const QString quien = !v.nombre.isEmpty()   ? v.nombre
                                      : catalogo_ != nullptr ? catalogo_->tituloDe(v.cosas.value(0))
                                                             : v.cosas.value(0);
                *avisos << QCoreApplication::translate("pantallas", "«%1»: su monitor no está conectado; va en el %2 (%3).")
                               .arg(quien)
                               .arg(m.orden)
                               .arg(m.nombre);
            }
        }
        auto it = std::find_if(grupos.begin(), grupos.end(), [&](const Grupo& gr) { return gr.id == id; });
        if (it == grupos.end()) {
            grupos.push_back(Grupo{i, id, {}, false});
            it = grupos.end() - 1;
        }
        it->piezas.emplace_back(p, g);
        it->encima = it->encima || v.encima;
    }
    std::vector<Lienzo> out;
    for (const Grupo& gr : grupos) {
        Lienzo l;
        l.id = gr.id;
        l.encima = gr.encima;
        QRect caja;
        for (const auto& pg : gr.piezas) caja = caja.united(pg.second);
        if (gr.monitor >= 0) {
            const MonitorVisto& m = hay[static_cast<std::size_t>(gr.monitor)];
            l.orden = m.orden;
            caja = caja.intersected(m.geometria);
            l.pantallaCompleta = caja == m.geometria;
            // Lo fijo de «Pantallas conectadas» en el mismo monitor: quedan las dos.
            if (monitores_ != nullptr && avisos != nullptr &&
                !monitores_->monitor(m.id).value("cosas").toStringList().isEmpty())
                *avisos << QCoreApplication::translate("pantallas", "El monitor %1 también tiene algo fijo (Pantallas conectadas): "
                                                                    "se van a tapar. Quitalo ahí si manda la distribución.")
                               .arg(m.orden);
        }
        l.geometria = caja;
        for (const auto& [pieza, g] : gr.piezas) {
            Pieza p = pieza;
            p.rect = caja.width() > 0 && caja.height() > 0
                         ? QRectF((g.x() - caja.x()) / static_cast<double>(caja.width()),
                                  (g.y() - caja.y()) / static_cast<double>(caja.height()),
                                  g.width() / static_cast<double>(caja.width()),
                                  g.height() / static_cast<double>(caja.height()))
                         : QRectF(0, 0, 1, 1);
            l.piezas.push_back(p);
        }
        out.push_back(std::move(l));
    }
    return out;
}

bool DistribucionesModel::aplicar(const QString& idPedido) {
    const Distribucion* d = buscar(idPedido);
    if (d == nullptr) return sinError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(idPedido));
    const QString antes = aplicada_;
    aplicada_ = d->id;
    // EXACTAMENTE lo suyo: las ventanas sueltas se cierran.
    if (ventanas_ != nullptr) ventanas_->cerrarTodas();
    aviso_.clear();
    reaplicar();
    for (const QString& id : {antes, aplicada_})
        if (const int r = filaDe(id); r >= 0 && !id.isEmpty()) emit dataChanged(index(r), index(r), {AplicadaRole});
    emit cambio();
    if (antes != aplicada_) guardar();
    return true;
}

void DistribucionesModel::reaplicar() {
    if (aplicada_.isEmpty()) return;
    const Distribucion* d = buscar(aplicada_);
    if (d == nullptr) return;
    QStringList avisos;
    lienzos_.poner(resolver(*d, &avisos));
    const QString nuevo = avisos.join('\n');
    if (nuevo == aviso_) return;
    aviso_ = nuevo;
    emit cambio();
    // Lo que no se pudo no es un error (se aplicó el resto): a la vista, salvo que quien
    // llamó esté capturando (el MCP lo devuelve él mismo, con `aviso`).
    if (!aviso_.isEmpty() && !CapturaError::tomar(QString())) emit notify(aviso_, "warn");
}

Distribucion DistribucionesModel::foto() const {
    Distribucion d;
    const std::vector<MonitorVisto> hay = monitoresVistos();
    const auto monitorDe = [&hay](const QString& id) -> const MonitorVisto* {
        for (const MonitorVisto& m : hay)
            if (m.id == id) return &m;
        return nullptr;
    };
    // Los lienzos: cada pieza vuelve a ser una ventana, relativa a su monitor.
    for (const Lienzo& l : lienzos_.lienzos()) {
        const MonitorVisto* m = monitorDe(l.id);
        for (const Pieza& p : l.piezas) {
            VentanaDistribucion v;
            v.nombre = p.nombre;
            v.cosas = p.cosas;
            v.disposicion = p.disposicion;
            v.encima = l.encima;
            v.pantallaCompleta = p.pantallaCompleta;
            v.monitor = l.id;
            if (m != nullptr && !p.pantallaCompleta) {
                const QRect g(l.geometria.x() + static_cast<int>(std::lround(p.rect.x() * l.geometria.width())),
                              l.geometria.y() + static_cast<int>(std::lround(p.rect.y() * l.geometria.height())),
                              static_cast<int>(std::lround(p.rect.width() * l.geometria.width())),
                              static_cast<int>(std::lround(p.rect.height() * l.geometria.height())));
                v.relativa = aRelativa(g, m->geometria);
            }
            d.ventanas.push_back(v);
        }
    }
    if (ventanas_ == nullptr) return d;
    // Las ventanas sueltas.
    for (const FilaVentana& f : ventanas_->ventanas()) {
        VentanaDistribucion v;
        v.cosas = f.cosas;
        v.disposicion = disposicionValida(f.distribucion) ? f.distribucion : QStringLiteral("grid");
        v.pantallaCompleta = f.pantallaCompleta;
        // Su monitor: el que dice, si está; si no, donde cae su centro; si no, el primero.
        int i = -1;
        for (std::size_t k = 0; k < hay.size(); ++k)
            if (!f.monitor.isEmpty() && hay[k].id == f.monitor) i = static_cast<int>(k);
        if (i < 0 && f.geometria.isValid())
            for (std::size_t k = 0; k < hay.size(); ++k)
                if (hay[k].geometria.contains(f.geometria.center())) i = static_cast<int>(k);
        if (i < 0 && !hay.empty()) i = 0;
        if (i >= 0) {
            const MonitorVisto& m = hay[static_cast<std::size_t>(i)];
            v.monitor = m.id;
            v.relativa = f.geometria.width() > 0 && f.geometria.height() > 0 ? aRelativa(f.geometria, m.geometria)
                                                                             : QRectF(0, 0, 1, 1);
        } else {
            v.monitor = f.monitor;
        }
        d.ventanas.push_back(v);
    }
    return d;
}

QString DistribucionesModel::capturar(const QString& nombre, const QString& id) {
    const Distribucion f = foto();
    if (!id.trimmed().isEmpty()) {
        const int r = filaDe(id.trimmed());
        if (r < 0)
            return avisarError(QCoreApplication::translate("pantallas", "No hay una distribución «%1».").arg(id)), QString();
        Distribucion d = filas_[static_cast<std::size_t>(r)];
        d.ventanas = f.ventanas;
        if (!nombre.trimmed().isEmpty() && nombre.trimmed() != d.nombre) {
            if (const Distribucion* o = buscar(nombre.trimmed()); o != nullptr && o->id != d.id)
                return avisarError(QCoreApplication::translate("pantallas", "Ya hay una distribución que se llama «%1».")
                                       .arg(nombre.trimmed())),
                       QString();
            d.nombre = nombre.trimmed();
        }
        poner(d);
        return d.id;
    }
    QVariantList vs;
    for (const VentanaDistribucion& v : f.ventanas) vs << mapaDeVentana(v);
    return crear(nombre, vs);
}

int DistribucionesModel::cerrarVentanas() {
    const int n = lienzos_.cerrarTodo();
    const QString antes = aplicada_;
    aplicada_.clear();
    aviso_.clear();
    if (const int r = filaDe(antes); r >= 0 && !antes.isEmpty()) emit dataChanged(index(r), index(r), {AplicadaRole});
    emit cambio();
    if (!antes.isEmpty()) guardar();
    return n;
}

void DistribucionesModel::aplicarAlAbrir() {
    for (const Distribucion& d : filas_)
        if (d.alAbrir) {
            aplicar(d.id);
            return;
        }
    // Ninguna marcada: la que estaba (un corte de luz no deja los monitores vacíos).
    // Sin cerrar las ventanas sueltas, que también volvieron como estaban.
    if (!aplicada_.isEmpty() && buscar(aplicada_) != nullptr) {
        reaplicar();
        emit cambio();
    } else {
        aplicada_.clear();
    }
}

// --- Persistencia ------------------------------------------------------------------------

void DistribucionesModel::cargar() {
    if (archivo_.empty()) return;
    const QJsonObject o = config::leerQObjeto(archivo_);
    // Lo guardado ya está resuelto: los monitores van por su id, tal cual.
    const auto tal = [](const QString& q) { return q; };
    for (const QJsonValue& x : o.value("distribuciones").toArray()) {
        const QVariantMap m = x.toObject().toVariantMap();
        Distribucion d;
        d.id = m.value("id").toString();
        d.nombre = m.value("nombre").toString();
        d.alAbrir = m.value("alAbrir").toBool();
        if (d.id.isEmpty() || d.nombre.isEmpty() || filaDe(d.id) >= 0) continue;
        for (const QVariant& vv : m.value("ventanas").toList()) {
            VentanaDistribucion v;
            if (!ventanaDeMapa(vv.toMap(), v, tal).isEmpty() || v.cosas.isEmpty()) continue;
            d.ventanas.push_back(v);
        }
        filas_.push_back(std::move(d));
    }
    aplicada_ = o.value("aplicada").toString();
}

void DistribucionesModel::guardar() const {
    if (archivo_.empty()) return;
    QJsonArray ds;
    for (const Distribucion& d : filas_) {
        QJsonArray vs;
        for (const VentanaDistribucion& v : d.ventanas) vs.append(QJsonObject::fromVariantMap(mapaDeVentana(v)));
        QJsonObject j{{"id", d.id}, {"nombre", d.nombre}, {"ventanas", vs}};
        if (d.alAbrir) j["alAbrir"] = true;
        ds.append(j);
    }
    QJsonObject raiz{{"distribuciones", ds}};
    // La que está puesta: vuelve sola al reiniciar (aplicarAlAbrir).
    if (!aplicada_.isEmpty()) raiz["aplicada"] = aplicada_;
    config::guardarQ(archivo_, raiz);
}

} // namespace ba::app
