// ============================================================================
// src/app/ui/CarruselViewModel.cpp — ver CarruselViewModel.hpp
// ============================================================================
#include "ui/CarruselViewModel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "AppRuntime.hpp"
#include "carrusel/Carrusel.hpp"
#include "carrusel/CarruselStore.hpp"
#include "carrusel/Conversion.hpp"
#include "medios/Medios.hpp"

namespace ba::app {
namespace {

/// Un nombre de carrusel se vuelve un nombre de carpeta. Se sacan los
/// separadores y lo que Windows no acepta: un carrusel llamado «Apertura 3/4»
/// no puede terminar creando una carpeta «4» adentro de otra «Apertura 3».
QString comoCarpeta(const QString& nombre) {
    QString s;
    for (const QChar c : nombre)
        s += (c.isLetterOrNumber() || c == ' ' || c == '-' || c == '_') ? c : QChar('-');
    return s.simplified().isEmpty() ? QStringLiteral("carrusel") : s.simplified();
}

} // namespace

CarruselViewModel::CarruselViewModel(AppRuntime& rt, QObject* parent) : QObject(parent), rt_(rt) {
    const QDir raiz(QString::fromStdWString(rt_.dataRoot().wstring()));
    store_ = std::make_unique<carrusel::Store>(
        std::filesystem::path(raiz.absoluteFilePath("Config/carruseles.json").toStdWString()));
    if (!store_->error().empty())
        emit notify(QString::fromStdString("Carruseles: " + store_->error()), "error");

    conv_ = std::make_unique<carrusel::Conversor>(this);
    connect(conv_.get(), &carrusel::Conversor::avance, this,
            [this](QString etapa, int hecho, int total) {
                etapa_ = etapa;
                hecho_ = hecho;
                total_ = total;
                emit progreso();
            });
    connect(conv_.get(), &carrusel::Conversor::listo, this, [this](QStringList imagenes) {
        convirtiendo_ = false;
        emit progreso();
        carrusel::Carrusel c;
        c.setNombre(destinoConversion_.toStdString());
        for (const QString& p : imagenes) {
            carrusel::Lamina l;
            l.archivo = p.toStdString();
            c.agregar(l);
        }
        store_->guardar(c);
        actual_ = destinoConversion_;
        emit cambio();
        emit notify(QString("«%1»: %2 láminas listas").arg(destinoConversion_).arg(imagenes.size()),
                    "ok");
    });
    connect(conv_.get(), &carrusel::Conversor::fallo, this, [this](QString motivo) {
        convirtiendo_ = false;
        etapa_.clear();
        emit progreso();
        // Cancelar lo pidió el operador: no es una falla que haya que anunciar
        // en rojo como si algo se hubiera roto.
        emit notify(motivo == "cancelado" ? "Conversión cancelada" : "No se pudo desarmar: " + motivo,
                    motivo == "cancelado" ? "info" : "error");
    });
}

CarruselViewModel::~CarruselViewModel() = default;

QString CarruselViewModel::carpetaDe(const QString& nombre) const {
    const QDir raiz(QString::fromStdWString(rt_.dataRoot().wstring()));
    return raiz.absoluteFilePath("Media/Carruseles/" + comoCarpeta(nombre));
}

QVariantList CarruselViewModel::carruseles() const {
    QVariantList out;
    for (const carrusel::Carrusel& c : store_->todos()) {
        const QString tapa = c.laminas().empty()
                                 ? QString()
                                 : QUrl::fromLocalFile(QString::fromStdString(c.laminas().front().archivo))
                                       .toString();
        out.append(QVariantMap{{"nombre", QString::fromStdString(c.nombre())},
                               {"laminas", int(c.tamano())},
                               {"fuente", QString::fromStdString(c.fuente())},
                               {"tapa", tapa}});
    }
    return out;
}

QString CarruselViewModel::actual() const { return actual_; }

void CarruselViewModel::setActual(const QString& nombre) {
    if (nombre == actual_) return;
    actual_ = nombre;
    // El filtro es de las láminas del carrusel abierto: dejarlo puesto al
    // cambiar de carrusel mostraría una grilla medio vacía sin motivo visible.
    consulta_.clear();
    emit cambio();
}

QVariantList CarruselViewModel::laminas() const {
    QVariantList out;
    const carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    if (!c) return out;
    const QString q = consulta_.trimmed();
    int i = -1;
    for (const carrusel::Lamina& l : c->laminas()) {
        ++i;
        const QString archivo = QString::fromStdString(l.archivo);
        const QString titulo = l.titulo.empty() ? QFileInfo(archivo).completeBaseName()
                                                : QString::fromStdString(l.titulo);
        if (!q.isEmpty()) {
            // Se busca sobre el título Y las etiquetas, con el mismo aplanado
            // de tildes que la grilla de medios: «grafico» encuentra «gráfico».
            const QString heno = titulo + " " + QString::fromStdString(l.etiquetas);
            if (!medios::coincide(heno, q)) continue;
        }
        out.append(QVariantMap{{"indice", i},
                               {"archivo", archivo},
                               {"url", QUrl::fromLocalFile(archivo).toString()},
                               {"titulo", titulo},
                               {"etiquetas", QString::fromStdString(l.etiquetas)},
                               {"analizada", l.analizada},
                               {"actual", i == c->actual()}});
    }
    return out;
}

QString CarruselViewModel::consulta() const { return consulta_; }

void CarruselViewModel::setConsulta(const QString& q) {
    if (q == consulta_) return;
    consulta_ = q;
    emit cambio(); // en memoria, sin tocar el disco: filtra mientras se escribe
}

bool CarruselViewModel::convirtiendo() const { return convirtiendo_; }
QString CarruselViewModel::etapa() const { return etapa_; }
int CarruselViewModel::hecho() const { return hecho_; }
int CarruselViewModel::total() const { return total_; }

bool CarruselViewModel::crear(const QString& nombre) {
    const QString n = nombre.trimmed();
    if (n.isEmpty()) {
        emit notify("Poné un nombre: sin nombre no se lo puede volver a encontrar", "error");
        return false;
    }
    if (store_->porNombre(n.toStdString())) {
        emit notify("Ya hay un carrusel llamado «" + n + "»", "error");
        return false;
    }
    carrusel::Carrusel c;
    c.setNombre(n.toStdString());
    if (!store_->guardar(c)) return false;
    actual_ = n;
    emit cambio();
    return true;
}

bool CarruselViewModel::borrar(const QString& nombre) {
    // Las imágenes NO se borran: pueden estar en una escena, o ser archivos que
    // el operador trajo él. Se va el carrusel, no lo que apunta.
    if (!store_->borrar(nombre.toStdString())) return false;
    if (actual_ == nombre) actual_.clear();
    emit cambio();
    return true;
}

bool CarruselViewModel::renombrar(const QString& de, const QString& a) {
    const QString n = a.trimmed();
    if (n.isEmpty() || store_->porNombre(n.toStdString())) return false;
    carrusel::Carrusel* c = store_->porNombre(de.toStdString());
    if (!c) return false;
    carrusel::Carrusel copia = *c;
    copia.setNombre(n.toStdString());
    if (!store_->borrar(de.toStdString()) || !store_->guardar(copia)) return false;
    if (actual_ == de) actual_ = n;
    emit cambio();
    return true;
}

bool CarruselViewModel::agregar(const QString& archivo) {
    carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    if (!c) return false;
    carrusel::Lamina l;
    l.archivo = archivo.toStdString();
    if (!c->agregar(l)) {
        emit notify("Esa imagen ya estaba en el carrusel", "info");
        return false;
    }
    store_->persistir();
    emit cambio();
    return true;
}

bool CarruselViewModel::quitar(int i) {
    carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    if (!c || i < 0 || !c->quitar(std::size_t(i))) return false;
    store_->persistir();
    emit cambio();
    return true;
}

bool CarruselViewModel::mover(int de, int a) {
    carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    if (!c || de < 0 || a < 0 || !c->mover(std::size_t(de), std::size_t(a))) return false;
    store_->persistir();
    emit cambio();
    return true;
}

bool CarruselViewModel::ir(int i) {
    carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    if (!c || !c->ir(i)) return false;
    store_->persistir();
    emit cambio();
    return true;
}

int CarruselViewModel::posicion() const {
    const carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    return c ? c->actual() : -1;
}

bool CarruselViewModel::etiquetar(const QString& carrusel, int i, const QString& etiquetas) {
    carrusel::Carrusel* c = store_->porNombre(carrusel.toStdString());
    if (!c || i < 0 || std::size_t(i) >= c->tamano()) return false;
    if (!c->etiquetar(c->laminas()[std::size_t(i)].archivo, etiquetas.toStdString())) return false;
    store_->persistir();
    emit cambio();
    return true;
}

bool CarruselViewModel::sePuedeDesarmar(const QString& archivo) const {
    if (!carrusel::Conversor::esDocumento(archivo)) return false;
    return !carrusel::Conversor::necesitaOffice(archivo) || !carrusel::Conversor::soffice().isEmpty();
}

void CarruselViewModel::desarmar(const QString& documento, const QString& nombre) {
    if (convirtiendo_) {
        emit notify("Esperá: ya se está desarmando otro", "info");
        return;
    }
    QString n = nombre.trimmed();
    if (n.isEmpty()) n = QFileInfo(documento).completeBaseName();
    // Un nombre repetido se numera en vez de pisar: traer dos veces la misma
    // presentación corregida es lo normal, y perder la anterior no.
    if (store_->porNombre(n.toStdString())) {
        int i = 2;
        while (store_->porNombre((n + " (" + QString::number(i) + ")").toStdString())) ++i;
        n = n + " (" + QString::number(i) + ")";
    }
    destinoConversion_ = n;
    convirtiendo_ = true;
    etapa_ = "Preparando";
    hecho_ = 0;
    total_ = 0;
    emit progreso();
    conv_->convertir(documento, carpetaDe(n));
}

void CarruselViewModel::cancelar() { conv_->cancelar(); }

} // namespace ba::app
