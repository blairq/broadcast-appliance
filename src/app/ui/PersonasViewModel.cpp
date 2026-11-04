// ============================================================================
// src/app/ui/PersonasViewModel.cpp — ver PersonasViewModel.hpp
// ============================================================================
#include "ui/PersonasViewModel.hpp"

#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18nTexto.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>

#include "arranque/AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "persist/ColaEscritura.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"

#if BA_HAVE_ROSTROS
#include "evento/Evento.hpp"
#include "rostros/Personas.hpp"
#include "rostros/QuienEs.hpp"
#include "rostros/Rostros.hpp"
#include "sala/Atencion.hpp"
#include "ui/CalidadCara.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/Reconocedor.hpp"
#include "ui/TarjetasRostros.hpp"
#include "ui/VideoViewModel.hpp"

#include <nlohmann/json.hpp>
#endif

namespace ba::app {

#if BA_HAVE_ROSTROS
using nlohmann::json;
#endif

struct PersonasViewModel::Impl {
    AppRuntime& rt;
    StudioViewModel& vm;
    QString archivo;
    QString carpetaFotos;
    QString aviso;
    bool autoalta = false;
    bool lobbyAuto = false;
    /// La atención a quien se conecta lo pide: el lobby se mira aunque el
    /// interruptor esté apagado (ver lobbyMirando).
    bool lobbyPorAtencion = false;
    /// Anotar solo con nombre a quien lo dio (ver altaSiDioSuNombre). Se guarda.
    bool altaAutomatica = false;
    QString feedAuto = "program";
    /// La cámara de la PUERTA: reconoce a quien entra al lugar y lo acredita
    /// en el padrón del evento. Es la contracara del lobby —el mismo
    /// reconocimiento, apuntado al mundo físico en vez de al remoto— y es lo
    /// que hace que «¿quiénes están?» sepa de las dos mitades de la gente.
    bool puertaAuto = false;
    QString feedPuerta;
#if BA_HAVE_ROSTROS
    /// El reconocimiento, en su propio hilo. Ver Reconocedor.hpp: en el de la
    /// interfaz colgaba al aparato entero.
    Reconocedor rec;
    ba::rostros::Personas libreta;
    /// Quién es cada cara y lo que se anota (las reglas: rostros/QuienEs).
    ba::rostros::QuienEs quien{libreta};
    /// De dónde salen las caras: las tarjetas «Rostros» de las cámaras del estudio y los
    /// cuadros de las demás claves (ui/TarjetasRostros).
    TarjetasRostros tarjetas;
    bool listo = false;
    /// La ronda del anotado automático: mira sola, sin que nadie abra la
    /// página. Si dependiera de que la vista esté abierta, prender el
    /// interruptor y cambiar de pestaña apagaría el anotado sin avisar.
    QTimer ronda;
    QVariantList vistos;      ///< lo último que contestó el otro hilo
    bool turnoPuerta = false; ///< alterna lobby / puerta cuando los dos están
    Impl(AppRuntime& r, StudioViewModel& v) : rt(r), vm(v), tarjetas(r, v, rec, quien) {}
#else
    Impl(AppRuntime& r, StudioViewModel& v) : rt(r), vm(v) {}
#endif
};

#if !BA_HAVE_ROSTROS
// Sin los modelos vendoreados la página existe pero lo dice: es mejor que el
// operador vea «esto necesita los modelos» a que vea una lista vacía y crea
// que no hay nadie guardado.
PersonasViewModel::PersonasViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, vm)) {
    connect(this, &PersonasViewModel::autoaltaCambio, this, &PersonasViewModel::etiquetasCambio);
    d_->aviso = tr("Este equipo se compiló sin reconocimiento de rostros. Para activarlo: "
                   "`make deps-rostros` y volver a compilar.");
}
PersonasViewModel::~PersonasViewModel() = default;
QVariantList PersonasViewModel::personas() const { return {}; }
bool PersonasViewModel::disponible() const { return false; }
QString PersonasViewModel::aviso() const { return d_->aviso; }
void PersonasViewModel::refrescar() {}
QString PersonasViewModel::guardar(const QString&, const QString&, const QString&, const QString&) {
    return {};
}
bool PersonasViewModel::borrar(const QString&) { return false; }
void PersonasViewModel::sumarCara(const QString&, const QString&, bool) { emit notify(d_->aviso, "error"); }
void PersonasViewModel::mirar(const QString&) {}
void PersonasViewModel::alCambiarMirados() {}
void PersonasViewModel::mirarAhora() {}
void PersonasViewModel::sincronizarRostros() {}
void PersonasViewModel::usarCamara(const QString&, bool) {}
void PersonasViewModel::analizar(const QString&, bool, const QString&) {}
QVariantList PersonasViewModel::vistos() const { return {}; }
void PersonasViewModel::revisarFuentes() {}
QString PersonasViewModel::etiquetaDe(const QString& feed) const { return feed; }
bool PersonasViewModel::autoalta() const { return false; }
void PersonasViewModel::setAutoalta(bool) {}
bool PersonasViewModel::puertaAuto() const { return false; }
void PersonasViewModel::setPuertaAuto(bool) {}
QString PersonasViewModel::feedPuerta() const { return {}; }
void PersonasViewModel::setFeedPuerta(const QString&) {}
bool PersonasViewModel::lobbyAuto() const { return d_->lobbyAuto; }
bool PersonasViewModel::lobbyPorAtencion() const { return false; }
void PersonasViewModel::setLobbyPorAtencion(bool) {}
bool PersonasViewModel::lobbyMirando() const { return false; }
bool PersonasViewModel::altaAutomatica() const { return false; }
void PersonasViewModel::setAltaAutomatica(bool activo) {
    if (activo) emit notify(d_->aviso, "error");
}
float PersonasViewModel::umbralEntrarSolo() const { return 1.0f; }
void PersonasViewModel::compararFoto(const QByteArray&, const QString&,
                                     std::function<void(float, bool, const QString&)> luego) {
    luego(0.0f, false, d_->aviso);
}
void PersonasViewModel::altaConFoto(const QByteArray&, const QString&, const QString&,
                                    std::function<void(const QString&, const QString&)> luego) {
    luego({}, d_->aviso);
}
void PersonasViewModel::identificarFoto(const QByteArray&, std::function<void(const Identificado&, const QString&)> luego) {
    luego({}, d_->aviso);
}

