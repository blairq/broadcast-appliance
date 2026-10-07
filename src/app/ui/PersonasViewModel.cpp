// ============================================================================
// src/app/ui/PersonasViewModel.cpp — ver PersonasViewModel.hpp
// ============================================================================
#include "ui/PersonasViewModel.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>

#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <set>

#include <algorithm>

#include "AppRuntime.hpp"
#include "diag/Registro.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"

#if BA_HAVE_ROSTROS
#include "evento/Evento.hpp"
#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"
#include "ui/CalidadCara.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/Reconocedor.hpp"
#include "video/VideoGrafos.hpp"

#include <gst/video/video.h>
#include <nlohmann/json.hpp>

#include "Entrada.hpp"
#endif

namespace ba::app {

struct PersonasViewModel::Impl {
    AppRuntime& rt;
    StudioViewModel& vm;
    QString archivo;
    QString carpetaFotos;
    QString aviso;
    bool autoalta = false;
    bool lobbyAuto = false;
    QString feedAuto = "program";
    /// La cámara de la PUERTA: reconoce a quien entra al lugar y lo acredita
    /// en el padrón del evento. Es la contracara del lobby —el mismo
    /// reconocimiento, apuntado al mundo físico en vez de al remoto— y es lo
    /// que hace que «¿quiénes están?» sepa de las dos mitades de la gente.
    bool puertaAuto = false;
    QString feedPuerta;
    /// La última cámara (entrada) que pidió la página, y cuándo: su tarjeta de
    /// rostros se mantiene mientras se la mire.
    QString feedMirado;
    std::int64_t miradoMs = 0;
    /// Las cámaras que se están siguiendo (Seguidor): llevan su tarjeta mientras tanto.
    std::set<QString> seguidas;
#if BA_HAVE_ROSTROS
    /// El reconocimiento, en su propio hilo. Ver Reconocedor.hpp: en el de la
    /// interfaz colgaba al aparato entero.
    Reconocedor rec;
    ba::rostros::Personas libreta;
    bool listo = false;
    /// La ronda del anotado automático: mira sola, sin que nadie abra la
    /// página. Si dependiera de que la vista esté abierta, prender el
    /// interruptor y cambiar de pestaña apagaría el anotado sin avisar.
    QTimer ronda;
    /// Una cara desconocida que todavía no llegó a ser ficha.
    struct Pendiente {
        std::vector<float> v;
        int veces = 0;
        std::int64_t ultimoMs = 0;
    };
    std::vector<Pendiente> pendientes;
    std::int64_t ultimoGuardado = 0;
    QVariantList vistos;     ///< lo último que contestó el otro hilo
    QVariantMap enLobby;     ///< posición de espera -> quién es
    int turnoLobby = 0;      ///< por dónde va la rotación entre los que esperan
    QString uidMirado;       ///< a quién se le pidió la última mirada del lobby
    QSet<QString> yaPasaron; ///< a quién ya se hizo pasar sola (una vez c/u)
    /// A quién ya se acreditó en la puerta. Acreditar de nuevo es inofensivo
    /// (no mueve la hora), pero AVISAR de nuevo no: alguien parado frente a la
    /// cámara generaría un aviso por segundo.
    QSet<QString> avisadosPuerta;
    bool turnoPuerta = false; ///< alterna lobby / puerta cuando los dos están
    int lugarMirado = 0;      ///< y en qué posición de preview estaba
    /// Quien espera lo próximo que vea la tarjeta «Rostros» de una cámara.
    struct Espera {
        int id = 0;
        Analisis luego;
    };
    std::map<QString, std::vector<Espera>> esperas;
    int proximaEspera = 1;
    /// El número que la tarjeta le dio a cada cara del último resultado de
    /// cada cámara (en el orden de las caras): para devolverle los nombres.
    std::map<QString, std::vector<int>> idsDe;
    /// Lo último que se le mandó a cada tarjeta como etiquetas (para no repetir).
    std::map<QString, std::string> etiquetasDe;
    int escucha = 0; ///< los mensajes de las tarjetas (motor::escucharElementos)
#endif
    Impl(AppRuntime& r, StudioViewModel& v) : rt(r), vm(v) {}
};

#if !BA_HAVE_ROSTROS
// Sin los modelos vendoreados la página existe pero lo dice: es mejor que el
// operador vea «esto necesita los modelos» a que vea una lista vacía y crea
// que no hay nadie guardado.
PersonasViewModel::PersonasViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, vm)) {
    d_->aviso = "Este equipo se compiló sin reconocimiento de rostros. Para activarlo: "
                "`make deps-rostros` y volver a compilar.";
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
void PersonasViewModel::sincronizarRostros() {}
void PersonasViewModel::usarCamara(const QString&, bool) {}
void PersonasViewModel::analizar(const QString&, bool, const QString&) {}
QVariantList PersonasViewModel::vistos() const { return {}; }
QVariantList PersonasViewModel::fuentes() const { return {}; }
QString PersonasViewModel::etiquetaDe(const QString& feed) const { return feed; }
bool PersonasViewModel::autoalta() const { return false; }
void PersonasViewModel::setAutoalta(bool) {}
bool PersonasViewModel::puertaAuto() const { return false; }
void PersonasViewModel::setPuertaAuto(bool) {}
QString PersonasViewModel::feedPuerta() const { return {}; }
void PersonasViewModel::setFeedPuerta(const QString&) {}
bool PersonasViewModel::lobbyAuto() const { return d_->lobbyAuto; }

// Sin reconocimiento no hay a quien mirar en el lobby: se avisa, como sumarCara.
void PersonasViewModel::setLobbyAuto(bool activo) {
    if (activo) emit notify(d_->aviso, "error");
}
QVariantMap PersonasViewModel::enLobby() const { return {}; }
QString PersonasViewModel::feedAuto() const { return d_->feedAuto; }
void PersonasViewModel::setFeedAuto(const QString&) {}
int PersonasViewModel::sinNombre() const { return 0; }
#else

namespace {

/// El cuadro de un feed en BGRA contiguo (lo que pide el motor) y su QImage.
bool cuadro(StudioViewModel& vm, const QString& feed, QImage& img, std::vector<std::uint8_t>& bgra) {
    // (Las cámaras del estudio no pasan por acá: las mira su tarjeta «Rostros»,
    // ver pedirAnalisis.)
    auto* frames = static_cast<FrameService*>(vm.frames());
    std::uint64_t serial = 0;
    if (!frames->latest(feed, img, serial) || img.isNull()) return false;
    img = img.convertToFormat(QImage::Format_ARGB32);
    bgra.resize(static_cast<std::size_t>(img.width()) * img.height() * 4);
    for (int y = 0; y < img.height(); ++y)
        std::memcpy(bgra.data() + static_cast<std::size_t>(y) * img.width() * 4, img.constScanLine(y),
                    static_cast<std::size_t>(img.width()) * 4);
    return true;
}

/// Un GstSample BGRA (el cuadro que analizó la tarjeta) como QImage.
QImage imagenDe(GstSample* s) {
    if (s == nullptr) return {};
    GstVideoInfo vi;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstVideoFrame f;
    if (b == nullptr || !gst_video_info_from_caps(&vi, gst_sample_get_caps(s)) ||
        !gst_video_frame_map(&f, &vi, b, GST_MAP_READ))
        return {};
    const int w = GST_VIDEO_FRAME_WIDTH(&f), h = GST_VIDEO_FRAME_HEIGHT(&f);
    QImage img(w, h, QImage::Format_ARGB32); // BGRA en memoria
    for (int y = 0; y < h; ++y)
        std::memcpy(img.scanLine(y),
                    static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&f, 0)) +
                        static_cast<std::size_t>(y) * GST_VIDEO_FRAME_PLANE_STRIDE(&f, 0),
                    static_cast<std::size_t>(w) * 4);
    gst_video_frame_unmap(&f);
    return img;
}

