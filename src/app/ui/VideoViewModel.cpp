// src/app/ui/VideoViewModel.cpp — ver el .hpp

#include "ui/VideoViewModel.hpp"

#include "ui/ResumenNodo.hpp"

#include "emision/VentanillaMotor.hpp"
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <vector>

namespace ba::app {

using json = nlohmann::json;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("v")
        .toVariant();
}
QStringList listaDeTextos(const json& j) {
    QStringList out;
    if (j.is_array())
        for (const auto& x : j)
            if (x.is_string()) out << q(x.get<std::string>());
    return out;
}

const json& vacio() {
    static const json v = json::object();
    return v;
}

/// `j[k]` sin copiar (y sin crearlo): la nada si no es un objeto o no lo tiene.
const json& parte(const json& j, const std::string& k) {
    static const json nada;
    if (!j.is_object()) return nada;
    const auto it = j.find(k);
    return it != j.end() ? *it : nada;
}

} // namespace

VideoViewModel::VideoViewModel(VentanillaMotor& ventanilla, video::VideoGrafos& grafos, QObject* parent)
    : QObject(parent), ventanilla_(ventanilla), grafos_(grafos) {
    ventanilla_.escuchar(this, [this](unsigned partes) { alCambiar(partes); });
    // Lo que avisa VideoGrafos desde el dueño llega encolado (vive en otro hilo).
    connect(&grafos_, &video::VideoGrafos::gstListo, this, &VideoViewModel::gstListo);
    connect(&grafos_, &video::VideoGrafos::medicion, this, &VideoViewModel::medicion);
    connect(&grafos_, &video::VideoGrafos::superresTarde, this, &VideoViewModel::superresTarde);
    releer();
}

VideoViewModel::~VideoViewModel() = default;

const json& VideoViewModel::v() const {
    const json& x = ventanilla_.foto().video;
    return x.is_object() ? x : vacio();
}

void VideoViewModel::alCambiar(unsigned partes) {
    if ((partes & FotoMotor::Video) != 0) releer();
}