// Sin reconocimiento no hay a quien mirar en el lobby: se avisa, como sumarCara.
void PersonasViewModel::setLobbyAuto(bool activo) {
    if (activo) emit notify(d_->aviso, "error");
}
QVariantMap PersonasViewModel::enLobby() const { return {}; }
QString PersonasViewModel::feedAuto() const { return d_->feedAuto; }
void PersonasViewModel::setFeedAuto(const QString&) {}
void PersonasViewModel::activarAutoalta(const QString&, bool activo) {
    if (activo) emit notify(d_->aviso, "error");
}
void PersonasViewModel::activarPuerta(const QString&, bool) {}
int PersonasViewModel::sinNombre() const { return 0; }
#else

namespace {

/// Guarda el recorte de la cara como foto de la ficha. Devuelve el nombre del
/// archivo, o vacío si no se pudo.
///
/// CON MARGEN Y CUADRADO. El rectángulo que da el detector es justo la cara:
/// recortado ahí, la foto queda claustrofóbica y cuesta reconocer a quién
/// pertenece. Un 40% alrededor entra el pelo y algo de hombro, que es lo que
/// una persona usa para decir «sí, es ella».
///
/// Chica a propósito (256 px): es para mirar en una lista, no para imprimir, y
/// multiplicada por cien fichas la carpeta portable se nota.
QString escribirFoto(const QImage& img, const ba::rostros::Cara& c, const QString& carpeta,
                     const QString& id) {
    const int margen = static_cast<int>(std::max(c.ancho, c.alto) * 0.4);
    const int lado = std::max(c.ancho, c.alto) + margen * 2;
    QRect r(c.x + c.ancho / 2 - lado / 2, c.y + c.alto / 2 - lado / 2, lado, lado);
    r = r.intersected(img.rect());
    if (r.width() < 16 || r.height() < 16) return {};
    QDir().mkpath(carpeta);
    const QString nombre = id + ".jpg";
    return img.copy(r)
                   .scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                   .save(carpeta + "/" + nombre, "JPG", 88)
               ? nombre
               : QString();
}

/// Cada cuánto mira la ronda. Detectar cuesta ~30 ms; a este ritmo es ruido de
/// fondo, y alcanza de sobra para juntar tres pasadas de quien se quedó.
inline int kRondaMs() { return ba::ajustes::entero("rostros.rondaMs", 700); }

/// Una foto JPEG como BGRA contiguo (lo que pide el reconocedor) y su QImage.
bool desdeJpeg(const QByteArray& jpeg, QImage& img, std::vector<std::uint8_t>& bgra) {
    if (!img.loadFromData(jpeg, "JPG") || img.isNull()) return false;
    img = img.convertToFormat(QImage::Format_ARGB32);
    bgra.resize(static_cast<std::size_t>(img.width()) * img.height() * 4);
    for (int y = 0; y < img.height(); ++y)
        std::memcpy(bgra.data() + static_cast<std::size_t>(y) * img.width() * 4, img.constScanLine(y),
                    static_cast<std::size_t>(img.width()) * 4);
    return true;
}

/// Lo visto, descrito para el modelo: la cara, su vector y si sirve (con la imagen).
std::vector<ba::rostros::QuienEs::Visto> describir(const std::vector<Reconocedor::Visto>& caras, const QImage& img) {
    std::vector<ba::rostros::QuienEs::Visto> out;
    out.reserve(caras.size());
    for (const auto& v : caras) {
        const Calidad q = calidadDe(img, v.cara);
        out.push_back({v.cara, v.vector, q.sirve, q.sugerencia});
    }
    return out;
}

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString urlDeFoto(const QString& carpeta, const std::string& foto) {
    return foto.empty() ? QString() : QUrl::fromLocalFile(carpeta + "/" + QString::fromStdString(foto)).toString();
}

} // namespace

