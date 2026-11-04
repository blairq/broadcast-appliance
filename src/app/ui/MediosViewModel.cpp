// ============================================================================
// src/app/ui/MediosViewModel.cpp — ver MediosViewModel.hpp
// ============================================================================
#include "ui/MediosViewModel.hpp"

#include "diag/Acoples.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUrl>

#include <algorithm>

#include "arranque/RuntimeConfig.hpp"

namespace ba::app {

// ---- ListaMedios ----

QVariantMap ListaMedios::aMapa(const medios::Archivo& a) {
    return {{"ruta", a.ruta},
            {"relativa", a.relativa},
            {"nombre", a.nombre},
            {"carpeta", a.carpeta},
            {"tipo", QString::fromUtf8(medios::nombreDeTipo(a.tipo))},
            {"bytes", a.bytes},
            {"modificadoMs", a.modificadoMs},
            // La vista previa se dibuja desde el disco: es una URL de archivo,
            // no una copia en memoria. Con doscientas imagenes, copiarlas seria
            // llenar la memoria con lo que el disco ya tiene.
            {"url", QUrl::fromLocalFile(a.ruta).toString()},
            {"desarmable", medios::sePuedeDesarmar(a.nombre)}};
}

QVariant ListaMedios::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const medios::Archivo& a = filas_[index.row()];
    switch (role) {
        case RutaRole: return a.ruta;
        case RelativaRole: return a.relativa;
        case NombreRole: return a.nombre;
        case CarpetaRole: return a.carpeta;
        case TipoRole: return QString::fromUtf8(medios::nombreDeTipo(a.tipo));
        case BytesRole: return a.bytes;
        case ModificadoRole: return a.modificadoMs;
        case UrlRole: return QUrl::fromLocalFile(a.ruta).toString();
        case DesarmableRole: return medios::sePuedeDesarmar(a.nombre);
        case FilaRole: return aMapa(a);
        default: return {};
    }
}

QHash<int, QByteArray> ListaMedios::roleNames() const {
    return {{RutaRole, "ruta"},       {RelativaRole, "relativa"},     {NombreRole, "nombre"},
            {CarpetaRole, "carpeta"}, {TipoRole, "tipo"},             {BytesRole, "bytes"},
            {ModificadoRole, "modificadoMs"}, {UrlRole, "url"},       {DesarmableRole, "desarmable"},
            {FilaRole, "fila"}};
}

QVariantMap ListaMedios::fila(int i) const {
    BA_ACOPLE("invocable", "ListaMedios::fila");
    if (i < 0 || i >= rowCount()) return {};
    return aMapa(filas_[i]);
}

int ListaMedios::indiceDe(const QString& relativa) const {
    for (int i = 0; i < filas_.size(); ++i)
        if (filas_[i].relativa == relativa) return i;
    return -1;
}

void ListaMedios::poner(QVector<medios::Archivo> nuevas, bool mismoOrden) {
    const int antes = rowCount();
    if (mismoOrden) {
        // Lo que queda tiene que venir en el mismo orden en las dos (al volver a mirar el
        // disco, un archivo que cambió de tamaño puede cambiar de lugar): si no, se rehace.
        QHash<QString, int> dondeNueva;
        for (int i = 0; i < nuevas.size(); ++i) dondeNueva.insert(nuevas[i].relativa, i);
        int ultima = -1;
        for (const medios::Archivo& a : filas_) {
            const auto it = dondeNueva.constFind(a.relativa);
            if (it == dondeNueva.constEnd()) continue;
            if (*it < ultima) {
                mismoOrden = false;
                break;
            }
            ultima = *it;
        }
    }
    if (!mismoOrden) {
        beginResetModel();
        filas_ = std::move(nuevas);
        endResetModel();
        if (antes != rowCount()) emit countChanged();
        return;
    }
    // Las dos son partes de la misma lista ordenada: primero se sacan las que ya no
    // están (de a tramos, de atrás para adelante) y después se ponen las nuevas en su
    // lugar. Lo que sigue estando no se toca (salvo que haya cambiado en el disco).
    QSet<QString> quedan;
    for (const medios::Archivo& a : nuevas) quedan.insert(a.relativa);
    for (int i = filas_.size() - 1; i >= 0;) {
        if (quedan.contains(filas_[i].relativa)) {
            --i;
            continue;
        }
        int desde = i;
        while (desde > 0 && !quedan.contains(filas_[desde - 1].relativa)) --desde;
        beginRemoveRows({}, desde, i);
        filas_.remove(desde, i - desde + 1);
        endRemoveRows();
        i = desde - 1;
    }
    for (int i = 0; i < nuevas.size(); ++i) {
        const medios::Archivo& n = nuevas[i];
        if (i < filas_.size() && filas_[i].relativa == n.relativa) {
            medios::Archivo& a = filas_[i];
            if (a.bytes != n.bytes || a.modificadoMs != n.modificadoMs || a.ruta != n.ruta || a.tipo != n.tipo) {
                a = n;
                const QModelIndex ix = index(i, 0);
                emit dataChanged(ix, ix);
            }
            continue;
        }
        beginInsertRows({}, i, i);
        filas_.insert(i, n);
        endInsertRows();
    }
    if (antes != rowCount()) emit countChanged();
}