// --- El anotado automático, y por qué estos números -------------------------
//
// QUE HACE: con el interruptor puesto, cada cara que pasa por la cámara
// elegida y no cae en ninguna ficha queda anotada SOLA, con su foto y sin
// nombre, para que después alguien le ponga quién es. Y a las que sí reconoce
// les suma la pose nueva, con lo que reconoce mejor cada vez que la ve.
//
// QUE NO HACE: no le pone nombre a nadie ni saca nada al aire. Una ficha sin
// nombre no rotula.
//
// TRES VECES ANTES DE ANOTAR, y no una: un reflejo, una mano o media cara que
// entra y sale dan una detección suelta. Anotando la primera, la libreta se
// llena de fichas de nada y el operador termina borrando más de lo que
// completa. Tres pasadas seguidas ya es alguien que está ahí.
constexpr int kVecesParaAnotar = 3;
/// Si esa cara no volvió en este rato, se olvida y empieza de nuevo.
///
/// DOCE SEGUNDOS Y NO CUATRO, por el lobby: ahí la ronda rota entre las
/// posiciones de espera, así que a la misma persona la vuelve a mirar cada
/// varios segundos. Con cuatro, se olvidaba de ella entre vuelta y vuelta y no
/// llegaba nunca a las tres pasadas: el anotado automático no anotaba a nadie.
constexpr std::int64_t kOlvidoPendienteMs = 12000;
/// Tope de la libreta para el anotado automático. Llegando acá se apaga solo y
/// lo dice: una libreta de miles de desconocidos no la revisa nadie, y cada
/// búsqueda las recorre a todas.
constexpr int kMaxFichasAuto = 500;
/// Cada cuánto mira la ronda. Detectar cuesta ~30 ms; a este ritmo es ruido de
/// fondo, y alcanza de sobra para juntar tres pasadas de quien se quedó.
constexpr int kRondaMs = 700;
/// Cuánto parecido hace falta para ABRIRLE LA PUERTA a alguien sin preguntar.
///
/// MAS QUE PARA PONERLE UN NOMBRE, y esa diferencia es la decisión. El 0.45 de
/// `kMismaPersona` está elegido para un rótulo: si se equivoca, alguien lo ve
/// en pantalla y lo corrige. Dejar entrar a la sala no se corrige igual —ya
/// está adentro, con cámara y micrófono, y puede quedar al aire—. Para eso se
/// pide un margen que no deja lugar a un parecido de familia: medido, la misma
/// persona da 0.72-0.90 y dos personas distintas ~0.13.
///
/// Y ADEMAS la cara tiene que servir: un reconocimiento hecho sobre una cara
/// borrosa o de perfil puede dar cualquier número, y ese número no puede ser
/// el que abre la puerta.
constexpr float kAbrirLaPuerta = 0.60F;
/// «La vi recién» se guarda cada tanto, no en cada vuelta: escribir la libreta
/// entera —vectores incluidos— dos veces por segundo es todo disco para nada.
/// Un alta o una cara nueva sí se guardan en el acto.
constexpr std::int64_t kGuardarVistasMs = 30000;