PersonasViewModel::PersonasViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, vm)) {
    // Las etiquetas de las cámaras del anotado y la puerta: cambian con ellas y con las fuentes.
    connect(this, &PersonasViewModel::autoaltaCambio, this, &PersonasViewModel::etiquetasCambio);
    connect(this, &PersonasViewModel::fuentesCambio, this, &PersonasViewModel::etiquetasCambio);
    mirada_.setInterval(1000);
    connect(&mirada_, &QTimer::timeout, this, &PersonasViewModel::mirarAhora);
    d_->archivo = QString::fromStdString((rt.dataRoot() / "Config" / "personas.json").string());
    d_->carpetaFotos = QString::fromStdString((rt.dataRoot() / "Config" / "personas").string());
    // Los modelos se cargan UNA vez, y acá y no en cada llamada: cuesta ~100 ms
    // y la página los va a usar en cada refresco.
    // Vienen con el programa (no son datos del usuario).
    const std::string modelos = (ba::platform::bundleRoot() / "data" / "rostros").string();
    if (d_->rec.abrir(modelos)) {
        d_->listo = true;
        d_->libreta.leer(d_->archivo.toStdString());
        d_->altaAutomatica =
            config::leerObjeto(rt.dataRoot() / "Config" / "personas-opciones.json").value("altaAutomatica", false);
    } else {
        d_->aviso = d_->rec.error();
    }
    d_->ronda.setInterval(kRondaMs());
    connect(&d_->ronda, &QTimer::timeout, this, [this] {
        // Una sola cámara por vuelta, siempre. Mirar las cuatro posiciones del
        // lobby juntas cuadruplicaría el trabajo en la misma vuelta; rotando,
        // el costo es el mismo haya uno esperando o haya cuatro.
        QString uid;
        int lugar = 0;
        // En un evento híbrido las dos cosas pasan a la vez: gente entrando al
        // salón y gente entrando a la sala. Se turnan, así que prender las dos
        // no cuesta el doble: cuesta la mitad de rápido cada una.
        d_->turnoPuerta = !d_->turnoPuerta;
        const bool vaLaPuerta =
            d_->puertaAuto && !d_->feedPuerta.isEmpty() && (d_->turnoPuerta || !lobbyMirando());
        QString feed;
        if (vaLaPuerta)
            feed = d_->feedPuerta;
        else if (lobbyMirando())
            feed = proximaDelLobby(uid, lugar);
        else
            feed = d_->feedAuto;
        if (!feed.isEmpty()) analizar(feed, true, uid);
    });
    // Las tarjetas «Rostros» de las cámaras que Personas usa por su cuenta.
    d_->tarjetas.setUsos([this]() {
        std::map<std::string, std::string> u;
        const auto cam = [](const QString& f) { return f.startsWith("entrada:") ? f.mid(8).toStdString() : std::string(); };
        if (d_->autoalta && !cam(d_->feedAuto).empty()) u[cam(d_->feedAuto)] = "autoalta";
        if (d_->puertaAuto && !cam(d_->feedPuerta).empty()) u[cam(d_->feedPuerta)] = "puerta";
        return u;
    });
    connect(&d_->tarjetas, &TarjetasRostros::error, this, [this](const QString& e) { emit notify(e, "error"); });
}
PersonasViewModel::~PersonasViewModel() = default;

void PersonasViewModel::pedirAnalisis(const QString& feed, bool descartable, Analisis luego) {
    if (!d_->listo) return luego({}, QImage(), d_->aviso);
    d_->tarjetas.pedirAnalisis(feed, descartable, std::move(luego));
}

ba::rostros::Personas& PersonasViewModel::libreta() { return d_->libreta; }
QString PersonasViewModel::carpetaFotos() const { return d_->carpetaFotos; }

bool PersonasViewModel::persistir() {
    const bool ok = d_->libreta.guardar(d_->archivo.toStdString());
    d_->quien.guardada(ahoraMs());
    emit cambio();
    return ok;
}

void PersonasViewModel::sincronizarRostros() { d_->tarjetas.sincronizar(); }

void PersonasViewModel::usarCamara(const QString& entrada, bool usar) { d_->tarjetas.usarCamara(entrada, usar); }

bool PersonasViewModel::autoalta() const { return d_->autoalta; }

void PersonasViewModel::setAutoalta(bool activo) {
    if (activo == d_->autoalta) return;
    if (activo && !d_->listo) {
        emit notify(d_->aviso, "error");
        return;
    }
    d_->autoalta = activo;
    d_->quien.olvidarPendientes(); // lo que estaba a medio ver no se arrastra
    revisarRonda();
    sincronizarRostros();
    emit autoaltaCambio();
}

bool PersonasViewModel::puertaAuto() const { return d_->puertaAuto; }

void PersonasViewModel::setPuertaAuto(bool activo) {
    if (activo == d_->puertaAuto) return;
    d_->puertaAuto = activo;
    d_->quien.olvidarPuerta();
    revisarRonda(); // las tres formas de mirar comparten la ronda
    sincronizarRostros();
    emit autoaltaCambio();
}

QString PersonasViewModel::feedPuerta() const { return d_->feedPuerta; }

void PersonasViewModel::setFeedPuerta(const QString& feed) {
    if (feed == d_->feedPuerta) return;
    d_->feedPuerta = feed;
    // Otra cámara es otra puerta: lo que se avisó de la anterior no vale.
    d_->quien.olvidarPuerta();
    sincronizarRostros();
    emit autoaltaCambio();
}

bool PersonasViewModel::lobbyAuto() const { return d_->lobbyAuto; }

void PersonasViewModel::setLobbyAuto(bool activo) {
    if (activo == d_->lobbyAuto) return;
    if (activo && !d_->listo) {
        emit notify(d_->aviso, "error");
        return;
    }
    d_->lobbyAuto = activo;
    d_->quien.olvidarPendientes();
    if (!lobbyMirando()) {
        d_->quien.olvidarLobby();
        emit enLobbyCambio();
    }
    // Las formas de mirar comparten la ronda: la del lobby gana, porque el
    // lobby es lo que está pasando ahora.
    revisarRonda();
    emit autoaltaCambio();
}

void PersonasViewModel::revisarRonda() {
    const bool hay = d_->autoalta || d_->puertaAuto || lobbyMirando();
    if (hay) {
        if (!d_->ronda.isActive()) d_->ronda.start();
    } else if (d_->ronda.isActive()) {
        d_->ronda.stop();
        d_->libreta.guardar(d_->archivo.toStdString()); // las vistas pendientes
    }
}

bool PersonasViewModel::lobbyPorAtencion() const { return d_->lobbyPorAtencion; }