// ---- MediosViewModel ----

MediosViewModel::MediosViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) { refrescar(); }

MediosViewModel::~MediosViewModel() = default;

QString MediosViewModel::raiz() const { return QString::fromStdWString(rt_.dataRoot().wstring()); }

QString MediosViewModel::carpetaDeMedios() const {
    return QDir(QString::fromStdWString(rt_.dataRoot().wstring())).absoluteFilePath("Media");
}

void MediosViewModel::refrescar() {
    const int totalAntes = todos_.size();
    const QVariantMap cuentaAntes = cuenta();
    todos_ = medios::listar(raiz());
    carpetas_ = medios::arbol(raiz(), todos_);
    if (todos_.size() != totalAntes || cuenta() != cuentaAntes) emit discoChanged();
    revisarArbol();
    // Lo que ya no existe deja de estar elegido: mover cuarenta archivos y que
    // la seleccion siga apuntando a donde estaban es la forma de que la
    // siguiente accion haga algo que nadie pidio.
    QStringList vivas;
    for (const QString& r : seleccion_)
        if (QFileInfo::exists(medios::resolverAdentro(raiz(), r))) vivas << r;
    if (vivas.size() != seleccion_.size()) {
        seleccion_ = vivas;
        emit seleccionCambio();
    }
    recalcular();
}

void MediosViewModel::recalcular(bool mismoOrden) {
    QVector<medios::Archivo> visibles = medios::filtrar(todos_, consulta_);
    if (!tipo_.isEmpty()) {
        QVector<medios::Archivo> out;
        for (const medios::Archivo& a : visibles)
            if (QString::fromUtf8(medios::nombreDeTipo(a.tipo)) == tipo_) out.push_back(a);
        visibles = out;
    }
    if (!carpeta_.isEmpty()) {
        QVector<medios::Archivo> out;
        for (const medios::Archivo& a : visibles) {
            const int corte = a.relativa.lastIndexOf('/');
            const QString suya = corte < 0 ? QString() : a.relativa.left(corte);
            const bool adentro =
                conSub_ ? (suya == carpeta_ || suya.startsWith(carpeta_ + "/")) : suya == carpeta_;
            if (adentro) out.push_back(a);
        }
        visibles = out;
    }
    // BUSCAR MANDA SOBRE LA CARPETA, y por eso el filtro se aplica al final:
    // si alguien escribe y no aparece nada, lo primero que piensa es que el
    // archivo no esta, no que lo tapo la carpeta elegida. Ver el cartel de la
    // grilla vacia, que lo dice.
    medios::ordenar(visibles, orden_, ordenDesc_);
    const int antes = lista_.count();
    lista_.poner(std::move(visibles), mismoOrden);
    if (lista_.count() != antes) emit mostradosChanged();
}

QString MediosViewModel::consulta() const { return consulta_; }

void MediosViewModel::setConsulta(const QString& q) {
    if (q == consulta_) return;
    consulta_ = q;
    emit consultaChanged();
    // Sin volver al disco: la lista ya esta en memoria y filtrar es comparar
    // texto. Es lo que hace que escribir rapido no congele nada.
    recalcular();
}