void VideoViewModel::releer() {
    const json& x = v();
    const json& antes = antes_.get();
    // Lo que redibuja el patchbay (entradas y grafos), aparte de lo que se mueve siempre.
    // Sin copiar cada parte (llega seguido: la red, el reloj, la latencia).
    const auto difiere = [&](const char* k) { return parte(x, k) != parte(antes, k); };
    const bool estructura = difiere("entradas") || difiere("info") || difiere("grafos") || difiere("estudio");
    // Lo que se mueve: cada propiedad avisa sola (el reloj o la red no redibujan el automixer).
    std::vector<void (VideoViewModel::*)()> avisar;
    // El valor nuevo se arma solo si esa parte cambió (convertir todo en cada foto era lo caro).
    const auto poner = [&](const char* k, auto& campo, auto armar, void (VideoViewModel::*senal)()) {
        if (!difiere(k)) return;
        auto nuevo = armar();
        if (campo == nuevo) return;
        campo = std::move(nuevo);
        avisar.push_back(senal);
    };
    // Lo que crece con las entradas (la red y el diagnóstico de cada una, los formularios…)
    // NI SE CONVIERTE: se avisa y se convierte recién cuando alguien lo lee (la vista que lo
    // muestra), de la vista guardada. Con 200 invitados, convertirlo en cada foto (JSON →
    // texto → QVariant de todas las entradas) era lo que más pesaba de este oyente.
    const auto perezosa = [&](const char* k, auto& cache, void (VideoViewModel::*senal)()) {
        if (!difiere(k)) return;
        cache.reset();
        avisar.push_back(senal);
    };
    perezosa("red", estadoRed_, &VideoViewModel::redCambio);
    perezosa("diagnostico", diagnostico_, &VideoViewModel::diagnosticoCambio);
    perezosa("programador", programador_, &VideoViewModel::programadorCambio);
    perezosa("reloj", reloj_, &VideoViewModel::relojCambio);
    perezosa("latencia", latencia_, &VideoViewModel::latenciaCambio);
    perezosa("automixer", automixer_, &VideoViewModel::automixerCambio);
    perezosa("filtros_audio", filtrosAudio_, &VideoViewModel::filtrosAudioCambio);
    perezosa("formularios", formularios_, &VideoViewModel::formulariosCambio);
    poner("escenas", escenas_, [&] { return listaDeTextos(parte(x, "escenas")); }, &VideoViewModel::escenasCambio);
    poner("hay_superres", haySuperres_, [&] { return parte(x, "hay_superres").is_boolean() && parte(x, "hay_superres").get<bool>(); },
          &VideoViewModel::haySuperresCambio);
    const bool eventos = difiere("eventos");
    // Los estados de las entradas (los tamaños que llegan): solo los que cambiaron.
    const json objetoVacio = json::object();
    if (tamanosOrigen_.releer(parte(x, "estados"), parte(antes, "estados"), difiere("entradas")))
        avisar.push_back(&VideoViewModel::tamanosOrigenCambio);
    // La anterior, compartida (un puntero: copiar la vista entera en cada foto era O(tamaño)).
    // (Desde acá `antes` ya no vale.)
    antes_ = ventanilla_.foto().video;
    const auto deLaElegida = [&](const char* k) -> json {
        const json& e = parte(parte(x, k), entrada_.toStdString());
        return e.is_object() ? e : objetoVacio;
    };
    if (estructura) {
        entradas_ = listaDeTextos(parte(x, "entradas"));
        entradasInfo_ = aVariant(parte(x, "info").is_array() ? parte(x, "info") : json::array()).toList();
        estudio_.reset(); // (se convierte cuando se lee: todo el estudio, con los resúmenes)
        // La elegida: la que se eligió acá; si no hay o se fue, la primera.
        if ((entrada_.isEmpty() || !entradas_.contains(entrada_)) && !entradas_.isEmpty()) entrada_ = entradas_.first();
        grafo_ = entrada_.isEmpty() ? QVariantMap{} : aVariant(conResumenes(deLaElegida("grafos"))).toMap();
    }
    // Los eventos son los de la elegida (que puede haber cambiado con la estructura).
    if (eventos || estructura) {
        QVariantMap e = aVariant(deLaElegida("eventos")).toMap();
        if (e != eventos_) {
            eventos_ = std::move(e);
            avisar.push_back(&VideoViewModel::eventosCambio);
        }
    }
    if (estructura) emit cambio();
    for (const auto senal : avisar) emit(this->*senal)();
}

// --- Lo que se convierte al leerlo (de la vista de la última foto, `antes_`) -----------

namespace {
template <class T, class F>
const T& perezoso(std::optional<T>& cache, F armar) {
    if (!cache) cache = armar();
    return *cache;
}
} // namespace

QVariantList VideoViewModel::estadoRed() const {
    return perezoso(estadoRed_, [&] { return aVariant(parte(antes_.get(), "red")).toList(); });
}
QVariantList VideoViewModel::diagnostico() const {
    return perezoso(diagnostico_, [&] { return aVariant(parte(antes_.get(), "diagnostico")).toList(); });
}
QVariantMap VideoViewModel::programador() const {
    return perezoso(programador_, [&] { return aVariant(parte(antes_.get(), "programador")).toMap(); });
}
QVariantMap VideoViewModel::reloj() const {
    return perezoso(reloj_, [&] { return aVariant(parte(antes_.get(), "reloj")).toMap(); });
}
QVariantMap VideoViewModel::latencia() const {
    return perezoso(latencia_, [&] { return aVariant(parte(antes_.get(), "latencia")).toMap(); });
}
QVariantMap VideoViewModel::automixer() const {
    return perezoso(automixer_, [&] { return aVariant(parte(antes_.get(), "automixer")).toMap(); });
}
QVariantMap VideoViewModel::filtrosAudio() const {
    return perezoso(filtrosAudio_, [&] { return aVariant(parte(antes_.get(), "filtros_audio")).toMap(); });
}
QVariantMap VideoViewModel::formularios() const {
    return perezoso(formularios_, [&] { return armarFormularios(parte(antes_.get(), "formularios")); });
}
QVariantMap VideoViewModel::estudio() const {
    return perezoso(estudio_, [&] {
        const json& e = parte(antes_.get(), "estudio");
        return aVariant(conResumenes(e.is_object() ? e : json::object())).toMap();
    });
}

