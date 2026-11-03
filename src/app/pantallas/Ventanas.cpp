// ============================================================================
// src/app/pantallas/Ventanas.cpp — ver Ventanas.hpp
// ============================================================================
#include "pantallas/Ventanas.hpp"
#include "arranque/ArchivoConfigQt.hpp"

#include "pantallas/Catalogo.hpp"
#include "pantallas/Monitores.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app {
namespace {

/// Como mucho tantas cosas en una ventana (lo mismo que un monitor).
constexpr int kMaxCosas = 9;

QString distribucionValida(const QString& d) {
    return d == "focus" || d == "pip" || d == "row" ? d : QStringLiteral("grid");
}

} // namespace

VentanasModel::VentanasModel(std::filesystem::path archivo, CatalogoVisible* catalogo, MonitoresModel* monitores,
                             QObject* parent)
    : QAbstractListModel(parent), archivo_(std::move(archivo)), catalogo_(catalogo), monitores_(monitores) {
    cargar();
    // Los títulos salen del catálogo: si una escena se renombra o se borra, lo
    // que dice cada ventana cambia con ella.
    // Lo guardado por la clave de antes (una escena por el nombre) pasa a la de hoy (su id).
    if (catalogo_ != nullptr)
        connect(catalogo_, &CatalogoVisible::cambio, this, [this]() {
            for (int r = 0; r < rowCount(); ++r) {
                FilaVentana& f = filas_[static_cast<std::size_t>(r)];
                QStringList claves;
                for (const QString& c : f.cosas) claves << catalogo_->claveDe(c);
                if (claves == f.cosas) continue;
                f.cosas = claves;
                tocada(r, {CosasRole});
            }
            if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1), {TituloRole});
        });
    if (monitores_ != nullptr) connect(monitores_, &MonitoresModel::cambio, this, &VentanasModel::traducirMonitores);
    traducirMonitores();
}

int VentanasModel::rowCount(const QModelIndex& padre) const {
    return padre.isValid() ? 0 : static_cast<int>(filas_.size());
}

QVariant VentanasModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const FilaVentana& f = filas_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case VentanaIdRole: return f.id;
        case CosasRole: return f.cosas;
        case DistribucionRole: return f.distribucion;
        case MonitorRole: return f.monitor;
        case SistemaRole: return f.sistema;
        case PantallaCompletaRole: return f.pantallaCompleta;
        case GeometriaRole: return f.geometria;
        case TituloRole: return titulo(f);
        case NombreRole: return f.nombre;
        case FijaRole: return f.fija;
        case EncimaRole: return f.encima;
        default: return {};
    }
}

QHash<int, QByteArray> VentanasModel::roleNames() const {
    return {{VentanaIdRole, "ventanaId"},   {CosasRole, "cosas"},
            {DistribucionRole, "distribucion"}, {MonitorRole, "monitor"},
            {SistemaRole, "sistema"},       {PantallaCompletaRole, "pantallaCompleta"},
            {GeometriaRole, "geometria"},   {TituloRole, "titulo"},
            {NombreRole, "nombre"},         {FijaRole, "fija"},
            {EncimaRole, "encima"}};
}

// --- Lo que ya estaba guardado ------------------------------------------------------

QString VentanasModel::cosaDeClave(const QString& clave) {
    if (clave == "program") return QStringLiteral("programa");
    if (clave == "armada") return QStringLiteral("preview");
    if (clave.startsWith("escena:")) return clave;
    return "video:" + clave;
}

std::vector<FilaVentana> VentanasModel::desdeUiViejo(const QJsonArray& windows, const QString& multiviewElegida) {
    std::vector<FilaVentana> out;
    for (const QJsonValue& v : windows) {
        const QJsonObject w = v.toObject();
        const QString layout = w.value("layout").toString("grid");
        FilaVentana f;
        if (layout.startsWith("multiview")) {
            // «multiview:<id>» es esa; «multiview» solo seguía a la elegida.
            const QString id = layout.startsWith("multiview:") ? layout.mid(10) : multiviewElegida;
            if (id.isEmpty()) continue;
            f.cosas << "multiview:" + id;
        } else {
            for (const QJsonValue& k : w.value("keys").toArray()) {
                const QString c = cosaDeClave(k.toString());
                if (!k.toString().isEmpty() && !f.cosas.contains(c)) f.cosas << c;
            }
            f.distribucion = distribucionValida(layout);
        }
        if (f.cosas.isEmpty()) continue;
        f.sistema = w.value("screen").toString();
        f.pantallaCompleta = w.value("fullscreen").toBool();
        const QRect g(w.value("x").toInt(120), w.value("y").toInt(120), w.value("w").toInt(960), w.value("h").toInt(540));
        if (g.width() > 0 && g.height() > 0) f.geometria = g;
        out.push_back(f);
    }
    return out;
}

