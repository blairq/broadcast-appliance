// src/app/dispositivos/Drivers.cpp — ver Drivers.hpp

#include "dispositivos/Drivers.hpp"

#include <QDebug>
#include <QPointer>

#include <algorithm>

namespace ba::app::dispositivos {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
bool esGst(const std::string& id) { return id.rfind("gst:", 0) == 0; }
int ordenDeTipo(const std::string& t) {
    if (t == "gstreamer") return 0;
    if (t == "asio") return 1;
    return 2;
}
} // namespace

Drivers::Drivers(Opciones o, QObject* parent)
    : QAbstractListModel(parent), o_(std::move(o)), salud_(o_.archivo, o_.huella, o_.etiqueta),
      sondeo_(o_.sondeo, this) {
    salud_.setUmbral(o_.umbral);
    conocidaAlArrancar_ = salud_.maquinaConocida();
    connect(&sondeo_, &Sondeo::progreso, this, [this](int hechos, int total, const QString&) {
        hechos_ = hechos;
        total_ = total;
        emit estadoCambio();
    });
    refrescar();
}

QString Drivers::maquina() const {
    const QString h = qs(o_.huella);
    return o_.etiqueta.empty() ? h : qs(o_.etiqueta) + " · " + h.left(6);
}

void Drivers::listarY(std::function<void(const std::vector<hw::DriverListado>&)> despues) {
    revisando_ = true;
    emit estadoCambio();
    QPointer<Drivers> yo(this);
    sondeo_.listar([yo, despues](bool ok, std::vector<hw::DriverListado> lista, QString error) {
        if (!yo) return;
        if (!ok) {
            // Sin lista no se prueba nada, pero tampoco se frena a nadie: lo que anduvo
            // sigue andando y lo de cuarentena sigue afuera.
            qWarning().noquote() << "drivers: no se pudo listar:" << error;
            yo->probarCola(); // lo que se haya pedido a mano (Probar de nuevo) igual se prueba
            return;
        }
        yo->listados_ = static_cast<int>(lista.size());
        despues(lista);
    });
}

void Drivers::arrancar() {
    // Lo que está en cuarentena, afuera también en este proceso (GstEntorno ya lo
    // hizo antes de gst_init; esto cubre un GStreamer que arrancó con otro archivo).
    if (o_.ponerRango)
        for (const auto& e : salud_.todos())
            if (esGst(e.id) && e.estado == "cuarentena") o_.ponerRango(e.id.substr(4), 0);
    completo_ = !salud_.maquinaConocida();
    listarY([this](const std::vector<hw::DriverListado>& lista) {
        for (const std::string& id : salud_.novedades(lista)) cola_ << qs(id);
        refrescar();
        probarCola();
    });
}

void Drivers::revisarTodo() {
    if (revisando_) return;
    listarY([this](const std::vector<hw::DriverListado>& lista) {
        for (const std::string& id : salud_.novedades(lista)) cola_ << qs(id);
        for (const auto& e : salud_.todos())
            if (e.presente && e.estado == "anda" && !cola_.contains(qs(e.id))) cola_ << qs(e.id);
        refrescar();
        probarCola();
    });
}

void Drivers::sospechar(const QString& porque) {
    qWarning().noquote() << "drivers: se prueban aparte los proveedores de GStreamer:" << porque;
    for (const auto& e : salud_.todos())
        if (esGst(e.id) && e.presente && e.estado != "cuarentena" && !cola_.contains(qs(e.id))) cola_ << qs(e.id);
    revisando_ = true;
    refrescar();
    probarCola();
}

QString Drivers::reintentar(const QString& id) {
    const std::string err = salud_.reintentar(id.toStdString());
    if (!err.empty()) return qs(err);
    if (!cola_.contains(id) && !probando_.contains(id)) cola_ << id;
    revisando_ = true;
    refrescar();
    probarCola();
    return {};
}

