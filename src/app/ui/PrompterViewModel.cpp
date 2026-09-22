// ============================================================================
// src/app/ui/PrompterViewModel.cpp — ver PrompterViewModel.hpp
// ============================================================================
#include "ui/PrompterViewModel.hpp"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>

#include "ui/ConferenceClient.hpp"
#include "ui/Discursos.hpp"
#include "ui/ConferenceViewModel.hpp"
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

    QString carpeta;  ///< la que se está mirando ("" es la raíz)
    QString abierto;  ///< documento abierto en el editor (rel)
    QString texto;    ///< lo que hay en el editor
    QString enDisco;  ///< lo que había cuando se abrió (para saber si está sucio)
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
    connect(&d_->reloj, &QTimer::timeout, this, [this]() {
        if (d_->sala == nullptr) return;
        d_->sala->client().get("/api/v1/prompter", [this](bool ok, const QJsonObject& r) {
            const bool antes = d_->enLinea;
            d_->enLinea = ok;
            if (ok) {
                d_->estado = r.toVariantMap();
                d_->clave = r.value("clave").toString();
                d_->conectados.clear();
                for (const QJsonValue& v : r.value("clientes").toArray())
                    d_->conectados.append(v.toObject().toVariantMap());
                // El texto entero viene en cada respuesta y no lo mira nadie
                // desde acá: la página muestra el título y el avance. Sacarlo
                // del mapa ahorra copiarlo a QML dos veces por segundo.
                d_->estado.remove("texto");
            }
            if (ok || antes) emit estadoCambio();
        });
    });
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
    if (d_->evento == nullptr) return "El guion del evento no está disponible.";
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
        emit notify("No pude abrir «" + rel + "»", "error");
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
    if (d_->abierto.isEmpty()) return "No hay ningún documento abierto.";
    const QString err = d_->docs.escribir(d_->abierto, d_->texto);
    if (!err.isEmpty()) return err;
    d_->enDisco = d_->texto;
    emit editorCambio();
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
        const double minutos = (d_->estado.value("ahoraMs", 0).toLongLong() -
                                d_->estado.value("desdeMs", 0).toLongLong()) /
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
    } else {
        d_->reloj.stop();
    }
}

namespace {
/// Todas las órdenes van por el mismo lado: POST al gateway, que es el dueño
/// del estado. Nada se guarda acá de antemano: se manda y se espera a que el
/// estado vuelva con el cambio. Si el gateway no está, no pasa nada y la
/// pantalla lo dice (`enLinea`), en vez de mostrar algo que no es.
void mandar(ConferenceViewModel* sala, const QJsonObject& cmd) {
    if (sala == nullptr) return;
    sala->client().post("/api/v1/prompter", cmd);
}
} // namespace

QString PrompterViewModel::cargar(const QString& rel) {
    bool ok = false;
    const QString texto = d_->docs.leer(rel, &ok);
    if (!ok) return "No pude abrir «" + rel + "».";
    // El título es el `rel`: así la lista sabe cuál está cargado, y el
    // operador ve "Apertura/intendente.txt" y no solo "intendente.txt".
    mandar(d_->sala, QJsonObject{{"titulo", rel}, {"texto", texto}});
    return {};
}

QString PrompterViewModel::cargarDelEditor() {
    if (d_->abierto.isEmpty()) return "No hay ningún documento abierto.";
    mandar(d_->sala, QJsonObject{{"titulo", d_->abierto}, {"texto", d_->texto}});
    return {};
}

void PrompterViewModel::arrancar(bool si) { mandar(d_->sala, QJsonObject{{"corriendo", si}}); }
void PrompterViewModel::setPpm(int ppm) {
    mandar(d_->sala, QJsonObject{{"ppm", std::clamp(ppm, 40, 400)}});
}
void PrompterViewModel::setTamano(int t) {
    mandar(d_->sala, QJsonObject{{"tamano", std::clamp(t, 1, 10)}});
}
void PrompterViewModel::setEspejo(bool si) { mandar(d_->sala, QJsonObject{{"espejo", si}}); }
void PrompterViewModel::avisar(const QString& texto) { mandar(d_->sala, QJsonObject{{"aviso", texto}}); }
void PrompterViewModel::panico() { mandar(d_->sala, QJsonObject{{"panico", true}}); }

void PrompterViewModel::irA(double fraccion) {
    const double total = d_->estado.value("palabras", 0).toDouble();
    mandar(d_->sala, QJsonObject{{"posicion", std::clamp(fraccion, 0.0, 1.0) * total}});
}

void PrompterViewModel::irAParrafo(int n) { mandar(d_->sala, QJsonObject{{"parrafo", std::max(0, n)}}); }

void PrompterViewModel::pedirQr(const QString& target, const QString& vista, bool conClave) {
    if (d_->sala == nullptr) {
        emit qrListo({}, {}, "La sala no está corriendo", {});
        return;
    }
    const QString t = target == "remote" ? "remote" : "local";
    const QString v = vista == "orador" ? "orador" : "control";
    d_->sala->client().get(
        "/api/v1/prompter-link?target=" + t + "&vista=" + v + "&conClave=" + (conClave ? "1" : "0"),
        [this](bool ok, const QJsonObject& r) {
            QStringList filas;
            for (const QJsonValue& x : r.value("modules").toArray()) filas << x.toString();
            QString msg = r.value("message").toString();
            const bool bien = ok && r.value("ok").toBool();
            if (!bien && msg.isEmpty()) msg = "La sala no responde";
            emit qrListo(bien ? r.value("url").toString() : QString(), filas, msg,
                         r.value("clave").toString());
        });
}

} // namespace ba::app
