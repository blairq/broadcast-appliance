// src/app/ui/VideoViewModel.cpp — ver el .hpp

#include "ui/VideoViewModel.hpp"

#include "emision/VentanillaMotor.hpp"
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

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

} // namespace

VideoViewModel::VideoViewModel(VentanillaMotor& ventanilla, video::VideoGrafos& grafos, QObject* parent)
    : QObject(parent), ventanilla_(ventanilla), grafos_(grafos) {
    connect(&ventanilla_, &VentanillaMotor::cambio, this, &VideoViewModel::alCambiar);
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
    // Lo que redibuja el patchbay (entradas y grafos), aparte de lo que se mueve siempre.
    const auto difiere = [&](const char* k) { return x.value(k, json()) != antes_.value(k, json()); };
    const bool estructura = difiere("entradas") || difiere("info") || difiere("grafos") || difiere("estudio");
    const bool estado = difiere("estados") || difiere("red") || difiere("diagnostico") || difiere("eventos") ||
                        difiere("programador") || difiere("reloj") || difiere("latencia") || difiere("automixer") ||
                        difiere("filtros_audio") || difiere("escenas") || difiere("hay_superres");
    antes_ = x;
    if (estructura) {
        entradas_ = listaDeTextos(x.value("entradas", json::array()));
        entradasInfo_ = aVariant(x.value("info", json::array())).toList();
        estudio_ = aVariant(x.value("estudio", json::object())).toMap();
        // La elegida: la que se eligió acá; si no hay o se fue, la primera.
        if ((entrada_.isEmpty() || !entradas_.contains(entrada_)) && !entradas_.isEmpty()) entrada_ = entradas_.first();
        grafo_ = entrada_.isEmpty() ? QVariantMap{}
                                    : aVariant(x.value("grafos", json::object()).value(entrada_.toStdString(), json::object())).toMap();
    }
    if (estado || estructura) {
        estadoRed_ = aVariant(x.value("red", json::array())).toList();
        diagnostico_ = aVariant(x.value("diagnostico", json::array())).toList();
        eventos_ = aVariant(x.value("eventos", json::object()).value(entrada_.toStdString(), json::object())).toMap();
        programador_ = aVariant(x.value("programador", json::object())).toMap();
        reloj_ = aVariant(x.value("reloj", json::object())).toMap();
        latencia_ = aVariant(x.value("latencia", json::object())).toMap();
        automixer_ = aVariant(x.value("automixer", json::object())).toMap();
        filtrosAudio_ = aVariant(x.value("filtros_audio", json::object())).toMap();
        escenas_ = listaDeTextos(x.value("escenas", json::array()));
        haySuperres_ = x.value("hay_superres", false);
    }
    if (estructura) emit cambio();
    if (estado || estructura) emit estadoCambio();
}