void VideoViewModel::seleccionar(const QString& entrada) {
    if (entrada == entrada_) return;
    entrada_ = entrada;
    const auto deLaElegida = [&](const char* k) -> json {
        const json& e = parte(parte(v(), k), entrada_.toStdString());
        return e.is_object() ? e : json::object();
    };
    grafo_ = aVariant(conResumenes(deLaElegida("grafos"))).toMap();
    eventos_ = aVariant(deLaElegida("eventos")).toMap();
    emit cambio();
    emit eventosCambio();
    // VideoGrafos también la sabe: las herramientas sin entrada usan la elegida.
    ventanilla_.pedir("video_elegir", [&g = grafos_, e = entrada]() { g.seleccionar(e); });
}

void VideoViewModel::setVista(const QString& v) {
    if (v == vista_ || (v != "racks" && v != "estudio")) return;
    vista_ = v;
    emit vistaCambio();
}

QVariantList VideoViewModel::catalogo() { return video::VideoGrafos::catalogo(); }
QVariantList VideoViewModel::virtuales() { return video::VideoGrafos::virtuales(); }

std::shared_ptr<const video::CatalogoGst> VideoViewModel::gst() const { return grafos_.catalogoGst(); }

QVariantList VideoViewModel::modulosGst() const { return aVariant(gst()->modulos()).toList(); }
QVariantList VideoViewModel::modulosAudio() const { return aVariant(gst()->modulosAudio()).toList(); }
QString VideoViewModel::versionGst() const {
    const auto c = gst();
    return c->disponible() ? q(c->version()) : tr("Sin GStreamer: %1").arg(q(c->error()));
}

QVariantMap VideoViewModel::armarFormularios(const json& formularios) const {
    // De la vista: el elemento y los valores con que corre cada tarjeta (un controlador, los
    // de su molde) y los de cada pad de una Mezcla.
    QVariantMap out;
    if (!formularios.is_object()) return out;
    const auto c = gst();
    for (auto it = formularios.begin(); it != formularios.end(); ++it) {
        QVariantMap pads;
        if (const auto pf = it->find("pads"); pf != it->end() && pf->is_object())
            for (auto p = pf->begin(); p != pf->end(); ++p) pads.insert(q(p.key()), video::VideoGrafos::controlesDe(*c, *it, p.key()));
        out.insert(q(it.key()), QVariantMap{{"props", video::VideoGrafos::controlesDe(*c, *it)}, {"pads", pads}});
    }
    return out;
}

QVariantList VideoViewModel::propiedadesModulo(const QString& elemento) const {
    return video::VideoGrafos::controlesGst(gst()->propiedades(elemento.toStdString(), json::object()));
}

QVariantList VideoViewModel::propiedadesCon(const video::CatalogoGst& c, const QString& elemento, const QVariantMap& valores) {
    // Los valores guardados vienen como texto (así van a GStreamer): se pasan al tipo de cada propiedad.
    json act = json::object();
    for (auto it = valores.begin(); it != valores.end(); ++it) {
        const QString t = it.value().toString();
        bool okN = false;
        const double n = t.toDouble(&okN);
        if (t == "true" || t == "false")
            act[it.key().toStdString()] = t == "true";
        else if (okN)
            act[it.key().toStdString()] = n;
        else
            act[it.key().toStdString()] = t.toStdString();
    }
    return video::VideoGrafos::controlesGst(c.propiedades(elemento.toStdString(), act));
}

