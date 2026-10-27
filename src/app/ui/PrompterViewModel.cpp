// ============================================================================
// src/app/ui/PrompterViewModel.cpp — ver PrompterViewModel.hpp
// ============================================================================
#include "ui/PrompterViewModel.hpp"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>

#include "sala/EnlacesSala.hpp"
#include "sala/Teleprompter.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/Discursos.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app {
namespace {

/// Cada cuánto se le pregunta el estado al gateway MIENTRAS la página está a
/// la vista. Medio segundo es lo mismo que usan la tablet y el control: el
/// texto se mueve parejo en las tres pantallas.
constexpr int kRefrescoMs = 500;

} // namespace

struct PrompterViewModel::Impl {
    EventoViewModel* evento = nullptr;
    ConferenceViewModel* sala = nullptr;

    /// Los archivos. La raíz se le pone en cada refresco: el evento abierto
    /// puede cambiar mientras la página está a la vista.
    Discursos docs;

    QString carpeta; ///< la que se está mirando ("" es la raíz)
    QString abierto; ///< documento abierto en el editor (rel)
    QString texto;   ///< lo que hay en el editor
    QString enDisco; ///< lo que había cuando se abrió (para saber si está sucio)
    QVariantList documentos;
    QStringList carpetas;

    QVariantMap estado;
    QVariantList conectados;
    QString clave;
    bool enLinea = false;
    QTimer reloj;
};

PrompterViewModel::PrompterViewModel(EventoViewModel* evento, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->evento = evento;
    d_->sala = sala;
    d_->reloj.setInterval(kRefrescoMs);
    connect(&d_->reloj, &QTimer::timeout, this, [this]() { consultar(); });
    if (evento != nullptr) {
        // Al abrir o cerrar un evento, los discursos son otros.
        connect(evento, &EventoViewModel::guionChanged, this, [this]() { refrescar(); });
    }
    refrescar();
}

PrompterViewModel::~PrompterViewModel() = default;

// --- Documentos ---------------------------------------------------------------------

QStringList PrompterViewModel::carpetas() const { return d_->carpetas; }
QString PrompterViewModel::carpeta() const { return d_->carpeta; }

void PrompterViewModel::setCarpeta(const QString& c) {
    if (c == d_->carpeta) return;
    d_->carpeta = c;
    refrescar();
}

QVariantList PrompterViewModel::documentos() const { return d_->documentos; }
QString PrompterViewModel::abierto() const { return d_->abierto; }
QString PrompterViewModel::texto() const { return d_->texto; }
bool PrompterViewModel::sucio() const { return !d_->abierto.isEmpty() && d_->texto != d_->enDisco; }
int PrompterViewModel::palabras() const { return Discursos::contarPalabras(d_->texto); }

int PrompterViewModel::duracionSeg() const {
    const int ppm = d_->estado.value("ppm", Discursos::kPpm).toInt();
    return ppm > 0 ? static_cast<int>(palabras() * 60.0 / ppm) : 0;
}

QString PrompterViewModel::sinEvento() const {
    if (d_->evento == nullptr) return tr("El guion del evento no está disponible.");
    if (d_->evento->carpetaDeDiscursos().isEmpty())
        return "Los discursos viven en la carpeta del evento. Abrí o creá un evento en la pestaña Guion "
               "y volvé: son los mismos archivos que usa el cue «teleprompter».";
    return {};
}

void PrompterViewModel::refrescar() {
    // La raíz puede haber cambiado (otro evento abierto, o ninguno).
    d_->docs.setRaiz(d_->evento == nullptr ? QString() : d_->evento->carpetaDeDiscursos());
    if (d_->docs.listo()) QDir(d_->docs.raiz()).mkpath(".");
    d_->carpetas = d_->docs.carpetas();
    // Si la carpeta que se estaba mirando ya no está (la borraron desde el
    // disco, o se cambió de evento), se vuelve a la raíz en vez de mostrar
    // una lista vacía sin explicación.
    if (!d_->carpetas.contains(d_->carpeta)) d_->carpeta.clear();
    d_->documentos = d_->docs.listar(d_->carpeta);
    // Cuál está en el teleprompter: se cargó con su `rel` como título.
    const QString cargadoAhora = cargado();
    for (QVariant& v : d_->documentos) {
        QVariantMap m = v.toMap();
        m["cargado"] = m.value("rel").toString() == cargadoAhora;
        v = m;
    }
    emit documentosCambio();
}

void PrompterViewModel::abrir(const QString& rel) {
    bool ok = false;
    const QString texto = d_->docs.leer(rel, &ok);
    if (!ok) {
        emit notify(tr("No pude abrir «%1»").arg(rel), "error");
        return;
    }
    d_->abierto = rel;
    d_->enDisco = texto;
    d_->texto = texto;
    emit editorCambio();
}