/// El recorte de la cara, con margen, para la ficha (ver McpRostroTools).
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

} // namespace

PersonasViewModel::PersonasViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt, vm)) {
    d_->archivo = QString::fromStdString((rt.dataRoot() / "Config" / "personas.json").string());
    d_->carpetaFotos = QString::fromStdString((rt.dataRoot() / "Config" / "personas").string());
    // Los modelos se cargan UNA vez, y acá y no en cada llamada: cuesta ~100 ms
    // y la página los va a usar en cada refresco.
    // Vienen con el programa (no son datos del usuario).
    const std::string modelos = (ba::platform::bundleRoot() / "data" / "rostros").string();
    if (d_->rec.abrir(modelos)) {
        d_->listo = true;
        d_->libreta.leer(d_->archivo.toStdString());
    } else {
        d_->aviso = d_->rec.error();
    }
    d_->ronda.setInterval(kRondaMs);
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
            d_->puertaAuto && !d_->feedPuerta.isEmpty() && (d_->turnoPuerta || !d_->lobbyAuto);
        QString feed;
        if (vaLaPuerta)
            feed = d_->feedPuerta;
        else if (d_->lobbyAuto)
            feed = proximaDelLobby(uid, lugar);
        else
            feed = d_->feedAuto;
        if (!feed.isEmpty()) analizar(feed, true, uid);
    });
    // Lo que ven las tarjetas «Rostros» llega por el bus de cada entrada (en el
    // bucle del motor): se pasa al hilo de la interfaz, que es quien toca la libreta.
    QPointer<PersonasViewModel> yo(this);
    d_->escucha = ba::motor::escucharElementos([yo](const std::string& entrada, const std::string& nombre,
                                                    const std::string& json, GstSample* cuadro) {
        if (nombre != "ba-rostros") return;
        if (cuadro != nullptr) gst_sample_ref(cuadro);
        QMetaObject::invokeMethod(
            qApp,
            [yo, e = QString::fromStdString(entrada), js = json, cuadro]() {
                if (yo) yo->alRostros(e, js, cuadro);
                if (cuadro != nullptr) gst_sample_unref(cuadro);
            },
            Qt::QueuedConnection);
    });
}
PersonasViewModel::~PersonasViewModel() { ba::motor::dejarDeEscuchar(d_->escucha); }

void PersonasViewModel::alRostros(const QString& entrada, const std::string& js, GstSample* cuadro) {
    const QString feed = "entrada:" + entrada;
    const nlohmann::json j = nlohmann::json::parse(js, nullptr, false);
    // A la tarjeta, QUIÉN ES cada cara (por su número): su overlay muestra el
    // nombre y el cargo, y el resto de la cadena los recibe en las metas y los
    // eventos de cada cara. Con cada resultado que trae vectores, la espere
    // alguien o no; solo si cambió.
    if (j.is_object()) {
        nlohmann::json etiquetas = nlohmann::json::array();
        bool hayVectores = false;
        for (const auto& c : j.value("caras", nlohmann::json::array())) {
            if (!c.contains("vector") || !c["vector"].is_array()) continue;
            hayVectores = true;
            const auto q = d_->libreta.buscar(c["vector"].get<std::vector<float>>());
            if (q.parecido < ba::rostros::kMismaPersona) continue;
            const ba::rostros::Persona* p = d_->libreta.porId(q.id);
            if (p == nullptr || p->nombre.empty()) continue;
            etiquetas.push_back({{"id", c.value("id", 0)}, {"nombre", p->nombre}, {"cargo", p->cargo}, {"persona", q.id}});
        }
        const std::string texto = etiquetas.dump();
        if (hayVectores && d_->etiquetasDe[feed] != texto) {
            d_->etiquetasDe[feed] = texto;
            d_->rt.engine().controlEntrada(entrada.toStdString(), {{"rostros_etiquetas", texto}});
        }
    }
    auto it = d_->esperas.find(feed);
    if (it == d_->esperas.end() || it->second.empty()) return; // nadie la está esperando
    QImage img = imagenDe(cuadro);
    std::vector<Reconocedor::Visto> vistos;
    std::vector<int> ids;
    QString error;
    if (j.is_object()) {
        const nlohmann::json a = j.value("analisis", nlohmann::json::object());
        const double W = a.value("ancho", img.width()), H = a.value("alto", img.height());
        for (const auto& c : j.value("caras", nlohmann::json::array())) {
            Reconocedor::Visto v;
            v.cara.x = static_cast<int>(c.value("x", 0.0) * W);
            v.cara.y = static_cast<int>(c.value("y", 0.0) * H);
            v.cara.ancho = static_cast<int>(c.value("ancho", 0.0) * W);
            v.cara.alto = static_cast<int>(c.value("alto", 0.0) * H);
            v.cara.confianza = c.value("confianza", 0.0F);
            if (c.contains("puntos") && c["puntos"].is_array() && c["puntos"].size() == 10)
                for (int k = 0; k < 5; ++k) {
                    v.cara.puntos[k * 2] = static_cast<float>(c["puntos"][k * 2].get<double>() * W);
                    v.cara.puntos[k * 2 + 1] = static_cast<float>(c["puntos"][k * 2 + 1].get<double>() * H);
                }
            if (c.contains("vector") && c["vector"].is_array()) v.vector = c["vector"].get<std::vector<float>>();
            ids.push_back(c.value("id", 0));
            vistos.push_back(std::move(v));
        }
        error = QString::fromStdString(j.value("error", std::string()));
    } else {
        error = "La tarjeta Rostros mandó algo que no se entiende.";
    }
    d_->idsDe[feed] = ids;
    std::vector<Impl::Espera> esperan = std::move(it->second);
    d_->esperas.erase(it);
    for (auto& e : esperan) e.luego(vistos, img, vistos.empty() ? error : QString());
}