void PersonasViewModel::setLobbyPorAtencion(bool activo) {
    if (activo == d_->lobbyPorAtencion) return;
    // Sin modelos no hay nada que mirar; la atención sigue igual (por chat se
    // atiende sin cámara), así que no se avisa como un error.
    if (activo && !d_->listo) return;
    d_->lobbyPorAtencion = activo;
    if (!lobbyMirando()) {
        d_->quien.olvidarLobby();
        emit enLobbyCambio();
    }
    revisarRonda();
    emit autoaltaCambio();
}

bool PersonasViewModel::lobbyMirando() const { return d_->lobbyAuto || d_->lobbyPorAtencion; }

bool PersonasViewModel::altaAutomatica() const { return d_->altaAutomatica; }

void PersonasViewModel::setAltaAutomatica(bool activo) {
    if (activo == d_->altaAutomatica) return;
    if (activo && !d_->listo) {
        emit notify(d_->aviso, "error");
        return;
    }
    d_->altaAutomatica = activo;
    // Se guarda al lado de la libreta: es una decisión sobre la libreta, no
    // del evento (la misma persona se reconoce en el que viene).
    config::guardar(d_->rt.dataRoot() / "Config" / "personas-opciones.json", {{"altaAutomatica", activo}});
    emit autoaltaCambio();
}

float PersonasViewModel::umbralEntrarSolo() const { return ba::rostros::QuienEs::abrirLaPuerta(); }

void PersonasViewModel::compararFoto(const QByteArray& jpeg, const QString& personaId,
                                     std::function<void(float, bool, const QString&)> luego) {
    if (!d_->listo) return luego(0.0f, false, d_->aviso);
    auto img = std::make_shared<QImage>();
    std::vector<std::uint8_t> bgra;
    if (!desdeJpeg(jpeg, *img, bgra)) return luego(0.0f, false, tr("La foto no se pudo leer."));
    const int w = img->width(), h = img->height();
    QPointer<PersonasViewModel> yo(this);
    d_->rec.pedir(std::move(bgra), w, h, false,
                  [yo, img, personaId, luego](std::vector<Reconocedor::Visto> v, const QString& error) {
                      if (!yo) return;
                      if (!error.isEmpty()) return luego(0.0f, false, error);
                      const auto caras = describir(v, *img);
                      const int i = ba::rostros::QuienEs::laMasGrande(caras);
                      if (i < 0) return luego(0.0f, false, tr("No se ve ninguna cara en la foto."));
                      const auto& cara = caras[static_cast<std::size_t>(i)];
                      const auto mejor = yo->d_->quien.comparar(cara, personaId.toStdString());
                      if (!mejor) return luego(0.0f, false, tr("No hay ficha con ese id."));
                      luego(*mejor, cara.sirve, {});
                  });
}

void PersonasViewModel::identificarFoto(const QByteArray& jpeg,
                                        std::function<void(const Identificado&, const QString&)> luego) {
    if (!d_->listo) return luego({}, d_->aviso);
    auto img = std::make_shared<QImage>();
    std::vector<std::uint8_t> bgra;
    if (!desdeJpeg(jpeg, *img, bgra)) return luego({}, tr("La foto no se pudo leer."));
    const int w = img->width(), h = img->height();
    QPointer<PersonasViewModel> yo(this);
    // NO descartable: uno descartado no contesta nunca, y quien pregunta (el
    // tótem) espera la respuesta para pedir la siguiente foto. El que no
    // encola de más es quien llama: una foto por vez por tótem.
    d_->rec.pedir(std::move(bgra), w, h, false, [yo, img, luego](std::vector<Reconocedor::Visto> v, const QString& error) {
        if (!yo) return;
        if (!error.isEmpty()) return luego({}, error);
        Identificado q;
        const auto caras = describir(v, *img);
        const int m = ba::rostros::QuienEs::laMasGrande(caras);
        if (m < 0) return luego(q, {});
        const auto& visto = v[static_cast<std::size_t>(m)];
        q.hayCara = true;
        const Calidad cal = calidadDe(*img, visto.cara);
        q.sirve = cal.sirve;
        q.sugerencia = QString::fromStdString(cal.sugerencia);
        q.sugerenciaEvento = QString::fromStdString(cal.sugerenciaEvento);
        const auto c = yo->d_->quien.identificar(caras[static_cast<std::size_t>(m)], ahoraMs());
        q.parecido = c.parecido;
        q.personaId = QString::fromStdString(c.id);
        q.nombre = QString::fromStdString(c.nombre);
        luego(q, {});
    });
}

void PersonasViewModel::altaConFoto(const QByteArray& jpeg, const QString& nombre, const QString& cargo,
                                    std::function<void(const QString&, const QString&)> luego) {
    if (!d_->listo) return luego({}, d_->aviso);
    auto img = std::make_shared<QImage>();
    std::vector<std::uint8_t> bgra;
    if (!desdeJpeg(jpeg, *img, bgra)) return luego({}, tr("La foto no se pudo leer."));
    const int w = img->width(), h = img->height();
    QPointer<PersonasViewModel> yo(this);
    d_->rec.pedir(std::move(bgra), w, h, false,
                  [yo, img, nombre, cargo, luego](std::vector<Reconocedor::Visto> v, const QString& error) {
                      if (!yo) return;
                      if (!error.isEmpty()) return luego({}, error);
                      const auto caras = describir(v, *img);
                      const int i = ba::rostros::QuienEs::laMasGrande(caras);
                      if (i < 0) return luego({}, tr("No se ve ninguna cara en la foto."));
                      const auto& visto = caras[static_cast<std::size_t>(i)];
                      if (!visto.sirve) return luego({}, tr("La cara de la foto no sirve para reconocerla."));
                      const auto a = yo->d_->quien.alta(visto, nombre.trimmed().toStdString(), cargo.trimmed().toStdString());
                      if (!a.cambio) return luego(QString::fromStdString(a.id), {});
                      if (a.sinFoto) yo->ponerFoto(a.id, *img, visto.cara);
                      yo->persistir();
                      luego(QString::fromStdString(a.id), {});
                  });
}