void VideoViewModel::seleccionar(const QString& entrada) {
    if (entrada == entrada_) return;
    entrada_ = entrada;
    grafo_ = aVariant(v().value("grafos", json::object()).value(entrada_.toStdString(), json::object())).toMap();
    eventos_ = aVariant(v().value("eventos", json::object()).value(entrada_.toStdString(), json::object())).toMap();
    emit cambio();
    emit estadoCambio();
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

QVariantList VideoViewModel::propiedades(const QString& id) const {
    // De la vista: el elemento y los valores con que corre (un controlador, los de su molde).
    return video::VideoGrafos::controlesDe(*gst(), v().value("formularios", json::object()).value(id.toStdString(), json()));
}

QVariantList VideoViewModel::propiedadesPad(const QString& id, const QString& puerto) const {
    return video::VideoGrafos::controlesDe(*gst(), v().value("formularios", json::object()).value(id.toStdString(), json()),
                                           puerto.toStdString());
}

QVariantList VideoViewModel::propiedadesModulo(const QString& elemento) const {
    return video::VideoGrafos::controlesGst(gst()->propiedades(elemento.toStdString(), json::object()));
}

QVariantList VideoViewModel::propiedadesModuloCon(const QString& elemento, const QVariantMap& valores) const {
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
    return video::VideoGrafos::controlesGst(gst()->propiedades(elemento.toStdString(), act));
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

QString VideoViewModel::agregar(const QString& tipo, const QString& despues) {
    return pedirEnLaElegida("video_agregar", [tipo, despues](video::VideoGrafos& g) { return g.agregar(tipo, despues); });
}
QString VideoViewModel::agregarFiltro(const QString& filtroId, const QString& despues) {
    return pedirEnLaElegida("video_agregar", [filtroId, despues](video::VideoGrafos& g) { return g.agregarFiltro(filtroId, despues); });
}
QString VideoViewModel::agregarModulo(const QString& elemento, const QString& despues) {
    return pedirEnLaElegida("video_agregar", [elemento, despues](video::VideoGrafos& g) { return g.agregarModulo(elemento, despues); });
}
QString VideoViewModel::agregarMezcla(const QString& elemento) {
    return pedirQ("video_agregar", [elemento](video::VideoGrafos& g) { return g.agregarMezcla(elemento); });
}
QString VideoViewModel::ajustarPad(const QString& id, const QString& puerto, const QString& clave, const QVariant& valor) {
    return pedirQ("video_ajustar", [id, puerto, clave, valor](video::VideoGrafos& g) { return g.ajustarPad(id, puerto, clave, valor); });
}
QString VideoViewModel::agregarSuperresUi(const QString& motor, const QString& modelo, const QString& despues) {
    return pedirEnLaElegida("video_agregar", [motor, modelo, despues](video::VideoGrafos& g) {
        return g.agregarSuperresUi(motor, modelo, despues);
    });
}
QString VideoViewModel::crearVirtual(const QString& nombre, const QString& preset, const QString& escena) {
    return pedirQ("video_virtual", [nombre, preset, escena](video::VideoGrafos& g) { return g.crearVirtual(nombre, preset, escena); });
}
QString VideoViewModel::quitar(const QString& id) {
    return pedirQ("video_quitar", [id](video::VideoGrafos& g) { return g.quitar(id); });
}
QString VideoViewModel::reubicar(const QString& id, const QString& despues) {
    return pedirQ("video_mover", [id, despues](video::VideoGrafos& g) { return g.reubicar(id, despues); });
}
QString VideoViewModel::correr(const QString& id, int paso) {
    return pedirQ("video_mover", [id, paso](video::VideoGrafos& g) { return g.correr(id, paso); });
}
QString VideoViewModel::conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada) {
    return pedirQ("video_cable", [desde, salida, hacia, entrada](video::VideoGrafos& g) { return g.conectar(desde, salida, hacia, entrada); });
}
QString VideoViewModel::desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada) {
    return pedirQ("video_cable",
                  [desde, salida, hacia, entrada](video::VideoGrafos& g) { return g.desconectar(desde, salida, hacia, entrada); });
}
QString VideoViewModel::mover(const QString& id, int x, int y) {
    return pedirQ("video_mover", [id, x, y](video::VideoGrafos& g) { return g.mover(id, x, y); });
}
QString VideoViewModel::ajustar(const QString& id, const QVariantMap& params) {
    return pedirQ("video_ajustar", [id, params](video::VideoGrafos& g) { return g.ajustar(id, params); });
}
QString VideoViewModel::controlar(const QString& id) {
    return pedirQ("video_controlador", [id](video::VideoGrafos& g) { return g.controlar(id); });
}
QString VideoViewModel::agregarControlada(const QString& control, const QString& entrada) {
    return pedirQ("video_controlador", [control, entrada](video::VideoGrafos& g) { return g.agregarControlada(control, entrada); });
}
QString VideoViewModel::soltarControl(const QString& id) {
    return pedirQ("video_controlador", [id](video::VideoGrafos& g) { return g.soltarControl(id); });
}
QString VideoViewModel::ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor) {
    return pedirQ("video_ajustar", [id, clave, valor](video::VideoGrafos& g) { return g.ajustarFiltro(id, clave, valor); });
}
QString VideoViewModel::aEscena(const QString& id, const QString& escena) {
    return pedirQ("video_a_escena", [id, escena](video::VideoGrafos& g) { return g.aEscena(id, escena); });
}
QString VideoViewModel::limpiar() {
    return pedirEnLaElegida("video_limpiar", [](video::VideoGrafos& g) { return g.limpiar(); });
}
QString VideoViewModel::ordenar() {
    return pedirQ("video_ordenar", [](video::VideoGrafos& g) { return g.ordenar(); });
}
QString VideoViewModel::configurarRelojUi(const QVariantMap& cambios) {
    return pedirQ("video_reloj", [cambios](video::VideoGrafos& g) { return g.configurarRelojUi(cambios); });
}
void VideoViewModel::medirUi(const QString& a, const QString& b, bool aplicar) {
    // El resultado llega por VideoGrafos::medicion (conectada a la nuestra).
    ventanilla_.pedir("video_medir", [&g = grafos_, a, b, aplicar]() { g.medirUi(a, b, aplicar); });
}
QString VideoViewModel::desfaseUi(const QString& entrada, double ms) {
    return pedirQ("video_desfase", [entrada, ms](video::VideoGrafos& g) { return g.desfaseUi(entrada, ms); });
}
QString VideoViewModel::superresUi(const QString& entrada, bool activa) {
    return pedirQ("video_superres", [entrada, activa](video::VideoGrafos& g) { return g.superresUi(entrada, activa); });
}
QString VideoViewModel::sincroniaUi(const QString& entrada, const QString& modo) {
    return pedirQ("video_sincronia", [entrada, modo](video::VideoGrafos& g) { return g.sincroniaUi(entrada, modo); });
}
void VideoViewModel::ponerTopeEstudioUi(int tope) {
    pedir("video_latencia", [tope](video::VideoGrafos& g) {
        g.ponerTopeEstudioUi(tope);
        return std::string();
    });
}
QString VideoViewModel::claseSincroniaUi(const QString& entrada, const QString& clase) {
    return pedirQ("video_sincronia", [entrada, clase](video::VideoGrafos& g) { return g.claseSincroniaUi(entrada, clase); });
}
QString VideoViewModel::bajarLatenciaUi() {
    // «Ya está en lo que piden» no es un error: va como aviso.
    pedir("video_latencia", [](video::VideoGrafos& g) { return g.bajarLatenciaUi().toStdString(); },
          [this](const std::string& e) {
              if (!e.empty()) emit notify(q(e), "info");
          });
    return {};
}
QString VideoViewModel::automixerPonerUi(const QString& entrada, const QString& grupo) {
    return pedirQ("video_automixer", [entrada, grupo](video::VideoGrafos& g) { return g.automixerPonerUi(entrada, grupo); });
}
QString VideoViewModel::automixerAjustesUi(const QString& grupo, const QVariantMap& ajustes) {
    return pedirQ("video_automixer", [grupo, ajustes](video::VideoGrafos& g) { return g.automixerAjustesUi(grupo, ajustes); });
}
QString VideoViewModel::configurarProgramadorUi(const QVariantMap& cambios) {
    return pedirQ("video_programador", [cambios](video::VideoGrafos& g) { return g.configurarProgramadorUi(cambios); });
}
QString VideoViewModel::agregarFiltroAudioUi(const QString& entrada, const QString& cual) {
    return pedirQ("video_filtro_audio", [entrada, cual](video::VideoGrafos& g) { return g.agregarFiltroAudioUi(entrada, cual); });
}
QString VideoViewModel::quitarFiltroAudioUi(const QString& entrada, int indice) {
    return pedirQ("video_filtro_audio", [entrada, indice](video::VideoGrafos& g) { return g.quitarFiltroAudioUi(entrada, indice); });
}
QString VideoViewModel::ponerFiltrosAudioUi(const QString& entrada, const QVariantList& filtros) {
    return pedirQ("video_filtro_audio", [entrada, filtros](video::VideoGrafos& g) { return g.ponerFiltrosAudioUi(entrada, filtros); });
}
QString VideoViewModel::superresMotorUi(const QString& entrada, const QString& motor, const QString& modelo) {
    return pedirQ("video_superres", [entrada, motor, modelo](video::VideoGrafos& g) { return g.superresMotorUi(entrada, motor, modelo); });
}
QString VideoViewModel::nuevaEntradaRed(const QString& nombre, const QString& uri, const QString& escena, int latenciaMs,
                                        bool normalizar) {
    return pedirQ("video_entrada_red", [nombre, uri, escena, latenciaMs, normalizar](video::VideoGrafos& g) {
        return g.nuevaEntradaRed(nombre, uri, escena, latenciaMs, normalizar);
    });
}
QString VideoViewModel::presetPanoramicaUi(const QString& id, const QString& preset, double campo) {
    return pedirQ("video_panoramica", [id, preset, campo](video::VideoGrafos& g) { return g.presetPanoramicaUi(id, preset, campo); });
}
QString VideoViewModel::aplicarDisposicionUi(const QString& compositor, const QString& disposicion) {
    return pedirQ("video_mosaico",
                  [compositor, disposicion](video::VideoGrafos& g) { return g.aplicarDisposicionUi(compositor, disposicion); });
}