void PersonasViewModel::pedirAnalisis(const QString& feed, bool descartable, Analisis luego) {
    if (!d_->listo) return luego({}, QImage(), d_->aviso);
    if (feed.startsWith("entrada:")) {
        // UNA CÁMARA DEL ESTUDIO: la mira su tarjeta «Rostros» (el elemento
        // barostros, en la cadena de la entrada) y contesta por el bus con las
        // caras, su vector y el cuadro: se espera su próximo resultado. Si
        // todavía no la tiene, se la pone (como si la página la mirara).
        d_->feedMirado = feed;
        d_->miradoMs = QDateTime::currentMSecsSinceEpoch();
        sincronizarRostros();
        auto& lista = d_->esperas[feed];
        if (descartable && !lista.empty()) return; // la ronda vuelve sola
        const int id = d_->proximaEspera++;
        lista.push_back({id, std::move(luego)});
        QTimer::singleShot(3000, this, [this, feed, id]() {
            auto it = d_->esperas.find(feed);
            if (it == d_->esperas.end()) return;
            auto& l = it->second;
            const auto e = std::find_if(l.begin(), l.end(), [id](const Impl::Espera& x) { return x.id == id; });
            if (e == l.end()) return;
            Analisis f = std::move(e->luego);
            l.erase(e);
            f({}, QImage(), "La tarjeta Rostros de «" + etiquetaDe(feed) + "» no contestó (¿hay imagen?).");
        });
        return;
    }
    // El cuadro se toma ACA: FrameService es de la interfaz y no se comparte.
    // Lo que se manda al otro hilo es una copia de los píxeles, que es barata
    // al lado de lo que cuesta mirarlos.
    auto img = std::make_shared<QImage>();
    std::vector<std::uint8_t> bgra;
    if (!cuadro(d_->vm, feed, *img, bgra)) {
        // Una cámara del estudio recién pedida: su tarjeta empieza a bajar
        // cuadros con este pedido. Lo que alguien espera se reintenta un rato;
        // la ronda no (vuelve sola en la vuelta siguiente).
        if (feed.startsWith("entrada:") && !descartable) {
            // Si todavía no tiene su tarjeta, se la pone (como si la página la mirara).
            d_->feedMirado = feed;
            d_->miradoMs = QDateTime::currentMSecsSinceEpoch();
            sincronizarRostros();
            QTimer::singleShot(200, this, [this, feed, luego]() { reintentarAnalisis(feed, luego, 1); });
            return;
        }
        return luego({}, QImage(), "No hay imagen en «" + etiquetaDe(feed) + "».");
    }
    const int w = img->width();
    const int h = img->height();
    d_->rec.pedir(std::move(bgra), w, h, descartable,
                  [this, feed, luego = std::move(luego), img](std::vector<Reconocedor::Visto> vistos,
                                                              const QString& error) {
                      luego(std::move(vistos), *img, error);
                  });
}

void PersonasViewModel::reintentarAnalisis(const QString& feed, Analisis luego, int intento) {
    auto img = std::make_shared<QImage>();
    std::vector<std::uint8_t> bgra;
    if (cuadro(d_->vm, feed, *img, bgra)) {
        const int w = img->width(), h = img->height();
        d_->rec.pedir(
            std::move(bgra), w, h, false,
            [this, feed, luego = std::move(luego), img](std::vector<Reconocedor::Visto> v, const QString& e) {
                luego(std::move(v), *img, e);
            });
    } else if (intento < 10) {
        QTimer::singleShot(200, this, [this, feed, luego = std::move(luego), intento]() mutable {
            reintentarAnalisis(feed, std::move(luego), intento + 1);
        });
    } else {
        luego({}, QImage(), "No hay imagen en «" + etiquetaDe(feed) + "».");
    }
}

ba::rostros::Personas& PersonasViewModel::libreta() { return d_->libreta; }
QString PersonasViewModel::carpetaFotos() const { return d_->carpetaFotos; }

bool PersonasViewModel::persistir() {
    const bool ok = d_->libreta.guardar(d_->archivo.toStdString());
    d_->ultimoGuardado = QDateTime::currentMSecsSinceEpoch();
    emit cambio();
    return ok;
}

void PersonasViewModel::sincronizarRostros() {
    // Las cámaras del estudio que Personas usa llevan su tarjeta «Rostros» en
    // el grafo de video: se pone al elegirlas y se saca al dejar de usarlas.
    auto* vg = d_->vm.videoGrafos();
    if (vg == nullptr) return;
    std::map<std::string, std::string> quiero;
    const auto cam = [](const QString& f) {
        return f.startsWith("entrada:") ? f.mid(8).toStdString() : std::string();
    };
    if (!cam(d_->feedMirado).empty() && QDateTime::currentMSecsSinceEpoch() - d_->miradoMs < 15000)
        quiero[cam(d_->feedMirado)] = "mirar";
    if (d_->autoalta && !cam(d_->feedAuto).empty()) quiero[cam(d_->feedAuto)] = "autoalta";
    if (d_->puertaAuto && !cam(d_->feedPuerta).empty()) quiero[cam(d_->feedPuerta)] = "puerta";
    for (const QString& e : d_->seguidas)
        quiero[e.toStdString()] = "seguir";
    const std::string e = vg->configurarRostros(quiero);
    if (!e.empty()) emit notify(QString::fromStdString(e), "error");
}

