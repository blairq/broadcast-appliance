// ============================================================================
// src/app/ui/MediosViewModel.cpp — ver MediosViewModel.hpp
// ============================================================================
#include "ui/MediosViewModel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

#include "arranque/AppRuntime.hpp"

namespace ba::app {

MediosViewModel::MediosViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) { refrescar(); }

MediosViewModel::~MediosViewModel() = default;

QString MediosViewModel::raiz() const { return QString::fromStdWString(rt_.dataRoot().wstring()); }

QString MediosViewModel::carpetaDeMedios() const {
    return QDir(QString::fromStdWString(rt_.dataRoot().wstring())).absoluteFilePath("Media");
}

void MediosViewModel::refrescar() {
    todos_ = medios::listar(raiz());
    carpetas_ = medios::arbol(raiz(), todos_);
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

void MediosViewModel::recalcular() {
    visibles_ = medios::filtrar(todos_, consulta_);
    if (!tipo_.isEmpty()) {
        QVector<medios::Archivo> out;
        for (const medios::Archivo& a : visibles_)
            if (QString::fromUtf8(medios::nombreDeTipo(a.tipo)) == tipo_) out.push_back(a);
        visibles_ = out;
    }
    if (!carpeta_.isEmpty()) {
        QVector<medios::Archivo> out;
        for (const medios::Archivo& a : visibles_) {
            const int corte = a.relativa.lastIndexOf('/');
            const QString suya = corte < 0 ? QString() : a.relativa.left(corte);
            const bool adentro =
                conSub_ ? (suya == carpeta_ || suya.startsWith(carpeta_ + "/")) : suya == carpeta_;
            if (adentro) out.push_back(a);
        }
        visibles_ = out;
    }
    // BUSCAR MANDA SOBRE LA CARPETA, y por eso el filtro se aplica al final:
    // si alguien escribe y no aparece nada, lo primero que piensa es que el
    // archivo no esta, no que lo tapo la carpeta elegida. Ver el cartel de la
    // grilla vacia, que lo dice.
    medios::ordenar(visibles_, orden_, ordenDesc_);
    emit cambio();
}

QVariantList MediosViewModel::archivos() const {
    QVariantList out;
    out.reserve(visibles_.size());
    for (const medios::Archivo& a : visibles_)
        out.append(QVariantMap{{"ruta", a.ruta},
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
                               {"desarmable", medios::sePuedeDesarmar(a.nombre)}});
    return out;
}

QString MediosViewModel::consulta() const { return consulta_; }

void MediosViewModel::setConsulta(const QString& q) {
    if (q == consulta_) return;
    consulta_ = q;
    // Sin volver al disco: la lista ya esta en memoria y filtrar es comparar
    // texto. Es lo que hace que escribir rapido no congele nada.
    recalcular();
}

QString MediosViewModel::tipo() const { return tipo_; }

void MediosViewModel::setTipo(const QString& t) {
    if (t == tipo_) return;
    tipo_ = t;
    recalcular();
}

QVariantList MediosViewModel::arbol() const {
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
    recalcular();
}

bool MediosViewModel::conSubcarpetas() const { return conSub_; }

void MediosViewModel::setConSubcarpetas(bool v) {
    if (v == conSub_) return;
    conSub_ = v;
    recalcular();
}

QString MediosViewModel::orden() const { return orden_; }

void MediosViewModel::setOrden(const QString& o) {
    if (o == orden_) return;
    orden_ = o;
    recalcular();
}

bool MediosViewModel::ordenDescendente() const { return ordenDesc_; }

void MediosViewModel::setOrdenDescendente(bool v) {
    if (v == ordenDesc_) return;
    ordenDesc_ = v;
    recalcular();
}

void MediosViewModel::plegar(const QString& relativa) {
    if (cerradas_.contains(relativa))
        cerradas_.remove(relativa);
    else
        cerradas_.insert(relativa);
    emit cambio();
}

void MediosViewModel::abrirHasta(const QString& relativa) {
    // Abre todas las madres: es lo que hace falta despues de mover algo a una
    // carpeta profunda, para que se vea a donde fue.
    QString acumulada;
    for (const QString& parte : relativa.split('/', Qt::SkipEmptyParts)) {
        acumulada = acumulada.isEmpty() ? parte : acumulada + "/" + parte;
        cerradas_.remove(acumulada);
    }
    emit cambio();
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
        for (int i = 0; i < visibles_.size(); ++i) {
            if (visibles_[i].relativa == ancla_) desde = i;
            if (visibles_[i].relativa == relativa) hasta = i;
        }
        if (desde < 0 || hasta < 0) {
            seleccion_ = {relativa};
            ancla_ = relativa;
        } else {
            if (desde > hasta) std::swap(desde, hasta);
            for (int i = desde; i <= hasta; ++i)
                if (!seleccion_.contains(visibles_[i].relativa)) seleccion_ << visibles_[i].relativa;
        }
    } else {
        seleccion_ = {relativa};
        ancla_ = relativa;
    }
    emit seleccionCambio();
}

void MediosViewModel::elegirTodo() {
    seleccion_.clear();
    for (const medios::Archivo& a : visibles_)
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
int MediosViewModel::mostrados() const { return visibles_.size(); }

QString MediosViewModel::rutaAbsoluta(const QString& relativa) const {
    return medios::resolverAdentro(raiz(), relativa);
}

bool MediosViewModel::sePuedeDesarmar(const QString& ruta) const { return medios::sePuedeDesarmar(ruta); }

} // namespace ba::app