bool VentanasModel::migrarDesde(const std::filesystem::path& uiJson, const QString& multiviewElegida) {
    // Ya hay ventanas.json: lo de ui.json es viejo (o ya se migró).
    if (archivo_.empty() || !config::leerObjeto(archivo_).empty()) return false;
    const QJsonObject o = config::leerQObjeto(uiJson);
    if (!o.contains("windows")) return false;
    const std::vector<FilaVentana> viejas = desdeUiViejo(o.value("windows").toArray(), multiviewElegida);
    for (const FilaVentana& v : viejas) insertar(v);
    traducirMonitores();
    // Guardado en el acto: ui.json deja de tener las ventanas la próxima vez que
    // se guarde el tablero, y no tienen que quedar en ningún lado.
    guardar();
    if (!viejas.empty()) emit cambio();
    return !viejas.empty();
}

void VentanasModel::traducirMonitores() {
    if (monitores_ == nullptr) return;
    for (std::size_t i = 0; i < filas_.size(); ++i) {
        FilaVentana& f = filas_[i];
        if (!f.monitor.isEmpty() || f.sistema.isEmpty()) continue;
        const QString id = monitores_->resolver(f.sistema);
        if (id.isEmpty() || !monitores_->monitor(id).value("conectado").toBool()) continue;
        f.monitor = id;
        f.sistema.clear();
        tocada(static_cast<int>(i), {MonitorRole, SistemaRole});
    }
}

// --- Abrir y cerrar -----------------------------------------------------------------

QString VentanasModel::validar(QStringList& cosas) const {
    QStringList limpias;
    for (const QString& c : cosas) {
        const QString t = c.trimmed();
        if (t.isEmpty() || limpias.contains(t)) continue;
        if (catalogo_ != nullptr) {
            const CatalogoVisible::Cosa* cosa = catalogo_->buscar(t);
            if (cosa == nullptr)
                return QCoreApplication::translate("pantallas", "No hay nada que se llame «%1» en lo que se puede mostrar.").arg(t);
            if (cosa->dibujo.isEmpty())
                return QCoreApplication::translate("pantallas", "«%1» no se puede mostrar en una ventana (solo en una TV de la red).")
                    .arg(cosa->titulo);
        }
        limpias << t;
    }
    if (limpias.isEmpty()) return QCoreApplication::translate("pantallas", "Elegí qué mostrar.");
    if (limpias.size() > kMaxCosas)
        return QCoreApplication::translate("pantallas", "Como mucho %1 cosas en una ventana: más ya no se leen.").arg(kMaxCosas);
    cosas = limpias;
    return {};
}

void VentanasModel::insertar(FilaVentana f) {
    f.id = proximo_++;
    const int r = rowCount();
    beginInsertRows({}, r, r);
    filas_.push_back(std::move(f));
    endInsertRows();
}

int VentanasModel::abrirEn(const QStringList& pedidas, const QString& distribucion, const QString& monitor,
                           const QRect& geometria, bool pantallaCompleta, QString* error) {
    QStringList cosas = pedidas;
    if (const QString e = validar(cosas); !e.isEmpty()) {
        if (error != nullptr) *error = e;
        return 0;
    }
    // Lo mismo dos veces no: se trae al frente la que ya está.
    for (const FilaVentana& f : filas_)
        if (f.cosas == cosas) {
            emit traerAlFrente(f.id);
            return f.id;
        }
    FilaVentana f;
    f.cosas = cosas;
    f.distribucion = distribucionValida(distribucion);
    f.monitor = monitor;
    f.pantallaCompleta = pantallaCompleta;
    f.geometria = geometria;
    insertar(f);
    emit cambio();
    guardar();
    return filas_.back().id;
}

int VentanasModel::abrirCon(const QStringList& cosas, const QString& distribucion, QString* error) {
    return abrirEn(cosas, distribucion, {}, {}, false, error);
}

int VentanasModel::abrir(const QStringList& cosas, const QString& distribucion) {
    return abrirCon(cosas, distribucion);
}

int VentanasModel::abrirVideo(const QStringList& claves, const QString& distribucion) {
    QStringList cosas;
    for (const QString& k : claves) cosas << cosaDeClave(k);
    return abrirCon(cosas, distribucion);
}

void VentanasModel::quitarFila(int r) {
    beginRemoveRows({}, r, r);
    filas_.erase(filas_.begin() + r);
    endRemoveRows();
}

void VentanasModel::cerrar(int id) {
    const int r = filaDe(id);
    if (r < 0) return;
    quitarFila(r);
    emit cambio();
    guardar();
}