QString MediosViewModel::tipo() const { return tipo_; }

void MediosViewModel::setTipo(const QString& t) {
    if (t == tipo_) return;
    tipo_ = t;
    emit tipoChanged();
    recalcular();
}

QVariantList MediosViewModel::arbol() const { return arbol_; }

void MediosViewModel::revisarArbol() {
    if (QVariantList a = armarArbol(); a != arbol_) {
        arbol_ = std::move(a);
        emit arbolChanged();
    }
}

QVariantList MediosViewModel::armarArbol() const {
    QVariantList out;
    for (const medios::Carpeta& c : carpetas_) {
        // Visible = ninguna de sus madres esta cerrada. Se resuelve aca, una
        // vez, y no en cada fila de la vista.
        bool visible = true;
        for (const QString& cerrada : cerradas_)
            if (c.relativa.startsWith(cerrada + "/")) {
                visible = false;
                break;
            }
        out.append(QVariantMap{{"relativa", c.relativa},
                               {"nombre", c.nombre},
                               {"nivel", c.nivel},
                               {"propios", c.propios},
                               {"total", c.total},
                               {"raiz", c.raiz},
                               {"abierta", !cerradas_.contains(c.relativa)},
                               {"visible", visible},
                               {"elegida", c.relativa == carpeta_}});
    }
    return out;
}

QString MediosViewModel::carpeta() const { return carpeta_; }

void MediosViewModel::setCarpeta(const QString& c) {
    if (c == carpeta_) return;
    carpeta_ = c;
    emit carpetaChanged();
    revisarArbol(); // la elegida
    recalcular();
}

bool MediosViewModel::conSubcarpetas() const { return conSub_; }

void MediosViewModel::setConSubcarpetas(bool v) {
    if (v == conSub_) return;
    conSub_ = v;
    emit conSubcarpetasChanged();
    recalcular();
}

QString MediosViewModel::orden() const { return orden_; }

void MediosViewModel::setOrden(const QString& o) {
    if (o == orden_) return;
    orden_ = o;
    emit ordenChanged();
    recalcular(false);
}

bool MediosViewModel::ordenDescendente() const { return ordenDesc_; }

void MediosViewModel::setOrdenDescendente(bool v) {
    if (v == ordenDesc_) return;
    ordenDesc_ = v;
    emit ordenChanged();
    recalcular(false);
}

void MediosViewModel::plegar(const QString& relativa) {
    if (cerradas_.contains(relativa))
        cerradas_.remove(relativa);
    else
        cerradas_.insert(relativa);
    revisarArbol();
}

void MediosViewModel::abrirHasta(const QString& relativa) {
    // Abre todas las madres: es lo que hace falta despues de mover algo a una
    // carpeta profunda, para que se vea a donde fue.
    QString acumulada;
    for (const QString& parte : relativa.split('/', Qt::SkipEmptyParts)) {
        acumulada = acumulada.isEmpty() ? parte : acumulada + "/" + parte;
        cerradas_.remove(acumulada);
    }
    revisarArbol();
}

QStringList MediosViewModel::seleccion() const { return seleccion_; }
int MediosViewModel::cuantosElegidos() const { return seleccion_.size(); }

qint64 MediosViewModel::bytesElegidos() const {
    qint64 n = 0;
    for (const medios::Archivo& a : todos_)
        if (seleccion_.contains(a.relativa)) n += a.bytes;
    return n;
}

bool MediosViewModel::estaElegido(const QString& relativa) const { return seleccion_.contains(relativa); }