void PersonasViewModel::ponerFoto(const std::string& id, const QImage& img, const ba::rostros::Cara& cara) {
    const QString foto = escribirFoto(img, cara, d_->carpetaFotos, QString::fromStdString(id));
    const auto* f = d_->libreta.porId(id);
    if (foto.isEmpty() || f == nullptr) return;
    ba::rostros::Persona conFoto = *f;
    conFoto.foto = foto.toStdString();
    d_->libreta.guardarFicha(conFoto);
}

void PersonasViewModel::altaSiDioSuNombre(const QString& uid, const QString& fichaId, bool sirve) {
    if (!d_->altaAutomatica || fichaId.isEmpty() || !sirve) return;
    auto* conf = static_cast<ConferenceViewModel*>(d_->vm.conference());
    auto* espera = conf != nullptr ? static_cast<JsonListModel*>(conf->waiting()) : nullptr;
    if (espera == nullptr) return;
    QString nombreSala, info;
    for (const auto& v : espera->items()) {
        const QJsonObject o = v.toObject();
        if (o.value("uid").toString() != uid) continue;
        nombreSala = o.value("name").toString();
        info = o.value("info").toString();
    }
    const std::string nombre = sala::nombreDado(nombreSala.toStdString());
    bool propia = false;
    const auto p = d_->quien.nombrar(uid.toStdString(), fichaId.toStdString(), sirve, nombre, info.trimmed().toStdString(), &propia);
    if (!p) return;
    d_->libreta.guardarFicha(*p);
    persistir();
    d_->quien.nombrada(uid.toStdString(), fichaId.toStdString());
    if (!propia)
        emit notify(tr("Anoté a «%1» en Personas: dio su nombre y se le ve bien la cara.").arg(QString::fromStdString(nombre)),
                    "info");
    ba::diag::registro().linea(ba::diag::Nivel::Info, "rostros", 0,
                               ba::i18n::llenar(ba::i18n::traducir("rostros", "alta automática: «%1» (ficha %2, lobby %3)"), {nombre, fichaId.toStdString(), uid.toStdString()}));
}

QVariantMap PersonasViewModel::enLobby() const {
    // Lo del modelo, con la foto como dirección para la vista.
    QVariantMap out;
    const json& m = d_->quien.enLobby();
    for (auto it = m.begin(); it != m.end(); ++it) {
        QVariantMap v = QJsonDocument::fromJson(QByteArray::fromStdString(it.value().dump())).object().toVariantMap();
        v["parecido"] = it.value().value("parecido", 0.0F);
        v["foto"] = urlDeFoto(d_->carpetaFotos, it.value().value("foto", std::string()));
        out[QString::fromStdString(it.key())] = v;
    }
    return out;
}

QString PersonasViewModel::proximaDelLobby(QString& uid, int& lugar) {
    // SE RECORREN PERSONAS, NO CANALES DE VIDEO. Barrer `lobby:1..4` a ciegas
    // era mirar lo que hubiera en esos canales, que no es lo mismo que mirar a
    // los que están esperando: un canal puede tener un cuadro viejo de alguien
    // que ya se fue, o quedar apuntado a otra cosa, y el reconocimiento del
    // lobby terminaría hablando de quien no está. La lista de espera es la
    // verdad de quién se está conectando; de ahí sale a quién mirar.
    auto* conf = static_cast<ConferenceViewModel*>(d_->vm.conference());
    if (conf == nullptr) return {};
    auto* espera = static_cast<JsonListModel*>(conf->waiting());
    if (espera == nullptr) return {};
    // Los que están conectados y tienen una posición de preview asignada. Sin
    // posición no hay video que mirar (el lobby publica un número acotado).
    std::vector<std::pair<std::string, int>> gente; // uid, lugar
    for (const auto& v : espera->items()) {
        const QJsonObject o = v.toObject();
        const int l = o.value("lobbySlot").toInt();
        const QString u = o.value("uid").toString();
        if (l > 0 && !u.isEmpty() && o.value("connected").toBool()) gente.emplace_back(u.toStdString(), l);
    }
    const auto t = d_->quien.proximoDelLobby(gente);
    if (!t.idos.empty()) emit enLobbyCambio();
    if (t.uid.empty()) return {};
    uid = QString::fromStdString(t.uid);
    lugar = t.lugar;
    return QString("lobby:%1").arg(lugar);
}

QString PersonasViewModel::feedAuto() const { return d_->feedAuto; }

void PersonasViewModel::setFeedAuto(const QString& feed) {
    if (feed.isEmpty() || feed == d_->feedAuto) return;
    d_->feedAuto = feed;
    d_->quien.olvidarPendientes(); // las caras de la otra cámara no cuentan para esta
    sincronizarRostros();
    emit autoaltaCambio();
}

void PersonasViewModel::activarAutoalta(const QString& fuente, bool activo) {
    if (activo && !d_->listo) {
        emit notify(d_->aviso, "error");
        return;
    }
    bool cambio = false;
    if (!fuente.isEmpty() && fuente != d_->feedAuto) {
        d_->feedAuto = fuente;
        cambio = true;
    }
    if (activo != d_->autoalta) {
        d_->autoalta = activo;
        cambio = true;
    }
    if (!cambio) return;
    d_->quien.olvidarPendientes(); // lo que estaba a medio ver (o de otra cámara) no se arrastra
    revisarRonda();
    sincronizarRostros();
    emit autoaltaCambio();
}

