// ============================================================================
// src/app/ui/CarruselViewModel.cpp — ver CarruselViewModel.hpp
// ============================================================================
#include "ui/CarruselViewModel.hpp"

#include <QDir>

#include <algorithm>
#include <QFileInfo>
#include <QUrl>

#include "arranque/AppRuntime.hpp"
#include "emision/BroadcastEngine.hpp"
#include "carrusel/Carrusel.hpp"
#include "carrusel/CarruselStore.hpp"
#include "carrusel/Conversion.hpp"
#include "diag/Registro.hpp"
#include "medios/Medios.hpp"

namespace ba::app {
namespace {

/// Desarmar tarda y pasa en otro proceso: cuando sale mal, el aviso de pantalla
/// se va en diez segundos y despues no queda NADA para saber por que. Esto ya
/// paso una vez —una conversion que fallaba en silencio— y encontrarlo costo
/// mas que escribir este renglon.
void anotar(diag::Nivel n, const std::string& texto) { diag::registro().linea(n, "laminas", 0, texto); }

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
    connect(conv_.get(), &carrusel::Conversor::avance, this, [this](QString etapa, int hecho, int total) {
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
        if (!store_->guardar(c))
            anotar(diag::Nivel::Aviso, "no pude guardar el carrusel «" + destinoConversion_.toStdString() +
                                           "»: " + store_->error());
        else
            anotar(diag::Nivel::Info, "«" + destinoConversion_.toStdString() +
                                          "»: " + std::to_string(imagenes.size()) + " laminas");
        actual_ = destinoConversion_;
        revisarEscena();
        emit cambio();
        emit notify(QString("«%1»: %2 láminas listas").arg(destinoConversion_).arg(imagenes.size()), "ok");
    });
    connect(conv_.get(), &carrusel::Conversor::fallo, this, [this](QString motivo) {
        convirtiendo_ = false;
        etapa_.clear();
        emit progreso();
        anotar(motivo == "cancelado" ? diag::Nivel::Info : diag::Nivel::Aviso,
               "desarmar «" + destinoConversion_.toStdString() + "»: " + motivo.toStdString());
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
        const QString tapa =
            c.laminas().empty()
                ? QString()
                : QUrl::fromLocalFile(QString::fromStdString(c.laminas().front().archivo)).toString();
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
    emit posicionCambio(); // otro carrusel, otra posición
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
        const QString titulo =
            l.titulo.empty() ? QFileInfo(archivo).completeBaseName() : QString::fromStdString(l.titulo);
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

int CarruselViewModel::sinAnalizar() const {
    const carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    if (!c) return 0;
    int n = 0;
    for (const carrusel::Lamina& l : c->laminas())
        if (!l.analizada) ++n;
    return n;
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
        emit notify(tr("Poné un nombre: sin nombre no se lo puede volver a encontrar"), "error");
        return false;
    }
    if (store_->porNombre(n.toStdString())) {
        emit notify(tr("Ya hay un carrusel llamado «%1»").arg(n), "error");
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
        emit notify(tr("Esa imagen ya estaba en el carrusel"), "info");
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
    // Y SALE. Esta línea es la razón de todo lo demás: sin ella el carrusel es
    // una lista de imágenes que no muestra ninguna.
    empujar(*c);
    // `posicionCambio` y NO `cambio`: lo segundo rearma la lista de láminas y
    // la vista recarga las cincuenta imágenes en cada salto. Se notaba: con la
    // ventana maximizada, el carrusel iba a los tirones.
    emit posicionCambio();
    return true;
}

int CarruselViewModel::posicion() const {
    const carrusel::Carrusel* c = store_->porNombre(actual_.toStdString());
    return c ? c->actual() : -1;
}

QString CarruselViewModel::enEscena() const { return enEscena_; }

void CarruselViewModel::revisarEscena() {
    const std::string escena = rt_.engine().currentScene();
    QString hallado;
    if (!escena.empty()) {
        const std::vector<std::string> fuentes = rt_.engine().sceneSources(escena);
        for (const carrusel::Carrusel& c : store_->todos()) {
            if (c.fuente().empty()) continue;
            if (std::find(fuentes.begin(), fuentes.end(), c.fuente()) == fuentes.end()) continue;
            hallado = QString::fromStdString(c.nombre());
            break; // el primero: dos carruseles en una escena es raro y ambiguo
        }
    }
    if (hallado == enEscena_) return;
    enEscena_ = hallado;
    // Y SE IMPONE LA LAMINA QUE EL CARRUSEL DICE. La fuente guarda su archivo
    // entre arranques, asi que al abrir el aparato puede estar mostrando una
    // lamina y la tarjeta marcando otra. Con la sala mirando, una marca que
    // miente es peor que no tener marca.
    if (!hallado.isEmpty()) {
        if (const carrusel::Carrusel* c = store_->porNombre(hallado.toStdString())) empujar(*c);
    }
    emit enEscenaCambio();
}

bool CarruselViewModel::existe(const QString& carrusel) const {
    return store_->porNombre(carrusel.toStdString()) != nullptr;
}

QString CarruselViewModel::fuenteDe(const QString& carrusel) const {
    const carrusel::Carrusel* c = store_->porNombre(carrusel.toStdString());
    return c ? QString::fromStdString(c->fuente()) : QString();
}

void CarruselViewModel::empujar(const carrusel::Carrusel& c) {
    if (c.fuente().empty()) return;
    const carrusel::Lamina* l = c.lamina();
    if (l == nullptr) return;
    // Se le cambia el archivo a la fuente que ya está en la escena. NO se la
    // vuelve a crear: recrearla en cada salto haría un parpadeo al aire y
    // perdería la ubicación que el operador le dio.
    rt_.updateSourceSettings(c.fuente(), nlohmann::json{{"file", l->archivo}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

bool CarruselViewModel::aplicarAEscena(const QString& carrusel, const QString& escena) {
    carrusel::Carrusel* c = store_->porNombre(carrusel.toStdString());
    if (c == nullptr) {
        emit notify(tr("No hay ningún carrusel llamado «%1»").arg(carrusel), "error");
        return false;
    }
    if (c->vacio()) {
        emit notify(tr("«%1» no tiene láminas todavía").arg(carrusel), "error");
        return false;
    }
    // El nombre lleva el del carrusel adentro: en la lista de fuentes de la
    // escena tiene que verse de qué es, y dos carruseles en la misma escena no
    // pueden compartir fuente.
    const std::string fuente = "Carrusel · " + carrusel.toStdString();
    if (c->fuente() != fuente) {
        SourceSpec spec;
        spec.id = "image_source";
        spec.name = fuente;
        spec.settingsJson = nlohmann::json{{"file", c->laminas().front().archivo}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f}; // pantalla completa; se acomoda en Escenas
        spec.visible = true;
        if (!rt_.addSource(escena.toStdString(), spec)) {
            // Puede fallar porque YA existe (el carrusel ya estaba en otra
            // escena): eso no es un error, es reusar la misma fuente.
            const QString err = QString::fromStdString(rt_.lastError());
            if (!err.contains("exist", Qt::CaseInsensitive) && !err.contains("ya", Qt::CaseInsensitive)) {
                emit notify(tr("No se pudo poner en la escena: %1").arg(err), "error");
                return false;
            }
        }
        c->setFuente(fuente);
        store_->persistir();
    }
    empujar(*c);
    rt_.save();
    revisarEscena();
    emit cambio();
    emit notify("«" + carrusel + "» quedó en «" + escena + "». Tocá una lámina para ponerla.", "ok");
    return true;
}

bool CarruselViewModel::soltarDeEscena(const QString& carrusel) {
    carrusel::Carrusel* c = store_->porNombre(carrusel.toStdString());
    if (c == nullptr || c->fuente().empty()) return false;
    c->setFuente("");
    store_->persistir();
    revisarEscena();
    emit cambio();
    emit notify(tr("«%1» ya no maneja ninguna fuente. La imagen quedó como estaba.").arg(carrusel), "info");
    return true;
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
    return !carrusel::Conversor::necesitaConvertir(archivo) || !carrusel::Conversor::herramienta("deckcraft").isEmpty();
}

void CarruselViewModel::desarmar(const QString& documento, const QString& nombre) {
    if (convirtiendo_) {
        emit notify(tr("Esperá: ya se está desarmando otro"), "info");
        return;
    }
    QString n = nombre.trimmed();
    if (n.isEmpty()) n = QFileInfo(documento).completeBaseName();
    // Un nombre repetido se numera en vez de pisar: traer dos veces la misma
    // presentación corregida es lo normal, y perder la anterior no.
    if (store_->porNombre(n.toStdString())) {
        int i = 2;
        while (store_->porNombre((n + " (" + QString::number(i) + ")").toStdString()))
            ++i;
        n = n + " (" + QString::number(i) + ")";
    }
    destinoConversion_ = n;
    convirtiendo_ = true;
    etapa_ = "Preparando";
    hecho_ = 0;
    total_ = 0;
    emit progreso();
    anotar(diag::Nivel::Info, "desarmando " + documento.toStdString() + " -> «" + n.toStdString() + "»");
    conv_->convertir(documento, carpetaDe(n));
}

void CarruselViewModel::cancelar() { conv_->cancelar(); }

} // namespace ba::app