void MediosViewModel::elegir(const QString& relativa, const QString& modo) {
    if (modo == "sumar") {
        if (seleccion_.contains(relativa))
            seleccion_.removeAll(relativa);
        else
            seleccion_ << relativa;
        ancla_ = relativa;
    } else if (modo == "hasta" && !ancla_.isEmpty()) {
        // El rango es sobre lo que se VE, en el orden en que se ve: con otro
        // orden puesto, un rango sobre la lista de disco elegiria cualquier
        // cosa.
        int desde = -1, hasta = -1;
        const QVector<medios::Archivo>& visibles = lista_.filas();
        for (int i = 0; i < visibles.size(); ++i) {
            if (visibles[i].relativa == ancla_) desde = i;
            if (visibles[i].relativa == relativa) hasta = i;
        }
        if (desde < 0 || hasta < 0) {
            seleccion_ = {relativa};
            ancla_ = relativa;
        } else {
            if (desde > hasta) std::swap(desde, hasta);
            for (int i = desde; i <= hasta; ++i)
                if (!seleccion_.contains(visibles[i].relativa)) seleccion_ << visibles[i].relativa;
        }
    } else {
        seleccion_ = {relativa};
        ancla_ = relativa;
    }
    emit seleccionCambio();
}

void MediosViewModel::elegirTodo() {
    seleccion_.clear();
    for (const medios::Archivo& a : lista_.filas())
        seleccion_ << a.relativa;
    emit seleccionCambio();
}

void MediosViewModel::noElegirNada() {
    if (seleccion_.isEmpty()) return;
    seleccion_.clear();
    ancla_.clear();
    emit seleccionCambio();
}

int MediosViewModel::moverElegidos(const QString& destino) {
    int n = 0;
    QString ultimoError;
    for (const QString& r : seleccion_) {
        QString e;
        if (medios::mover(raiz(), r, destino, &e))
            ++n;
        else
            ultimoError = e;
    }
    // Se avisa SIEMPRE, aunque haya salido bien: mover cuarenta archivos sin
    // que pase nada visible deja a cualquiera dudando de si hizo algo.
    if (n > 0) {
        emit notify(tr("%n archivo a «%1»", "", n).arg(destino), "ok");
        abrirHasta(destino);
    }
    if (!ultimoError.isEmpty()) emit notify(tr("No se pudo mover todo: %1").arg(ultimoError), "error");
    noElegirNada();
    refrescar();
    return n;
}

bool MediosViewModel::nuevaCarpeta(const QString& padre, const QString& nombre) {
    QString e;
    if (!medios::crearCarpeta(raiz(), padre.isEmpty() ? QString("Media") : padre, nombre, &e)) {
        emit notify(tr("No se pudo crear la carpeta: %1").arg(e), "error");
        return false;
    }
    refrescar();
    abrirHasta(padre);
    return true;
}

bool MediosViewModel::renombrar(const QString& relativa, const QString& nombre) {
    QString e;
    if (!medios::renombrar(raiz(), relativa, nombre, &e)) {
        emit notify(tr("No se pudo renombrar: %1").arg(e), "error");
        return false;
    }
    refrescar();
    return true;
}

int MediosViewModel::borrarElegidos() {
    int n = 0;
    QString ultimoError;
    for (const QString& r : seleccion_) {
        QString e;
        if (medios::aPapelera(raiz(), r, &e))
            ++n;
        else
            ultimoError = e;
    }
    if (n > 0) emit notify(tr("%1 a la papelera. Están en Papelera/, no se perdieron.").arg(n), "ok");
    if (!ultimoError.isEmpty()) emit notify(tr("No se pudo borrar todo: %1").arg(ultimoError), "error");
    noElegirNada();
    refrescar();
    return n;
}

QVariantMap MediosViewModel::cuenta() const {
    QVariantMap c{{"imagen", 0}, {"video", 0}, {"audio", 0}, {"documento", 0}};
    for (const medios::Archivo& a : todos_) {
        const QString k = QString::fromUtf8(medios::nombreDeTipo(a.tipo));
        c[k] = c.value(k).toInt() + 1;
    }
    return c;
}

int MediosViewModel::total() const { return todos_.size(); }
int MediosViewModel::mostrados() const { return lista_.count(); }

QString MediosViewModel::rutaAbsoluta(const QString& relativa) const {
    return medios::resolverAdentro(raiz(), relativa);
}

bool MediosViewModel::sePuedeDesarmar(const QString& ruta) const { return medios::sePuedeDesarmar(ruta); }

} // namespace ba::app

#include "moc_MediosViewModel.cpp"