int VentanasModel::fijas() const {
    int n = 0;
    for (const FilaVentana& f : filas_) n += f.fija ? 1 : 0;
    return n;
}

int VentanasModel::cerrarFijas() {
    int n = 0;
    for (int r = rowCount() - 1; r >= 0; --r)
        if (filas_[static_cast<std::size_t>(r)].fija) {
            quitarFila(r);
            ++n;
        }
    if (n > 0) {
        emit cambio();
        guardar();
    }
    return n;
}

QStringList VentanasModel::ponerExactas(std::vector<FilaVentana> deseadas) {
    QStringList avisos;
    // 1) Las que se pueden mostrar, con sus cosas limpias (como al abrir una).
    std::vector<FilaVentana> validas;
    for (FilaVentana& d : deseadas) {
        if (const QString e = validar(d.cosas); !e.isEmpty()) {
            avisos << e;
            continue;
        }
        d.distribucion = distribucionValida(d.distribucion);
        d.fija = true;
        d.sistema.clear();
        validas.push_back(std::move(d));
    }
    // 2) Cada deseada se empareja con una que ya esté abierta con las mismas cosas
    //    (cada abierta, una sola vez): esa se mueve en su lugar.
    std::vector<bool> usada(filas_.size(), false);
    std::vector<int> emparejada(validas.size(), 0);
    for (std::size_t i = 0; i < validas.size(); ++i)
        for (std::size_t r = 0; r < filas_.size(); ++r)
            if (!usada[r] && filas_[r].cosas == validas[i].cosas) {
                usada[r] = true;
                emparejada[i] = filas_[r].id;
                break;
            }
    bool hubo = false;
    // 3) Las que sobran se cierran (de atrás para adelante).
    for (int r = static_cast<int>(filas_.size()) - 1; r >= 0; --r)
        if (!usada[static_cast<std::size_t>(r)]) {
            quitarFila(r);
            hubo = true;
        }
    // 4) Las emparejadas toman lo de la distribución; las demás se abren.
    for (std::size_t i = 0; i < validas.size(); ++i) {
        const FilaVentana& d = validas[i];
        if (emparejada[i] == 0) {
            insertar(d);
            hubo = true;
            continue;
        }
        const int r = filaDe(emparejada[i]);
        FilaVentana& f = filas_[static_cast<std::size_t>(r)];
        QList<int> roles;
        const bool lugar = f.monitor != d.monitor || f.geometria != d.geometria || f.pantallaCompleta != d.pantallaCompleta;
        if (f.distribucion != d.distribucion) roles << DistribucionRole;
        if (f.monitor != d.monitor) roles << MonitorRole;
        if (!f.sistema.isEmpty()) roles << SistemaRole;
        if (f.pantallaCompleta != d.pantallaCompleta) roles << PantallaCompletaRole;
        if (f.geometria != d.geometria) roles << GeometriaRole;
        if (f.nombre != d.nombre) roles << NombreRole;
        if (f.fija != d.fija) roles << FijaRole;
        if (f.encima != d.encima) roles << EncimaRole;
        if (roles.isEmpty()) continue;
        const int id = f.id;
        f = d;
        f.id = id;
        emit dataChanged(index(r), index(r), roles);
        if (lugar) emit ubicar(id, f.geometria);
        hubo = true;
    }
    if (hubo) {
        emit cambio();
        guardar();
    }
    return avisos;
}

void VentanasModel::cerrarCosa(const QString& cosa) {
    if (const int id = ventanaDe(cosa); id > 0) cerrar(id);
}

int VentanasModel::ventanaDe(const QString& cosa) const {
    for (const FilaVentana& f : filas_)
        if (f.cosas.size() == 1 && f.cosas.first() == cosa) return f.id;
    return 0;
}

QStringList VentanasModel::afuera() const {
    QStringList out;
    for (const FilaVentana& f : filas_)
        if (f.cosas.size() == 1) out << f.cosas.first();
    return out;
}

// --- Cambiar una ---------------------------------------------------------------------

QString VentanasModel::setCosas(int id, const QStringList& pedidas) {
    const int r = filaDe(id);
    if (r < 0) return QCoreApplication::translate("pantallas", "No hay una ventana con ese id.");
    QStringList cosas = pedidas;
    if (const QString e = validar(cosas); !e.isEmpty()) return e;
    FilaVentana& f = filas_[static_cast<std::size_t>(r)];
    if (f.cosas == cosas) return {};
    f.cosas = cosas;
    tocada(r, {CosasRole, TituloRole});
    emit cambio(); // `afuera` puede cambiar
    return {};
}

void VentanasModel::setDistribucion(int id, const QString& distribucion) {
    const int r = filaDe(id);
    const QString d = distribucionValida(distribucion);
    if (r < 0 || filas_[static_cast<std::size_t>(r)].distribucion == d) return;
    filas_[static_cast<std::size_t>(r)].distribucion = d;
    tocada(r, {DistribucionRole});
}