void EditorModulo::setVideo(QObject* v) {
    auto* vm = qobject_cast<VideoViewModel*>(v);
    if (vm == video_) return;
    if (video_) disconnect(video_, nullptr, this, nullptr);
    video_ = vm;
    // El catálogo de GStreamer se arma al arrancar: cuando está, de nuevo.
    if (vm != nullptr) connect(vm, &VideoViewModel::gstListo, this, &EditorModulo::armar);
    emit entradaCambio();
    armar();
}

void EditorModulo::setElemento(const QString& e) {
    if (e == elemento_) return;
    elemento_ = e;
    emit entradaCambio();
    poner({}); // las del anterior no son de éste
    armar();
}

void EditorModulo::setValores(const QVariantMap& v) {
    if (v == valores_) return;
    valores_ = v;
    emit entradaCambio();
    armar();
}

void EditorModulo::poner(const QVariantList& p) {
    if (p == propiedades_) return;
    propiedades_ = p;
    emit propiedadesCambio();
}

void EditorModulo::armar() {
    const unsigned n = ++pedido_;
    if (!video_ || elemento_.isEmpty()) return poner({});
    std::shared_ptr<const video::CatalogoGst> c = video_->gst();
    if (!c) return poner({});
    // Preguntarle a GStreamer crea el elemento (la primera vez): en el fondo.
    QThreadPool::globalInstance()->start([yo = QPointer<EditorModulo>(this), c, n, e = elemento_, v = valores_]() {
        const QVariantList p = VideoViewModel::propiedadesCon(*c, e, v);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, n, p]() {
                if (yo && yo->pedido_ == n) yo->poner(p);
            },
            Qt::QueuedConnection);
    });
}

// --- Pedir ---------------------------------------------------------------------------

void VideoViewModel::pedir(const char* que, std::function<std::string(video::VideoGrafos&)> orden,
                           std::function<void(const std::string&)> listo) {
    ventanilla_.pedir(
        this, que,
        [&g = grafos_, orden = std::move(orden)]() {
            std::string e = orden(g);
            // Lo que cambió, a la vista (y a la foto, que la interfaz recibe antes que esto).
            g.publicarVista();
            return e;
        },
        [this, listo = std::move(listo)](const std::string& e) {
            if (listo) return listo(e);
            if (!e.empty()) emit notify(q(e), "error");
        });
}

QString VideoViewModel::pedirQ(const char* que, std::function<QString(video::VideoGrafos&)> orden) {
    pedir(que, [orden = std::move(orden)](video::VideoGrafos& g) { return orden(g).toStdString(); });
    return {};
}

QString VideoViewModel::pedirEnLaElegida(const char* que, std::function<QString(video::VideoGrafos&)> orden) {
    return pedirQ(que, [e = entrada_, orden = std::move(orden)](video::VideoGrafos& g) {
        g.seleccionar(e);
        return orden(g);
    });
}

void VideoViewModel::capturarCuadroAparte(const std::string& entrada, std::function<void(QImage)> listo) {
    // La captura la pide el dueño; el cuadro llega a la interfaz (VideoGrafos::capturarCuadroAparte).
    ventanilla_.pedir("video_cuadro", [&g = grafos_, entrada, listo = std::move(listo)]() mutable {
        g.capturarCuadroAparte(entrada, std::move(listo));
    });
}

void VideoViewModel::sincronizar() {
    ventanilla_.pedir("video_revisar", [&g = grafos_]() { g.sincronizar(); });
}