// --- Para los demás modelos (de la foto) ---------------------------------------------

json VideoViewModel::estadoEntrada(const std::string& entrada) const {
    const json st = v().value("estados", json::object()).value(entrada, json());
    if (st.is_object()) return st;
    return {{"error", tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString()}};
}

bool VideoViewModel::esEntrada(const std::string& nombre) const { return entradas_.contains(q(nombre)); }

std::string VideoViewModel::uriDe(const std::string& entrada) const {
    const json f = ventanilla_.foto().fuentesVideo.value(entrada, json());
    if (!f.is_object() || f.value("tipo", std::string()) != "ba_gst_fuente") return {};
    const json a = f.value("ajustes", json::object());
    return a.is_object() ? a.value("uri", std::string()) : std::string();
}

json VideoViewModel::entradasDeHost(const std::string& host) const {
    json out = json::array();
    for (const auto& [n, f] : ventanilla_.foto().fuentesVideo.items()) {
        if (!f.is_object() || f.value("tipo", std::string()) != "ba_gst_fuente") continue;
        const json a = f.value("ajustes", json::object());
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
    for (const auto& t : v().value("tamanos", json::array()))
        if (t.is_array() && t.size() == 2) out.emplace_back(t[0].get<int>(), t[1].get<int>());
    return out;
}

json VideoViewModel::nodos() const { return v().value("nodos", json::array()); }
std::string VideoViewModel::legalizar() const { return v().value("legalizar", std::string("no")); }
json VideoViewModel::estudioJson() const { return v().value("estudio", json::object()); }

} // namespace ba::app