void PersonasViewModel::activarPuerta(const QString& fuente, bool activo) {
    bool cambio = false;
    if (!fuente.isEmpty() && fuente != d_->feedPuerta) {
        d_->feedPuerta = fuente;
        cambio = true;
    }
    if (activo != d_->puertaAuto) {
        d_->puertaAuto = activo;
        cambio = true;
    }
    if (!cambio) return;
    d_->quien.olvidarPuerta(); // otra cámara (o volver a mirar) es otra puerta
    revisarRonda();
    sincronizarRostros();
    emit autoaltaCambio();
}

int PersonasViewModel::sinNombre() const {
    int n = 0;
    for (const auto& p : d_->libreta.todas())
        if (p.nombre.empty()) ++n;
    return n;
}

bool PersonasViewModel::disponible() const { return d_->listo; }
QString PersonasViewModel::aviso() const { return d_->aviso; }

QVariantList PersonasViewModel::personas() const {
    QVariantList out;
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    for (const auto& p : d_->libreta.todas()) {
        out.append(
            QVariantMap{{"id", QString::fromStdString(p.id)},
                        {"nombre", QString::fromStdString(p.nombre)},
                        {"cargo", QString::fromStdString(p.cargo)},
                        {"nota", QString::fromStdString(p.nota)},
                        {"foto", urlDeFoto(d_->carpetaFotos, p.foto)},
                        {"caras", static_cast<int>(p.caras.size())},
                        // En segundos desde que se la vio, -1 = nunca. La página decide
                        // cómo decirlo; acá no se inventa un formato de fecha.
                        {"vistaSeg", p.vistaMs > 0 ? static_cast<int>((ahora - p.vistaMs) / 1000) : -1}});
    }
    return out;
}

void PersonasViewModel::refrescar() {
    if (!d_->listo) return;
    d_->libreta.leer(d_->archivo.toStdString());
    emit cambio();
}

QString PersonasViewModel::guardar(const QString& id, const QString& nombre, const QString& cargo,
                                   const QString& nota) {
    if (!d_->listo) return {};
    if (nombre.trimmed().isEmpty()) {
        emit notify(tr("Ponele un nombre: sin eso la ficha no sirve para nada."), "aviso");
        return {};
    }
    ba::rostros::Persona p;
    p.id = id.toStdString();
    p.nombre = nombre.trimmed().toStdString();
    p.cargo = cargo.trimmed().toStdString();
    p.nota = nota.trimmed().toStdString();
    std::string error;
    const std::string nuevo = guardarFicha(p, &error);
    if (nuevo.empty()) emit notify(QString::fromStdString(error), "error");
    return QString::fromStdString(nuevo);
}

std::string PersonasViewModel::guardarFicha(const ba::rostros::Persona& p, std::string* error) {
    if (!d_->listo) {
        if (error != nullptr) *error = d_->aviso.toStdString();
        return {};
    }
    const std::string id = d_->libreta.guardarFicha(p);
    if (!persistir()) {
        if (error != nullptr) *error = d_->libreta.error();
        return {};
    }
    return id;
}

bool PersonasViewModel::borrar(const QString& id) {
    if (!d_->listo) return false;
    const auto* p = d_->libreta.porId(id.toStdString());
    const QString foto = p != nullptr ? QString::fromStdString(p->foto) : QString();
    if (!d_->libreta.borrar(id.toStdString())) return false;
    // La foto se va con la ficha: dejarla suelta sería guardar la cara de
    // alguien que se pidió borrar. Por el escritor único, como la libreta.
    if (!foto.isEmpty()) ba::persist::cola().borrar((d_->carpetaFotos + "/" + foto).toStdU16String());
    persistir();
    return true;
}

ba::rostros::Personas::Alta PersonasViewModel::anotarCara(const std::string& id, const Reconocedor::Visto& visto,
                                                          const QImage& img, bool nuevaFoto) {
    const auto alta = d_->libreta.sumarCara(id, visto.vector);
    if (alta == ba::rostros::Personas::Alta::NoExiste) return alta;
    // La foto se saca la PRIMERA vez y después solo si la piden: cambiarla en
    // cada captura haría que la ficha se vea distinta cada vez sin que nadie lo
    // decidiera, y la que quedaría es la última, no la mejor.
    const auto* p = d_->libreta.porId(id);
    if (p != nullptr && (p->foto.empty() || nuevaFoto)) ponerFoto(id, img, visto.cara);
    persistir();
    return alta;
}

bool PersonasViewModel::fusionar(const std::string& id, const std::string& otro) {
    if (!d_->listo) return false;
    const auto* b = d_->libreta.porId(otro);
    const std::string fotoOtro = b != nullptr ? b->foto : std::string();
    if (!d_->libreta.fusionar(id, otro)) return false;
    // La foto de la absorbida se borra solo si la que queda no se quedó con ella
    // (pasa cuando la que queda no tenía foto).
    const auto* f = d_->libreta.porId(id);
    if (!fotoOtro.empty() && (f == nullptr || f->foto != fotoOtro))
        ba::persist::cola().borrar((d_->carpetaFotos + "/" + QString::fromStdString(fotoOtro)).toStdU16String());
    persistir();
    return true;
}

void PersonasViewModel::vistas(const std::vector<std::string>& ids) {
    if (!d_->listo) return;
    if (d_->quien.vistas(ids, ahoraMs())) persistir();
}

ba::rostros::QuienEs& PersonasViewModel::quienEs() { return d_->quien; }