void PersonasViewModel::usarCamara(const QString& entrada, bool usar) {
    const bool antes = d_->seguidas.count(entrada) > 0;
    if (usar == antes) return;
    if (usar)
        d_->seguidas.insert(entrada);
    else
        d_->seguidas.erase(entrada);
    sincronizarRostros();
}

bool PersonasViewModel::autoalta() const { return d_->autoalta; }

void PersonasViewModel::setAutoalta(bool activo) {
    if (activo == d_->autoalta) return;
    if (activo && !d_->listo) {
        emit notify(d_->aviso, "error");
        return;
    }
    d_->autoalta = activo;
    d_->pendientes.clear(); // lo que estaba a medio ver no se arrastra
    if (activo) {
        d_->ronda.start();
    } else if (!d_->lobbyAuto) {
        d_->ronda.stop();
        d_->libreta.guardar(d_->archivo.toStdString()); // las vistas pendientes
    }
    sincronizarRostros();
    emit autoaltaCambio();
}

bool PersonasViewModel::puertaAuto() const { return d_->puertaAuto; }

void PersonasViewModel::setPuertaAuto(bool activo) {
    if (activo == d_->puertaAuto) return;
    d_->puertaAuto = activo;
    d_->avisadosPuerta.clear();
    // Las tres formas de mirar comparten la ronda.
    if (activo || d_->lobbyAuto || d_->autoalta)
        d_->ronda.start();
    else {
        d_->ronda.stop();
        d_->libreta.guardar(d_->archivo.toStdString());
    }
    sincronizarRostros();
    emit autoaltaCambio();
}

QString PersonasViewModel::feedPuerta() const { return d_->feedPuerta; }

void PersonasViewModel::setFeedPuerta(const QString& feed) {
    if (feed == d_->feedPuerta) return;
    d_->feedPuerta = feed;
    // Otra cámara es otra puerta: lo que se avisó de la anterior no vale.
    d_->avisadosPuerta.clear();
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
    d_->pendientes.clear();
    if (!activo) {
        d_->enLobby.clear();
        emit enLobbyCambio();
    }
    // Las dos formas de anotar comparten la ronda: la de acá gana, porque el
    // lobby es lo que está pasando ahora.
    if (activo || d_->autoalta || d_->puertaAuto) {
        d_->ronda.start();
    } else {
        d_->ronda.stop();
        d_->libreta.guardar(d_->archivo.toStdString());
    }
    emit autoaltaCambio();
}

QVariantMap PersonasViewModel::enLobby() const { return d_->enLobby; }

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
    std::vector<std::pair<QString, int>> gente; // uid, lugar
    for (const auto& v : espera->items()) {
        const QJsonObject o = v.toObject();
        const int lugar = o.value("lobbySlot").toInt();
        const QString uid = o.value("uid").toString();
        if (lugar > 0 && !uid.isEmpty() && o.value("connected").toBool()) gente.emplace_back(uid, lugar);
    }

    // Lo que se sabía de quien ya no está esperando deja de valer en el acto:
    // dejarlo puesto haría que la baldosa del próximo que ocupe su lugar
    // muestre el nombre del anterior.
    bool limpie = false;
    for (const QString& uid : d_->enLobby.keys()) {
        const bool sigue =
            std::any_of(gente.begin(), gente.end(), [&](const auto& g) { return g.first == uid; });
        if (!sigue) {
            d_->enLobby.remove(uid);
            limpie = true;
        }
    }
    if (limpie) emit enLobbyCambio();
    if (gente.empty()) return {};

    // Por turno, uno por vuelta: el costo es el mismo haya uno esperando o
    // haya diez.
    d_->turnoLobby = (d_->turnoLobby + 1) % static_cast<int>(gente.size());
    const auto& quien = gente[static_cast<std::size_t>(d_->turnoLobby)];
    uid = quien.first;
    lugar = quien.second;
    return QString("lobby:%1").arg(lugar);
}

QString PersonasViewModel::feedAuto() const { return d_->feedAuto; }