void PrompterViewModel::editar(const QString& texto) {
    if (texto == d_->texto) return;
    d_->texto = texto;
    emit editorCambio();
}

QString PrompterViewModel::guardar() {
    if (d_->abierto.isEmpty()) return tr("No hay ningún documento abierto.");
    const QString err = d_->docs.escribir(d_->abierto, d_->texto);
    if (!err.isEmpty()) return err;
    d_->enDisco = d_->texto;
    emit editorCambio();
    refrescar();
    return {};
}

QString PrompterViewModel::escribirDoc(const QString& rel, const QString& texto) {
    if (!d_->docs.listo()) return sinEvento();
    const QString err = d_->docs.escribir(rel, texto);
    if (!err.isEmpty()) return err;
    // Si es el que está abierto, el editor tiene que mostrar lo nuevo: si no,
    // el operador seguiría viendo lo viejo y al guardar lo pisaría.
    if (d_->abierto == rel) {
        d_->texto = texto;
        d_->enDisco = texto;
        emit editorCambio();
    }
    refrescar();
    return {};
}

QString PrompterViewModel::crear(const QString& nombre) {
    if (!d_->docs.listo()) return sinEvento();
    QString rel;
    const QString err = d_->docs.crear(d_->carpeta, nombre, &rel);
    if (!err.isEmpty()) return err;
    refrescar();
    abrir(rel);
    return {};
}

QString PrompterViewModel::crearCarpeta(const QString& nombre) {
    if (!d_->docs.listo()) return sinEvento();
    QString rel;
    const QString err = d_->docs.crearCarpeta(d_->carpeta, nombre, &rel);
    if (!err.isEmpty()) return err;
    d_->carpeta = rel; // se entra en la que se acaba de crear
    refrescar();
    return {};
}

QString PrompterViewModel::renombrar(const QString& rel, const QString& nombre) {
    QString nuevo;
    const QString err = d_->docs.renombrar(rel, nombre, &nuevo);
    if (!err.isEmpty()) return err;
    if (d_->abierto == rel) {
        d_->abierto = nuevo;
        emit editorCambio();
    }
    refrescar();
    return {};
}

QString PrompterViewModel::mover(const QString& rel, const QString& aCarpeta) {
    QString nuevo;
    const QString err = d_->docs.mover(rel, aCarpeta, &nuevo);
    if (!err.isEmpty()) return err;
    if (d_->abierto == rel) {
        d_->abierto = nuevo;
        emit editorCambio();
    }
    refrescar();
    return {};
}

QString PrompterViewModel::borrar(const QString& rel) {
    const QString err = d_->docs.borrar(rel);
    if (!err.isEmpty()) return err;
    if (d_->abierto == rel) {
        d_->abierto.clear();
        d_->texto.clear();
        d_->enDisco.clear();
        emit editorCambio();
    }
    refrescar();
    return {};
}

// --- El teleprompter ----------------------------------------------------------------

QVariantMap PrompterViewModel::estado() const { return d_->estado; }
bool PrompterViewModel::enLinea() const { return d_->enLinea; }
QString PrompterViewModel::cargado() const { return d_->estado.value("titulo").toString(); }
QString PrompterViewModel::clave() const { return d_->clave; }
QVariantList PrompterViewModel::conectados() const { return d_->conectados; }

int PrompterViewModel::oradores() const {
    int n = 0;
    for (const QVariant& v : d_->conectados)
        if (v.toMap().value("vista").toString() == "orador") ++n;
    return n;
}

int PrompterViewModel::controles() const { return static_cast<int>(d_->conectados.size()) - oradores(); }

double PrompterViewModel::avance() const {
    const double total = d_->estado.value("palabras", 0).toDouble();
    if (total <= 0) return 0;
    // La posición que manda el gateway es de cuando contestó; mientras corre
    // hay que sumarle lo que avanzó desde entonces, o la barra iría a tirones
    // de medio segundo.
    double pos = d_->estado.value("posicion", 0).toDouble();
    if (d_->estado.value("corriendo", false).toBool()) {
        const double minutos =
            (d_->estado.value("ahoraMs", 0).toLongLong() - d_->estado.value("desdeMs", 0).toLongLong()) /
            60000.0;
        pos += minutos * d_->estado.value("ppm", Discursos::kPpm).toDouble();
    }
    return std::clamp(pos / total, 0.0, 1.0);
}