void PersonasViewModel::sumarCara(const QString& id, const QString& feed, bool nuevaFoto) {
    // Contesta por `notify` y no devolviendo un texto: mirar una cara ya no es
    // instantáneo —pasa por el otro hilo—, así que no hay nada que devolver
    // todavía cuando esta función termina.
    pedirAnalisis(feed, false,
                  [this, id, nuevaFoto](std::vector<Reconocedor::Visto> vistos, const QImage& img,
                                        const QString& error) {
                      if (!error.isEmpty()) return emit notify(error, "error");
                      if (vistos.empty()) return emit notify(tr("No se ve ninguna cara ahí."), "aviso");
                      if (vistos.size() > 1)
                          return emit notify(tr("Hay %1 caras en el cuadro: no adivino cuál es. Elegí una fuente "
                                                "donde esté sola.")
                                                 .arg(vistos.size()),
                                             "aviso");
                      const auto alta = anotarCara(id.toStdString(), vistos[0], img, nuevaFoto);
                      if (alta == ba::rostros::Personas::Alta::NoExiste)
                          return emit notify(tr("No existe esa persona."), "error");
                      emit notify(alta == ba::rostros::Personas::Alta::Sumada
                                      ? tr("Cara sumada.")
                                      : tr("Esa pose ya la tenía: probá desde otro ángulo."),
                                  alta == ba::rostros::Personas::Alta::Sumada ? "ok" : "aviso");
                  });
}

QVariantList PersonasViewModel::vistos() const { return d_->vistos; }

void PersonasViewModel::alCambiarMirados() {
    if (mirando_.usados().isEmpty()) {
        mirada_.stop();
        // Nadie mira: la cámara del estudio deja de tener su tarjeta.
        d_->tarjetas.dejarDeMirar();
        return;
    }
    if (!mirada_.isActive()) mirada_.start();
    mirarAhora();
}

void PersonasViewModel::mirarAhora() {
    for (const QString& f : mirando_.usados())
        if (!f.isEmpty()) mirar(f);
}

void PersonasViewModel::mirar(const QString& feed) {
    d_->tarjetas.mirando(feed);
    // La vista pide «quién hay» para dibujar los recuadros. Que además anote
    // sola pasa si el interruptor está puesto EN ESA MISMA cámara: si el
    // operador está mirando otra, la que anota es la ronda, no la vista.
    analizar(feed, d_->autoalta && feed == d_->feedAuto);
}

void PersonasViewModel::analizar(const QString& feed, bool registrar, const QString& uid) {
    pedirAnalisis(feed, true,
                  [this, feed, uid, registrar](std::vector<Reconocedor::Visto> caras, const QImage& img,
                                               const QString& error) {
                      if (!error.isEmpty()) return;
                      resolver(feed, uid, std::move(caras), img, registrar);
                  });
}

void PersonasViewModel::dejarPasarSiCorresponde(const QString& uid, const QString& personaId,
                                                const QString& nombre, float parecido, bool sirve) {
    if (!d_->quien.podriaPasar(uid.toStdString(), parecido, sirve)) return; // ver QuienEs::abrirLaPuerta
    auto* ev = static_cast<EventoViewModel*>(d_->vm.evento());
    auto* conf = static_cast<ConferenceViewModel*>(d_->vm.conference());
    if (ev == nullptr || conf == nullptr) return;
    // El permiso vive en el PADRON del evento (`dejarPasarSolo`). Antes habia
    // una lista blanca aparte en el guion; dos listas para lo mismo se
    // desincronizan, asi que quedo una sola (ver EventoViewModel::migrarAutoAceptar).
    if (!ev->padronVm().puedeEntrarSolo(personaId)) return;
    d_->quien.paso(uid.toStdString());
    conf->admit(uid);
    // Y QUEDA ANOTADO QUE LLEGO. El que entra por la sala y el que entra por la
    // puerta van al mismo padron: si no, «quienes estan» solo sabe de la mitad
    // de la gente, que es el agujero que el padron vino a tapar.
    ev->padronVm().acreditarPorPersona(personaId, "lobby");
    // El asistente la atiende DE OTRA MANERA: confirmarle lo que ya sabemos,
    // no preguntarle quién es. `marcarAtendido` evita además que el camino de
    // siempre la aborde con el cuestionario completo.
    conf->marcarAtendido(uid);
    d_->vm.sala().assistantIntake(uid, nombre, true);
    // SE AVISA SIEMPRE. Que alguien entre a la sala sin que nadie lo decida en
    // el momento está bien si estaba en la lista, pero el operador tiene que
    // enterarse en el acto: es gente que aparece en su sala.
    emit notify(tr("%1 entró sola: tiene permiso en el padrón del evento.")
                    .arg(nombre.isEmpty() ? tr("Alguien de la lista del evento") : nombre),
                "info");
    ba::diag::registro().linea(ba::diag::Nivel::Info, "rostros", 0,
                               ba::i18n::llenar(ba::i18n::traducir("rostros", "auto-aceptada del evento: «%1» (ficha %2, lobby %3, parecido %4)"), {nombre.toStdString(), personaId.toStdString(), uid.toStdString(), std::to_string(parecido)}));
}