void PersonasViewModel::setFeedAuto(const QString& feed) {
    if (feed.isEmpty() || feed == d_->feedAuto) return;
    d_->feedAuto = feed;
    d_->pendientes.clear(); // las caras de la otra cámara no cuentan para esta
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
        const QString foto =
            p.foto.empty()
                ? QString()
                : QUrl::fromLocalFile(d_->carpetaFotos + "/" + QString::fromStdString(p.foto)).toString();
        out.append(
            QVariantMap{{"id", QString::fromStdString(p.id)},
                        {"nombre", QString::fromStdString(p.nombre)},
                        {"cargo", QString::fromStdString(p.cargo)},
                        {"nota", QString::fromStdString(p.nota)},
                        {"foto", foto},
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
        emit notify("Ponele un nombre: sin eso la ficha no sirve para nada.", "aviso");
        return {};
    }
    ba::rostros::Persona p;
    p.id = id.toStdString();
    p.nombre = nombre.trimmed().toStdString();
    p.cargo = cargo.trimmed().toStdString();
    p.nota = nota.trimmed().toStdString();
    const QString nuevo = QString::fromStdString(d_->libreta.guardarFicha(p));
    if (!d_->libreta.guardar(d_->archivo.toStdString())) {
        emit notify(QString::fromStdString(d_->libreta.error()), "error");
        return {};
    }
    emit cambio();
    return nuevo;
}

bool PersonasViewModel::borrar(const QString& id) {
    if (!d_->listo) return false;
    const auto* p = d_->libreta.porId(id.toStdString());
    const QString foto = p != nullptr ? QString::fromStdString(p->foto) : QString();
    if (!d_->libreta.borrar(id.toStdString())) return false;
    // La foto se va con la ficha: dejarla suelta sería guardar la cara de
    // alguien que se pidió borrar.
    if (!foto.isEmpty()) QFile::remove(d_->carpetaFotos + "/" + foto);
    d_->libreta.guardar(d_->archivo.toStdString());
    emit cambio();
    return true;
}

void PersonasViewModel::sumarCara(const QString& id, const QString& feed, bool nuevaFoto) {
    // Contesta por `notify` y no devolviendo un texto: mirar una cara ya no es
    // instantáneo —pasa por el otro hilo—, así que no hay nada que devolver
    // todavía cuando esta función termina.
    pedirAnalisis(feed, false,
                  [this, id, nuevaFoto](std::vector<Reconocedor::Visto> vistos, const QImage& img,
                                        const QString& error) {
                      if (!error.isEmpty()) return emit notify(error, "error");
                      if (vistos.empty()) return emit notify("No se ve ninguna cara ahí.", "aviso");
                      if (vistos.size() > 1)
                          return emit notify("Hay " + QString::number(vistos.size()) +
                                                 " caras en el cuadro: no adivino cuál es. Elegí una fuente "
                                                 "donde esté sola.",
                                             "aviso");
                      const auto alta = d_->libreta.sumarCara(id.toStdString(), vistos[0].vector);
                      if (alta == ba::rostros::Personas::Alta::NoExiste)
                          return emit notify("No existe esa persona.", "error");
                      const auto* p = d_->libreta.porId(id.toStdString());
                      if (p != nullptr && (p->foto.empty() || nuevaFoto)) {
                          const QString archivo = escribirFoto(img, vistos[0].cara, d_->carpetaFotos, id);
                          if (!archivo.isEmpty()) {
                              ba::rostros::Persona conFoto = *p;
                              conFoto.foto = archivo.toStdString();
                              d_->libreta.guardarFicha(conFoto);
                          }
                      }
                      persistir();
                      emit notify(alta == ba::rostros::Personas::Alta::Sumada
                                      ? "Cara sumada."
                                      : "Esa pose ya la tenía: probá desde otro ángulo.",
                                  alta == ba::rostros::Personas::Alta::Sumada ? "ok" : "aviso");
                  });
}

QVariantList PersonasViewModel::vistos() const { return d_->vistos; }

void PersonasViewModel::mirar(const QString& feed) {
    // Una cámara del estudio: que tenga su tarjeta mientras se la mira.
    if (feed.startsWith("entrada:")) {
        const bool otra = feed != d_->feedMirado;
        d_->feedMirado = feed;
        d_->miradoMs = QDateTime::currentMSecsSinceEpoch();
        if (otra) sincronizarRostros();
    } else if (d_->feedMirado.startsWith("entrada:")) {
        d_->feedMirado.clear();
        sincronizarRostros();
    }
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
    // Una sola vez por persona que espera: si el operador la rechaza a mano,
    // volver a hacerla pasar en la vuelta siguiente sería pelearle al operador.
    if (d_->yaPasaron.contains(uid)) return;
    if (parecido < kAbrirLaPuerta || !sirve) return; // ver kAbrirLaPuerta
    auto* ev = static_cast<EventoViewModel*>(d_->vm.evento());
    auto* conf = static_cast<ConferenceViewModel*>(d_->vm.conference());
    if (ev == nullptr || conf == nullptr) return;
    // El permiso vive en el PADRON del evento (`dejarPasarSolo`). Antes habia
    // una lista blanca aparte en el guion; dos listas para lo mismo se
    // desincronizan, asi que quedo una sola (ver EventoViewModel::migrarAutoAceptar).
    if (!ev->puedeEntrarSolo(personaId)) return;
    d_->yaPasaron.insert(uid);
    conf->admit(uid);
    // Y QUEDA ANOTADO QUE LLEGO. El que entra por la sala y el que entra por la
    // puerta van al mismo padron: si no, «quienes estan» solo sabe de la mitad
    // de la gente, que es el agujero que el padron vino a tapar.
    ev->acreditarPorPersona(personaId, "lobby");
    // El asistente la atiende DE OTRA MANERA: confirmarle lo que ya sabemos,
    // no preguntarle quién es. `marcarAtendido` evita además que el camino de
    // siempre la aborde con el cuestionario completo.
    conf->marcarAtendido(uid);
    d_->vm.assistantIntake(uid, nombre, true);
    // SE AVISA SIEMPRE. Que alguien entre a la sala sin que nadie lo decida en
    // el momento está bien si estaba en la lista, pero el operador tiene que
    // enterarse en el acto: es gente que aparece en su sala.
    emit notify((nombre.isEmpty() ? QString("Alguien de la lista del evento") : nombre) +
                    " entró sola: tiene permiso en el padrón del evento.",
                "info");
    ba::diag::registro().linea(ba::diag::Nivel::Info, "rostros", 0,
                               "auto-aceptada del evento: «" + nombre.toStdString() + "» (ficha " +
                                   personaId.toStdString() + ", lobby " + uid.toStdString() + ", parecido " +
                                   std::to_string(parecido) + ")");
}

void PersonasViewModel::acreditarEnLaPuerta(const QString& personaId, const QString& nombre) {
    auto* ev = static_cast<EventoViewModel*>(d_->vm.evento());
    if (ev == nullptr || ev->abierto().isEmpty()) return;
    // Acreditar de nuevo es inofensivo —no mueve la hora de la primera vez—,
    // pero AVISAR de nuevo no: alguien parado frente a la camara daria un aviso
    // por segundo. El aviso es una vez por persona; la constancia, siempre.
    const bool primera = !d_->avisadosPuerta.contains(personaId);
    if (!ev->acreditarPorPersona(personaId, "rostro")) {
        // Esta en la libreta pero no en la lista de este evento. NO se lo anota
        // solo: quien entra sin estar invitado es una decision del que
        // organiza, no del reconocimiento. Se avisa una vez y se sigue.
        if (primera) {
            d_->avisadosPuerta.insert(personaId);
            emit notify((nombre.isEmpty() ? QString("Alguien") : nombre) +
                            " llegó y no está en el padrón del evento.",
                        "warn");
        }
        return;
    }
    if (!primera) return;
    d_->avisadosPuerta.insert(personaId);
    emit notify((nombre.isEmpty() ? QString("Alguien de la libreta") : nombre) + " llegó.", "info");
    ba::diag::registro().linea(ba::diag::Nivel::Info, "rostros", 0,
                               "acreditado en la puerta: «" + nombre.toStdString() + "» (ficha " +
                                   personaId.toStdString() + ")");
}

void PersonasViewModel::resolver(const QString& feed, const QString& uid,
                                 std::vector<Reconocedor::Visto> caras, const QImage& img, bool registrar) {
    const std::int64_t ahora = QDateTime::currentMSecsSinceEpoch();

    // Las pendientes que no volvieron a aparecer se olvidan acá y no dentro
    // del lazo: si no se ve ninguna cara, igual hay que soltarlas.
    if (registrar) {
        auto& ps = d_->pendientes;
        ps.erase(
            std::remove_if(ps.begin(), ps.end(),
                           [&](const Impl::Pendiente& p) { return ahora - p.ultimoMs > kOlvidoPendienteMs; }),
            ps.end());
    }

    QVariantList out;
    bool alta = false;  // se creó una ficha o entró una cara: guardar ya
    bool vista = false; // solo «la vi recién»: guardar cada tanto
    for (const auto& visto : caras) {
        const ba::rostros::Cara& c = visto.cara;
        auto q = d_->libreta.buscar(visto.vector);
        bool seguro = q.parecido >= ba::rostros::kMismaPersona;
        if (registrar && !visto.vector.empty()) {
            if (seguro) {
                d_->libreta.vistaAhora(q.id, ahora);
                vista = true;
                // La pose de ahora, sumada a la ficha: es lo que hace que la
                // próxima vez la reconozca de costado, con otra luz o con
                // anteojos. La libreta descarta las que no aportan.
                if (d_->libreta.sumarCara(q.id, visto.vector) == ba::rostros::Personas::Alta::Sumada)
                    alta = true;
            } else {
                const QString nuevo = anotarDesconocida(visto.vector, img, c, ahora);
                if (!nuevo.isEmpty()) {
                    q.id = nuevo.toStdString();
                    q.nombre.clear();
                    q.parecido = 1.0F;
                    seguro = true;
                    alta = true;
                }
            }
        }
        out.append(QVariantMap{{"id", seguro ? QString::fromStdString(q.id) : QString()},
                               {"nombre", seguro ? QString::fromStdString(q.nombre) : QString()},
                               {"parecido", q.parecido},
                               {"x", c.x},
                               {"y", c.y},
                               {"ancho", c.ancho},
                               {"alto", c.alto},
                               // El tamaño del cuadro del que salieron: sin eso
                               // el recuadro no se puede poner donde va. La
                               // vista no sabe cuánto mide el video (cada
                               // fuente mide distinto) ni dónde lo dejaron las
                               // bandas negras del encuadre.
                               {"cw", img.width()},
                               {"ch", img.height()}});
    }

    if (alta || (vista && ahora - d_->ultimoGuardado > kGuardarVistasMs)) {
        d_->libreta.guardar(d_->archivo.toStdString());
        d_->ultimoGuardado = ahora;
    }
    // --- La puerta ---------------------------------------------------------
    // Reconocer a alguien en la entrada del lugar y dejar constancia. Es el
    // mismo reconocimiento del lobby apuntado al mundo fisico, y es lo que hace
    // que «quienes estan» sepa de las dos mitades de la gente.
    if (d_->puertaAuto && !d_->feedPuerta.isEmpty() && feed == d_->feedPuerta) {
        for (std::size_t i = 0; i < caras.size(); ++i) {
            const QVariantMap v = out.at(static_cast<int>(i)).toMap();
            const QString id = v.value("id").toString();
            if (id.isEmpty()) continue;
            // El mismo margen que para abrir la puerta de la sala, y por la
            // misma razon: acreditar al que no es deja a dos personas mal
            // anotadas —una que no vino y otra que si— y nadie lo descubre
            // hasta que hay que contar el quorum.
            if (v.value("parecido").toFloat() < kAbrirLaPuerta) continue;
            if (!calidadDe(img, caras[i].cara).sirve) continue;
            acreditarEnLaPuerta(id, v.value("nombre").toString());
        }
    }

    if (feed.startsWith("lobby:")) {
        // Lo del lobby no va a la lista de la página de Personas: va a la
        // baldosa de quien está esperando, que es donde el operador lo está
        // mirando. UNA cara por posición, la más grande: si se ven dos, la que
        // importa es la de quien está sentado frente a la cámara, no la de
        // quien pasa por atrás.
        // Se guarda por la PERSONA, no por el número de posición: las
        // posiciones se reciclan y lo que se supo de una se le pegaría al
        // siguiente que la ocupe.
        if (uid.isEmpty()) return; // no se sabe de quién era esa cámara
        int mejor = -1;
        for (int i = 0; i < out.size(); ++i)
            if (mejor < 0 || caras[static_cast<std::size_t>(i)].cara.alto >
                                 caras[static_cast<std::size_t>(mejor)].cara.alto)
                mejor = i;
        if (mejor < 0) {
            d_->enLobby.remove(uid);
        } else {
            QVariantMap v = out.at(mejor).toMap();
            // Lo que hace falta para PEDIRLE algo: si la cara sirve y, si no,
            // qué decirle. Sin esto la baldosa solo sabría decir «no sé».
            const Calidad q = calidadDe(img, caras[static_cast<std::size_t>(mejor)].cara);
            v["sirve"] = q.sirve;
            v["sugerencia"] = QString::fromStdString(q.sugerencia);
            const QString id = v.value("id").toString();
            const auto* f = id.isEmpty() ? nullptr : d_->libreta.porId(id.toStdString());
            v["cargo"] = f != nullptr ? QString::fromStdString(f->cargo) : QString();
            v["caras"] = f != nullptr ? static_cast<int>(f->caras.size()) : 0;
            v["foto"] =
                f != nullptr && !f->foto.empty()
                    ? QUrl::fromLocalFile(d_->carpetaFotos + "/" + QString::fromStdString(f->foto)).toString()
                    : QString();
            // El lugar va ADENTRO: la clave del mapa es la persona, así que
            // sin esto se perdía de qué cámara salió y `lobby:<clave>` daba un
            // feed que no existe.
            v["uid"] = uid;
            v["lugar"] = feed.mid(6).toInt();
            d_->enLobby[uid] = v;
            if (!id.isEmpty())
                dejarPasarSiCorresponde(uid, id, v.value("nombre").toString(), v.value("parecido").toFloat(),
                                        q.sirve);
        }
        emit enLobbyCambio();
    } else {
        d_->vistos = out;
        emit vistosCambio();
    }
    // La lista de fichas solo se avisa cuando cambió de verdad. Avisar en cada
    // vuelta la rearmaría mientras alguien la está editando.
    if (alta) emit cambio();
}

QString PersonasViewModel::anotarDesconocida(const std::vector<float>& v, const QImage& img,
                                             const ba::rostros::Cara& c, std::int64_t ahora) {
    for (auto& p : d_->pendientes) {
        if (ba::rostros::parecido(p.v, v) < ba::rostros::kMismaPersona) continue;
        p.v = v; // el último visto: la cara suele ir mejorando al acercarse
        p.ultimoMs = ahora;
        if (++p.veces < kVecesParaAnotar) return {};

        if (static_cast<int>(d_->libreta.todas().size()) >= kMaxFichasAuto) {
            d_->aviso = QString("La libreta llegó a %1 fichas: apagué el anotado automático para "
                                "no llenarla de desconocidos. Completá o borrá las que sobran.")
                            .arg(kMaxFichasAuto);
            setAutoalta(false);
            emit notify(d_->aviso, "aviso");
            return {};
        }
        // SIN NOMBRE A PROPOSITO. El sistema puede decir «este que está acá es
        // el mismo de recién»; quién es lo dice una persona. Una ficha sin
        // nombre no rotula nada: es una fila esperando que la completen.
        ba::rostros::Persona nueva;
        nueva.vistaMs = ahora;
        const std::string id = d_->libreta.guardarFicha(nueva);
        d_->libreta.sumarCara(id, v);
        const QString foto = escribirFoto(img, c, d_->carpetaFotos, QString::fromStdString(id));
        if (!foto.isEmpty()) {
            ba::rostros::Persona conFoto = *d_->libreta.porId(id);
            conFoto.foto = foto.toStdString();
            d_->libreta.guardarFicha(conFoto);
        }
        d_->pendientes.erase(d_->pendientes.begin() + (&p - d_->pendientes.data()));
        return QString::fromStdString(id);
    }
    d_->pendientes.push_back(Impl::Pendiente{v, 1, ahora});
    return {};
}

QString PersonasViewModel::etiquetaDe(const QString& feed) const {
    if (feed.startsWith("entrada:")) return "Cámara · " + feed.mid(8);
    return static_cast<FrameService*>(d_->vm.frames())->label(feed);
}

QVariantList PersonasViewModel::fuentes() const {
    QVariantList out;
    auto* frames = static_cast<FrameService*>(d_->vm.frames());
    for (const QString& k : frames->keys())
        out.append(QVariantMap{{"clave", k}, {"etiqueta", frames->label(k)}});
    // Las cámaras del estudio (las entradas del grafo de video).
    if (auto* vg = d_->vm.videoGrafos())
        for (const QString& e : vg->entradas())
            out.append(QVariantMap{{"clave", "entrada:" + e}, {"etiqueta", "Cámara · " + e}});
    return out;
}
#endif // BA_HAVE_ROSTROS

} // namespace ba::app