void VideoViewModel::agregar(const QString& tipo, const QString& despues) {
    avisarError(pedirEnLaElegida("video_agregar", [tipo, despues](video::VideoGrafos& g) { return g.agregar(tipo, despues); }));
}
void VideoViewModel::agregarModulo(const QString& elemento, const QString& despues) {
    avisarError(pedirEnLaElegida("video_agregar", [elemento, despues](video::VideoGrafos& g) { return g.agregarModulo(elemento, despues); }));
}
void VideoViewModel::agregarMezcla(const QString& elemento) {
    avisarError(pedirQ("video_agregar", [elemento](video::VideoGrafos& g) { return g.agregarMezcla(elemento); }));
}
void VideoViewModel::ajustarPad(const QString& id, const QString& puerto, const QString& clave, const QVariant& valor) {
    avisarError(pedirQ("video_ajustar", [id, puerto, clave, valor](video::VideoGrafos& g) { return g.ajustarPad(id, puerto, clave, valor); }));
}
void VideoViewModel::agregarSuperresUi(const QString& motor, const QString& modelo, const QString& despues) {
    return avisarError(pedirEnLaElegida("video_agregar", [motor, modelo, despues](video::VideoGrafos& g) {
        return g.agregarSuperresUi(motor, modelo, despues);
    }));
}
bool VideoViewModel::crearVirtual(const QString& nombre, const QString& preset, const QString& escena) {
    avisarError(pedirQ("video_virtual", [nombre, preset, escena](video::VideoGrafos& g) { return g.crearVirtual(nombre, preset, escena); }));
    return true;
}
void VideoViewModel::quitar(const QString& id) {
    avisarError(pedirQ("video_quitar", [id](video::VideoGrafos& g) { return g.quitar(id); }));
}
void VideoViewModel::reubicar(const QString& id, const QString& despues) {
    avisarError(pedirQ("video_mover", [id, despues](video::VideoGrafos& g) { return g.reubicar(id, despues); }));
}
void VideoViewModel::correr(const QString& id, int paso) {
    avisarError(pedirQ("video_mover", [id, paso](video::VideoGrafos& g) { return g.correr(id, paso); }));
}
void VideoViewModel::conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada) {
    avisarError(pedirQ("video_cable", [desde, salida, hacia, entrada](video::VideoGrafos& g) { return g.conectar(desde, salida, hacia, entrada); }));
}
void VideoViewModel::desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada) {
    avisarError(pedirQ("video_cable",
                  [desde, salida, hacia, entrada](video::VideoGrafos& g) { return g.desconectar(desde, salida, hacia, entrada); }));
}
void VideoViewModel::mover(const QString& id, int x, int y) {
    avisarError(pedirQ("video_mover", [id, x, y](video::VideoGrafos& g) { return g.mover(id, x, y); }));
}
void VideoViewModel::ajustar(const QString& id, const QVariantMap& params) {
    avisarError(pedirQ("video_ajustar", [id, params](video::VideoGrafos& g) { return g.ajustar(id, params); }));
}
void VideoViewModel::controlar(const QString& id) {
    avisarError(pedirQ("video_controlador", [id](video::VideoGrafos& g) { return g.controlar(id); }));
}
void VideoViewModel::agregarControlada(const QString& control, const QString& entrada) {
    avisarError(pedirQ("video_controlador", [control, entrada](video::VideoGrafos& g) { return g.agregarControlada(control, entrada); }));
}
void VideoViewModel::soltarControl(const QString& id) {
    avisarError(pedirQ("video_controlador", [id](video::VideoGrafos& g) { return g.soltarControl(id); }));
}
void VideoViewModel::ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor) {
    avisarError(pedirQ("video_ajustar", [id, clave, valor](video::VideoGrafos& g) { return g.ajustarFiltro(id, clave, valor); }));
}
void VideoViewModel::aEscena(const QString& id, const QString& escena) {
    avisarError(pedirQ("video_a_escena", [id, escena](video::VideoGrafos& g) { return g.aEscena(id, escena); }));
}
void VideoViewModel::limpiar() {
    avisarError(pedirEnLaElegida("video_limpiar", [](video::VideoGrafos& g) { return g.limpiar(); }));
}
void VideoViewModel::ordenar() {
    avisarError(pedirQ("video_ordenar", [](video::VideoGrafos& g) { return g.ordenar(); }));
}
bool VideoViewModel::configurarRelojUi(const QVariantMap& cambios) {
    return sinError(pedirQ("video_reloj", [cambios](video::VideoGrafos& g) { return g.configurarRelojUi(cambios); }));
}
void VideoViewModel::medirUi(const QString& a, const QString& b, bool aplicar) {
    // El resultado llega por VideoGrafos::medicion (conectada a la nuestra).
    ventanilla_.pedir("video_medir", [&g = grafos_, a, b, aplicar]() { g.medirUi(a, b, aplicar); });
}
bool VideoViewModel::desfaseUi(const QString& entrada, double ms) {
    return sinError(pedirQ("video_desfase", [entrada, ms](video::VideoGrafos& g) { return g.desfaseUi(entrada, ms); }));
}
bool VideoViewModel::superresUi(const QString& entrada, bool activa) {
    return sinError(pedirQ("video_superres", [entrada, activa](video::VideoGrafos& g) { return g.superresUi(entrada, activa); }));
}
bool VideoViewModel::sincroniaUi(const QString& entrada, const QString& modo) {
    return sinError(pedirQ("video_sincronia", [entrada, modo](video::VideoGrafos& g) { return g.sincroniaUi(entrada, modo); }));
}
void VideoViewModel::ponerTopeEstudioUi(int tope) {
    pedir("video_latencia", [tope](video::VideoGrafos& g) {
        g.ponerTopeEstudioUi(tope);
        return std::string();
    });
}
bool VideoViewModel::claseSincroniaUi(const QString& entrada, const QString& clase) {
    return sinError(pedirQ("video_sincronia", [entrada, clase](video::VideoGrafos& g) { return g.claseSincroniaUi(entrada, clase); }));
}
bool VideoViewModel::bajarLatenciaUi() {
    // «Ya está en lo que piden» no es un error: va como aviso.
    pedir("video_latencia", [](video::VideoGrafos& g) { return g.bajarLatenciaUi().toStdString(); },
          [this](const std::string& e) {
              if (!e.empty()) emit notify(q(e), "info");
          });
    return true;
}
void VideoViewModel::automixerPonerUi(const QString& entrada, const QString& grupo) {
    avisarError(pedirQ("video_automixer", [entrada, grupo](video::VideoGrafos& g) { return g.automixerPonerUi(entrada, grupo); }));
}
void VideoViewModel::automixerAjustesUi(const QString& grupo, const QVariantMap& ajustes) {
    avisarError(pedirQ("video_automixer", [grupo, ajustes](video::VideoGrafos& g) { return g.automixerAjustesUi(grupo, ajustes); }));
}
bool VideoViewModel::configurarProgramadorUi(const QVariantMap& cambios) {
    return sinError(pedirQ("video_programador", [cambios](video::VideoGrafos& g) { return g.configurarProgramadorUi(cambios); }));
}
bool VideoViewModel::agregarFiltroAudioUi(const QString& entrada, const QString& cual) {
    return sinError(pedirQ("video_filtro_audio", [entrada, cual](video::VideoGrafos& g) { return g.agregarFiltroAudioUi(entrada, cual); }));
}
bool VideoViewModel::quitarFiltroAudioUi(const QString& entrada, int indice) {
    return sinError(pedirQ("video_filtro_audio", [entrada, indice](video::VideoGrafos& g) { return g.quitarFiltroAudioUi(entrada, indice); }));
}
bool VideoViewModel::ponerFiltrosAudioUi(const QString& entrada, const QVariantList& filtros) {
    return sinError(pedirQ("video_filtro_audio", [entrada, filtros](video::VideoGrafos& g) { return g.ponerFiltrosAudioUi(entrada, filtros); }));
}
void VideoViewModel::superresMotorUi(const QString& entrada, const QString& motor, const QString& modelo) {
    avisarError(pedirQ("video_superres", [entrada, motor, modelo](video::VideoGrafos& g) { return g.superresMotorUi(entrada, motor, modelo); }));
}
bool VideoViewModel::nuevaEntradaRed(const QString& nombre, const QString& uri, const QString& escena, int latenciaMs,
                                        bool normalizar) {
    return sinError(pedirQ("video_entrada_red", [nombre, uri, escena, latenciaMs, normalizar](video::VideoGrafos& g) {
        return g.nuevaEntradaRed(nombre, uri, escena, latenciaMs, normalizar);
    }));
    return true;
}
void VideoViewModel::presetPanoramicaUi(const QString& id, const QString& preset, double campo) {
    avisarError(pedirQ("video_panoramica", [id, preset, campo](video::VideoGrafos& g) { return g.presetPanoramicaUi(id, preset, campo); }));
}
void VideoViewModel::aplicarDisposicionUi(const QString& compositor, const QString& disposicion) {
    avisarError(pedirQ("video_mosaico",
                  [compositor, disposicion](video::VideoGrafos& g) { return g.aplicarDisposicionUi(compositor, disposicion); }));
}