int PrompterViewModel::faltaSeg() const {
    const int ppm = d_->estado.value("ppm", Discursos::kPpm).toInt();
    const double total = d_->estado.value("palabras", 0).toDouble();
    if (ppm <= 0 || total <= 0) return 0;
    return static_cast<int>(total * (1.0 - avance()) * 60.0 / ppm);
}

void PrompterViewModel::mirar(bool si) {
    if (si) {
        if (!d_->reloj.isActive()) d_->reloj.start();
        consultar(); // que la página no arranque en blanco medio segundo
    } else {
        d_->reloj.stop();
    }
}

void PrompterViewModel::consultar(std::function<void(bool)> cuando) {
    if (d_->sala == nullptr) {
        if (cuando) cuando(false);
        return;
    }
    QPointer<PrompterViewModel> yo(this);
    d_->sala->teleprompter().ver([yo, cuando](const std::string& err, const nlohmann::json& j) {
        if (!yo) return;
        auto& d_ = yo->d_;
        const bool ok = err.empty();
        const QJsonObject r = QJsonDocument::fromJson(QByteArray::fromStdString(
                                                          j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
                                  .object();
        const bool antes = d_->enLinea;
        d_->enLinea = ok;
        if (ok) {
            d_->estado = r.toVariantMap();
            d_->clave = r.value("clave").toString();
            d_->conectados.clear();
            for (const QJsonValue& v : r.value("clientes").toArray())
                d_->conectados.append(v.toObject().toVariantMap());
            // El texto entero viene en cada respuesta y no lo mira nadie desde
            // acá: la página muestra el título y el avance. Sacarlo del mapa
            // ahorra copiarlo a QML dos veces por segundo.
            d_->estado.remove("texto");
        }
        if (ok || antes) emit yo->estadoCambio();
        if (cuando) cuando(ok);
    });
}

// Todas las órdenes son operaciones del modelo (sala::Teleprompter), las mismas
// que usa el cue «teleprompter» del guion. Nada se guarda acá de antemano: se
// manda y se espera a que el estado vuelva con el cambio. Si el gateway no
// está, no pasa nada y la pantalla lo dice (`enLinea`).

QString PrompterViewModel::cargar(const QString& rel) {
    if (d_->sala == nullptr) return tr("El servidor no está disponible.");
    return QString::fromStdString(d_->sala->teleprompter().cargarDiscurso(d_->docs, rel.toStdString()));
}

QString PrompterViewModel::cargarDelEditor() {
    if (d_->abierto.isEmpty()) return tr("No hay ningún documento abierto.");
    if (d_->sala != nullptr) d_->sala->teleprompter().cargarTexto(d_->abierto.toStdString(), d_->texto.toStdString());
    return {};
}

void PrompterViewModel::arrancar(bool si) {
    if (d_->sala != nullptr) d_->sala->teleprompter().arrancar(si);
}
void PrompterViewModel::setPpm(int ppm) {
    if (d_->sala != nullptr) d_->sala->teleprompter().setPpm(ppm);
}
void PrompterViewModel::setTamano(int t) {
    if (d_->sala != nullptr) d_->sala->teleprompter().setTamano(t);
}
void PrompterViewModel::setEspejo(bool si) {
    if (d_->sala != nullptr) d_->sala->teleprompter().setEspejo(si);
}
void PrompterViewModel::avisar(const QString& texto) {
    if (d_->sala != nullptr) d_->sala->teleprompter().avisar(texto.toStdString());
}
void PrompterViewModel::panico() {
    if (d_->sala != nullptr) d_->sala->teleprompter().panico();
}

void PrompterViewModel::irA(double fraccion) {
    // La vista habla en fracción; el gateway, en palabras.
    const double total = d_->estado.value("palabras", 0).toDouble();
    if (d_->sala != nullptr) d_->sala->teleprompter().irA(std::clamp(fraccion, 0.0, 1.0) * total);
}

void PrompterViewModel::irAParrafo(int n) {
    if (d_->sala != nullptr) d_->sala->teleprompter().irAParrafo(n);
}

void PrompterViewModel::pedirQr(const QString& target, const QString& vista, bool conClave) {
    if (d_->sala == nullptr) {
        emit qrListo({}, {}, "El servidor no está corriendo", {});
        return;
    }
    QPointer<PrompterViewModel> yo(this);
    d_->sala->enlaces().prompter(target == "remote", vista.toStdString(), conClave, [yo](const sala::Enlace& e) {
        if (!yo) return;
        QStringList filas;
        for (const std::string& f : e.filas) filas << QString::fromStdString(f);
        emit yo->qrListo(QString::fromStdString(e.url), filas, QString::fromStdString(e.mensaje),
                         QString::fromStdString(e.clave));
    });
}

} // namespace ba::app