void PersonasViewModel::acreditarEnLaPuerta(const QString& personaId, const QString& nombre) {
    auto* ev = static_cast<EventoViewModel*>(d_->vm.evento());
    if (ev == nullptr || ev->guionVm().abierto().isEmpty()) return;
    // Acreditar de nuevo es inofensivo —no mueve la hora de la primera vez—,
    // pero AVISAR de nuevo no (QuienEs::avisarPuerta). El aviso es una vez por
    // persona; la constancia, siempre.
    if (!ev->padronVm().acreditarPorPersona(personaId, "rostro")) {
        // Esta en la libreta pero no en la lista de este evento. NO se lo anota
        // solo: quien entra sin estar invitado es una decision del que
        // organiza, no del reconocimiento. Se avisa una vez y se sigue.
        if (d_->quien.avisarPuerta(personaId.toStdString()))
            emit notify(tr("%1 llegó y no está en el padrón del evento.").arg(nombre.isEmpty() ? tr("Alguien") : nombre),
                        "warn");
        return;
    }
    if (!d_->quien.avisarPuerta(personaId.toStdString())) return;
    emit notify(tr("%1 llegó.").arg(nombre.isEmpty() ? tr("Alguien de la libreta") : nombre), "info");
    ba::diag::registro().linea(ba::diag::Nivel::Info, "rostros", 0,
                               ba::i18n::llenar(ba::i18n::traducir("rostros", "acreditado en la puerta: «%1» (ficha %2)"), {nombre.toStdString(), personaId.toStdString()}));
}

void PersonasViewModel::resolver(const QString& feed, const QString& uid, std::vector<Reconocedor::Visto> vistos,
                                 const QImage& img, bool registrar) {
    const std::int64_t ahora = ahoraMs();
    // Quién es cada cara (y, registrando, lo que se anota): rostros/QuienEs.
    const auto caras = describir(vistos, img);
    const ba::rostros::QuienEs::Resultado r = d_->quien.resolver(caras, registrar, ahora);
    // Las fichas nuevas, con la foto de la cara que se vio.
    for (const auto& [id, i] : r.fotos) ponerFoto(id, img, caras[i].cara);
    if (r.alta || r.guardarVistas) {
        d_->libreta.guardar(d_->archivo.toStdString());
        d_->quien.guardada(ahora);
    }
    if (r.lleno) {
        d_->aviso = tr("La libreta llegó a %1 fichas: apagué el anotado automático para "
                       "no llenarla de desconocidos. Completá o borrá las que sobran.")
                        .arg(ba::rostros::QuienEs::maxFichasAuto());
        setAutoalta(false);
        emit notify(d_->aviso, "aviso");
    }
    // --- La puerta ---------------------------------------------------------
    // Reconocer a alguien en la entrada del lugar y dejar constancia. Es el
    // mismo reconocimiento del lobby apuntado al mundo fisico, y es lo que hace
    // que «quienes estan» sepa de las dos mitades de la gente.
    if (d_->puertaAuto && !d_->feedPuerta.isEmpty() && feed == d_->feedPuerta)
        for (std::size_t i : d_->quien.paraAcreditar(r, caras))
            acreditarEnLaPuerta(QString::fromStdString(r.filas[i].id), QString::fromStdString(r.filas[i].nombre));

    if (feed.startsWith("lobby:")) {
        // Lo del lobby no va a la lista de la página de Personas: va a la baldosa de quien
        // está esperando, que es donde el operador lo está mirando (QuienEs::enLobby).
        if (uid.isEmpty()) return; // no se sabe de quién era esa cámara
        d_->quien.enLobby(uid.toStdString(), feed.mid(6).toInt(), r, caras, img.width(), img.height());
        const json& v = d_->quien.enLobby();
        if (v.contains(uid.toStdString())) {
            const json& x = v[uid.toStdString()];
            const QString id = QString::fromStdString(x.value("id", std::string()));
            const bool sirve = x.value("sirve", false);
            altaSiDioSuNombre(uid, id, sirve);
            if (!id.isEmpty())
                dejarPasarSiCorresponde(uid, id, QString::fromStdString(x.value("nombre", std::string())),
                                        x.value("parecido", 0.0F), sirve);
        }
        emit enLobbyCambio();
    } else {
        QVariantList out;
        for (const auto& f : r.filas)
            out.append(QVariantMap{{"id", QString::fromStdString(f.id)},
                                   {"nombre", QString::fromStdString(f.nombre)},
                                   {"parecido", f.parecido},
                                   {"x", f.cara.x},
                                   {"y", f.cara.y},
                                   {"ancho", f.cara.ancho},
                                   {"alto", f.cara.alto},
                                   // El tamaño del cuadro del que salieron: sin eso el
                                   // recuadro no se puede poner donde va. La vista no sabe
                                   // cuánto mide el video (cada fuente mide distinto) ni
                                   // dónde lo dejaron las bandas negras del encuadre.
                                   {"cw", img.width()},
                                   {"ch", img.height()}});
        d_->vistos = out;
        emit vistosCambio();
    }
    // La lista de fichas solo se avisa cuando cambió de verdad. Avisar en cada
    // vuelta la rearmaría mientras alguien la está editando.
    if (r.alta) emit cambio();
}

QString PersonasViewModel::etiquetaDe(const QString& feed) const {
    return d_->tarjetas.etiquetaDe(feed);
}

void PersonasViewModel::revisarFuentes() {
    QVariantList out;
    auto* frames = static_cast<FrameService*>(d_->vm.frames());
    for (const QString& k : frames->keys())
        out.append(QVariantMap{{"clave", k}, {"etiqueta", frames->label(k)}});
    // Las cámaras del estudio (las entradas del grafo de video).
    if (auto* vg = d_->vm.videoVista())
        for (const QString& e : vg->entradas())
            out.append(QVariantMap{{"clave", "entrada:" + e}, {"etiqueta", tr("Cámara · %1").arg(e)}});
    if (out == fuentes_) return;
    fuentes_ = out;
    emit fuentesCambio();
}
#endif // BA_HAVE_ROSTROS

} // namespace ba::app