void Drivers::probarCola() {
    if (sondeo_.ocupado()) return; // al terminar la ronda en curso se sigue con la cola
    QStringList ids;
    for (const QString& id : cola_)
        if (salud_.sePuedeCargar(id.toStdString()) && !ids.contains(id)) ids << id;
    cola_.clear();
    if (ids.isEmpty()) return terminoRonda();
    probando_ = QSet<QString>(ids.begin(), ids.end());
    revisando_ = true;
    hechos_ = 0;
    total_ = static_cast<int>(ids.size());
    refrescar();
    QPointer<Drivers> yo(this);
    sondeo_.probar(
        ids, [yo](const ResultadoSonda& r) { if (yo) yo->alResultado(r); },
        [yo](QString error) {
            if (!yo) return;
            if (!error.isEmpty()) qWarning().noquote() << "drivers:" << error;
            yo->probando_.clear();
            yo->refrescar();
            yo->probarCola(); // lo que se pidió mientras tanto
        });
}

void Drivers::alResultado(const ResultadoSonda& r) {
    const auto antes = salud_.uno(r.id);
    const bool estaba = antes && antes->estado == "cuarentena";
    const QString nombre = antes ? qs(antes->nombre) : qs(r.id);
    if (r.ausente) {
        salud_.ausente(r.id);
    } else if (r.ok) {
        salud_.exito(r.id, r.detalle, ahora().toStdString());
        if (estaba && esGst(r.id) && o_.ponerRango) o_.ponerRango(r.id.substr(4), antes->rango);
        qInfo().noquote() << "drivers:" << nombre << "anda:" << qs(r.detalle);
    } else {
        const bool quedo = salud_.falla(r.id, r.falla, r.motivo, ahora().toStdString());
        qWarning().noquote() << QString("drivers: %1 falló (%2): %3").arg(nombre, hw::nombreFalla(r.falla), qs(r.motivo));
        if (quedo) {
            if (esGst(r.id) && o_.ponerRango) o_.ponerRango(r.id.substr(4), 0);
            qWarning().noquote() << "drivers:" << nombre << "queda en cuarentena: no se vuelve a cargar solo";
            emit cuarentenaNueva(qs(r.id), nombre, qs(r.motivo));
        }
    }
    probando_.remove(qs(r.id));
    refrescar();
}

void Drivers::terminoRonda() {
    if (sondeo_.ocupado() || !cola_.isEmpty()) return;
    // El primer sondeo completo de una máquina nueva la vuelve conocida (si hubo algo
    // que listar: un GStreamer sin registro todavía no lista nada y no cuenta).
    if (completo_ && listados_ > 0) {
        salud_.sondeoCompleto(ahora().toStdString());
        completo_ = false;
    }
    revisando_ = false;
    const bool primera = !listo_;
    listo_ = true;
    refrescar();
    if (primera) emit listoArranque();
}

QString Drivers::resumen() const {
    const int andan = salud_.cuantosAndan();
    const int q = salud_.cuantosEnCuarentena();
    if (revisando_) {
        const QString k = total_ > 0 ? QString(" (%1 de %2)").arg(std::min(hechos_ + 1, total_)).arg(total_) : QString();
        if (!conocidaAlArrancar_) return "Máquina nueva: revisando todo" + k + "…";
        return QString("Máquina conocida: %1 drivers listos, revisando novedades%2…").arg(andan).arg(k);
    }
    if (!listo_) return "Todavía no se revisaron los drivers";
    QString s = QString("%1 %2").arg(andan).arg(andan == 1 ? "driver anda" : "drivers andan");
    if (q > 0) s += QString(", %1 en cuarentena").arg(q);
    return s;
}