void VentanasModel::setMonitor(int id, const QString& monitor) {
    const int r = filaDe(id);
    if (r < 0) return;
    FilaVentana& f = filas_[static_cast<std::size_t>(r)];
    if (f.monitor == monitor && f.sistema.isEmpty()) return;
    f.monitor = monitor;
    f.sistema.clear();
    tocada(r, {MonitorRole, SistemaRole});
    // Una fija no se acomoda sola: se la lleva (MCP, editor).
    if (f.fija) emit ubicar(f.id, f.geometria);
}

void VentanasModel::setPantallaCompleta(int id, bool si) {
    const int r = filaDe(id);
    if (r < 0 || filas_[static_cast<std::size_t>(r)].pantallaCompleta == si) return;
    filas_[static_cast<std::size_t>(r)].pantallaCompleta = si;
    tocada(r, {PantallaCompletaRole});
    if (filas_[static_cast<std::size_t>(r)].fija) emit ubicar(filas_[static_cast<std::size_t>(r)].id, filas_[static_cast<std::size_t>(r)].geometria);
}

void VentanasModel::setGeometria(int id, int x, int y, int w, int h) {
    const int r = filaDe(id);
    const QRect g(x, y, w, h);
    if (r < 0 || w <= 0 || h <= 0 || filas_[static_cast<std::size_t>(r)].geometria == g) return;
    filas_[static_cast<std::size_t>(r)].geometria = g;
    guardar();
}

QVariantMap VentanasModel::ventana(int id) const {
    const int r = filaDe(id);
    if (r < 0) return {};
    QVariantMap m;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        m.insert(QString::fromUtf8(it.value()), data(index(r), it.key()));
    return m;
}

int VentanasModel::filaDe(int id) const {
    for (std::size_t i = 0; i < filas_.size(); ++i)
        if (filas_[i].id == id) return static_cast<int>(i);
    return -1;
}

QString VentanasModel::titulo(const FilaVentana& f) const {
    QStringList t;
    for (const QString& c : f.cosas) t << (catalogo_ != nullptr ? catalogo_->tituloDe(c) : c);
    return t.join(QStringLiteral(" + "));
}

void VentanasModel::tocada(int fila, const QList<int>& roles) {
    emit dataChanged(index(fila), index(fila), roles);
    guardar();
}

// --- Persistencia ----------------------------------------------------------------------

void VentanasModel::cargar() {
    if (archivo_.empty()) return;
    const QJsonObject o = config::leerQObjeto(archivo_);
    for (const QJsonValue& v : o.value("ventanas").toArray()) {
        const QJsonObject w = v.toObject();
        FilaVentana fila;
        for (const QJsonValue& c : w.value("cosas").toArray())
            if (!c.toString().isEmpty()) fila.cosas << c.toString();
        if (fila.cosas.isEmpty()) continue;
        fila.distribucion = distribucionValida(w.value("distribucion").toString());
        fila.monitor = w.value("monitor").toString();
        fila.sistema = w.value("sistema").toString();
        fila.pantallaCompleta = w.value("pantallaCompleta").toBool();
        fila.nombre = w.value("nombre").toString();
        fila.fija = w.value("fija").toBool();
        fila.encima = w.value("encima").toBool();
        if (w.contains("ancho"))
            fila.geometria = QRect(w.value("x").toInt(), w.value("y").toInt(), w.value("ancho").toInt(),
                                   w.value("alto").toInt());
        fila.id = proximo_++;
        filas_.push_back(fila);
    }
}

void VentanasModel::guardar() const {
    if (archivo_.empty()) return;
    QJsonArray vs;
    for (const FilaVentana& f : filas_) {
        QJsonObject w{{"cosas", QJsonArray::fromStringList(f.cosas)},
                      {"distribucion", f.distribucion},
                      {"pantallaCompleta", f.pantallaCompleta}};
        if (!f.monitor.isEmpty()) w["monitor"] = f.monitor;
        if (!f.sistema.isEmpty()) w["sistema"] = f.sistema;
        if (!f.nombre.isEmpty()) w["nombre"] = f.nombre;
        if (f.fija) w["fija"] = true;
        if (f.encima) w["encima"] = true;
        if (f.geometria.width() > 0 && f.geometria.height() > 0) {
            w["x"] = f.geometria.x();
            w["y"] = f.geometria.y();
            w["ancho"] = f.geometria.width();
            w["alto"] = f.geometria.height();
        }
        vs.append(w);
    }
    config::guardarQ(archivo_, QJsonObject{{"ventanas", vs}});
}

} // namespace ba::app