// --- Para los demás modelos (de la foto) ---------------------------------------------

const json* VideoViewModel::estadoDe(const std::string& entrada) const {
    // Sin copiar el mapa de estados (se recorre por entrada: copiarlo entero en cada una
    // era O(N²) con cada foto).
    const json& st = parte(parte(v(), "estados"), entrada);
    return st.is_object() ? &st : nullptr;
}

json VideoViewModel::estadoEntrada(const std::string& entrada) const {
    if (const json* st = estadoDe(entrada)) return *st;
    return {{"error", tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString()}};
}

bool VideoViewModel::esEntrada(const std::string& nombre) const { return entradas_.contains(q(nombre)); }

std::string VideoViewModel::uriDe(const std::string& entrada) const {
    const json& f = parte(ventanilla_.foto().fuentesVideo.get(), entrada);
    if (!f.is_object() || f.value("tipo", std::string()) != "ba_gst_fuente") return {};
    const json& a = parte(f, "ajustes");
    return a.is_object() ? a.value("uri", std::string()) : std::string();
}

json VideoViewModel::entradasDeHost(const std::string& host) const {
    json out = json::array();
    for (const auto& [n, f] : ventanilla_.foto().fuentesVideo.items()) {
        if (!f.is_object() || f.value("tipo", std::string()) != "ba_gst_fuente") continue;
        const json& a = parte(f, "ajustes");
        const std::string uri = a.is_object() ? a.value("uri", std::string()) : std::string();
        const QUrl u(q(uri));
        if (!uri.empty() && u.host().toStdString() == host) {
            QUrl sin = u;
            sin.setUserInfo({});
            out.push_back({{"entrada", n}, {"url", sin.toString().toStdString()}});
        }
    }
    return out;
}

json VideoViewModel::programadorJson() const { return v().value("programador", json::object()); }

std::vector<std::pair<int, int>> VideoViewModel::tamanosDeEntradas() const {
    std::vector<std::pair<int, int>> out;
    const json& tamanos = parte(v(), "tamanos");
    if (!tamanos.is_array()) return out;
    for (const auto& t : tamanos)
        if (t.is_array() && t.size() == 2) out.emplace_back(t[0].get<int>(), t[1].get<int>());
    return out;
}

json VideoViewModel::nodos() const { return v().value("nodos", json::array()); }
std::string VideoViewModel::legalizar() const { return v().value("legalizar", std::string("no")); }
json VideoViewModel::estudioJson() const { return v().value("estudio", json::object()); }

} // namespace ba::app