QVariantMap Drivers::fila(const hw::EstadoDriver& e) const {
    const QString id = qs(e.id);
    QString estado = qs(e.estado);
    if (!e.presente) estado = "ausente";
    if (probando_.contains(id) || cola_.contains(id)) estado = "probando";
    QString texto;
    if (estado == "probando")
        texto = "probando…";
    else if (estado == "ausente")
        texto = "no está instalado ahora";
    else if (estado == "anda")
        texto = e.detalle.empty() ? QString("anda") : "anda · " + qs(e.detalle);
    else if (estado == "falla")
        texto = QString("falló %1 de %2: %3").arg(e.fallas).arg(salud_.umbral()).arg(qs(e.motivo));
    else if (estado == "cuarentena")
        texto = QString("en cuarentena (%1 fallas): %2").arg(e.fallas).arg(qs(e.motivo));
    else
        texto = "sin probar";
    return {{"id", id},
            {"nombre", qs(e.nombre)},
            {"tipo", qs(e.tipo)},
            {"estado", estado},
            {"fallas", e.fallas},
            {"fallas_total", e.fallasTotal},
            {"ultima_falla", qs(e.ultimaFalla)},
            {"motivo", qs(e.motivo)},
            {"detalle", qs(e.detalle)},
            {"cuando", qs(e.cuando)},
            {"ultimo_ok", qs(e.ultimoOk)},
            {"texto", texto}};
}

void Drivers::refrescar() {
    std::vector<hw::EstadoDriver> nuevas = salud_.todos();
    std::stable_sort(nuevas.begin(), nuevas.end(), [](const hw::EstadoDriver& a, const hw::EstadoDriver& b) {
        const int ta = ordenDeTipo(a.tipo), tb = ordenDeTipo(b.tipo);
        if (ta != tb) return ta < tb;
        return QString::compare(qs(a.nombre), qs(b.nombre), Qt::CaseInsensitive) < 0;
    });
    std::vector<QVariantMap> vistas;
    vistas.reserve(nuevas.size());
    for (const auto& e : nuevas) vistas.push_back(fila(e));
    bool mismas = nuevas.size() == filas_.size();
    for (std::size_t i = 0; mismas && i < nuevas.size(); ++i) mismas = nuevas[i].id == filas_[i].id;
    if (!mismas) {
        beginResetModel();
        filas_ = std::move(nuevas);
        vistas_ = std::move(vistas);
        endResetModel();
    } else {
        // Las mismas filas: se avisa solo la que cambió (la lista no se rehace ni salta).
        filas_ = std::move(nuevas);
        for (std::size_t i = 0; i < filas_.size(); ++i) {
            if (vistas[i] == vistas_[i]) continue;
            vistas_[i] = vistas[i];
            const QModelIndex x = index(static_cast<int>(i));
            emit dataChanged(x, x);
        }
    }
    emit estadoCambio();
}

int Drivers::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(filas_.size());
}

QVariant Drivers::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(filas_.size())) return {};
    const QVariantMap& f = vistas_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRol: return f.value("id");
        case NombreRol: return f.value("nombre");
        case TipoRol: return f.value("tipo");
        case EstadoRol: return f.value("estado");
        case FallasRol: return f.value("fallas");
        case MotivoRol: return f.value("motivo");
        case DetalleRol: return f.value("detalle");
        case CuandoRol: return f.value("cuando");
        case TextoRol: return f.value("texto");
        default: return {};
    }
}

QHash<int, QByteArray> Drivers::roleNames() const {
    return {{IdRol, "id"},         {NombreRol, "nombre"}, {TipoRol, "tipo"},   {EstadoRol, "estado"},
            {FallasRol, "fallas"}, {MotivoRol, "motivo"}, {DetalleRol, "detalle"}, {CuandoRol, "cuando"},
            {TextoRol, "texto"}};
}

QVariantList Drivers::lista() const {
    QVariantList out;
    for (const auto& v : vistas_) out << v;
    return out;
}

QVariantMap Drivers::uno(const QString& id) const {
    const auto e = salud_.uno(id.toStdString());
    return e ? fila(*e) : QVariantMap{};
}

} // namespace ba::app::dispositivos
