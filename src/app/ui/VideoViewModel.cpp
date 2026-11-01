// src/app/ui/VideoViewModel.cpp — ver el .hpp

#include "ui/VideoViewModel.hpp"

#include "emision/VentanillaMotor.hpp"
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

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

// Los textos son los que estaban en la página (mismo contexto: VideoPage), así siguen
// con sus traducciones.
QString t(const char* s) { return QCoreApplication::translate("VideoPage", s); }
QString tn(const char* s, int n) { return QCoreApplication::translate("VideoPage", s, "", n); }
QString num(double v, int dec = 2) { return QString::number(v, 'f', dec); }
QString texto(const json& j) {
    if (j.is_string()) return q(j.get<std::string>());
    if (j.is_number_integer()) return QString::number(j.get<long long>());
    if (j.is_number()) {
        QString s = QString::number(j.get<double>(), 'f', 2);
        while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) s.chop(1);
        return s;
    }
    return j.is_null() ? QString() : q(j.dump());
}
double dbl(const json& p, const char* k, double d = 0) {
    const auto it = p.find(k);
    return it != p.end() && it->is_number() ? it->get<double>() : d;
}

/// Lo que se lee de un nodo en su tarjeta (abajo del nombre): qué hace, con lo principal
/// de sus ajustes. `conectadas`: los cables que le llegan (para una mezcla).
QString resumenDeNodo(const json& n, int conectadas) {
    const json p = n.value("params", json::object());
    const std::string tipo = n.value("tipo", std::string());
    if (n.contains("error") && !n["error"].is_null() && !texto(n["error"]).isEmpty()) return "⚠ " + texto(n["error"]);
    const QString enEscena = n.value("publicada", false) ? t(" · en escena") : QString();
    if (tipo == "entrada") return t("La cámara (ya encuadrada por el PTZ)");
    if (tipo == "escena") return t("Lo que ve la escena");
    if (tipo == "obs") return t("Filtro de OBS · %1").arg(texto(p.value("filtro", json(""))));
    if (tipo == "control") {
        const std::string tarjeta = p.value("tarjeta", std::string());
        const json pp = p.value("params", json::object());
        return t("Controlador · %1").arg(tarjeta == "gst" ? texto(pp.value("elemento", json(""))) : tarjeta == "obs" ? texto(pp.value("filtro", json(""))) : q(tarjeta));
    }
    // `controlador` puede ser true o el id del controlador (texto): value<bool> con un texto tira.
    const json ctl = n.value("controlador", json());
    if ((ctl.is_boolean() && ctl.get<bool>()) || (ctl.is_string() && !ctl.get<std::string>().empty()))
        return p.value("local", json::object()).empty() ? t("↳ del controlador") : t("↳ del controlador · con ajuste propio");
    if (tipo == "gst") {
        const json pr = p.value("propiedades", json::object());
        QStringList ks;
        for (const auto& [k, v] : pr.items()) {
            if (ks.size() == 2) break;
            ks << q(k) + "=" + texto(v);
        }
        return "GStreamer · " + texto(p.value("elemento", json(""))) + (ks.isEmpty() ? QString() : " · " + ks.join(' '));
    }
    if (tipo == "compositor") {
        const int ent = static_cast<int>(n.value("entradas", json::array()).size()) - 1;
        return tn("%n entrada · %1", ent).arg(dbl(p, "ancho") > 0 ? texto(p["ancho"]) + "×" + texto(p.value("alto", json(0))) : t("lienzo del estudio")) + enEscena;
    }
    if (tipo == "mezcla") return tn("GStreamer · %1 · %n conectada", conectadas).arg(texto(p.value("elemento", json("")))) + enEscena;
    if (tipo == "color")
        return t("brillo %1 · contraste %2 · saturación %3").arg(num(dbl(p, "brillo")), num(dbl(p, "contraste")), num(dbl(p, "saturacion")));
    if (tipo == "croma")
        return t("fondo %1 · similitud %2").arg("#" + QString::number(static_cast<qint64>(dbl(p, "clave")), 16).rightJustified(6, '0').right(6).toUpper(), num(dbl(p, "similitud")));
    if (tipo == "recorte") {
        QStringList partes;
        if (dbl(p, "izquierda") > 0 || dbl(p, "arriba") > 0 || dbl(p, "derecha") > 0 || dbl(p, "abajo") > 0) partes << t("bordes recortados");
        if (p.value("espejoH", false)) partes << t("espejo ↔");
        if (p.value("espejoV", false)) partes << t("espejo ↕");
        return partes.isEmpty() ? t("sin cambios") : partes.join(" · ");
    }
    if (tipo == "vista360" || tipo == "panoramica") {
        const std::string ver = p.value("ver", std::string());
        if (ver == "entrada") return t("calibrando: la entrada con los círculos");
        if (ver == "tira")
            return (dbl(p, "ancho") > 0 ? t("la tira %1×%2").arg(texto(p["ancho"]), texto(p.value("alto", json(0)))) : t("la tira (la suma de las cámaras)")) + enEscena;
        if (ver == "panorama") {
            const std::string guia = p.value("guia", std::string());
            return !guia.empty() && guia != "ninguna" ? t("el panorama 360 · guía %1").arg(q(guia)) : t("el panorama 360");
        }
        const QString mira = t("mira %1° · %2° · abre %3°").arg(qRound(dbl(p, "yaw"))).arg(qRound(dbl(p, "pitch"))).arg(qRound(dbl(p, "fov", 90)));
        if (tipo != "panoramica") return mira + enEscena;
        int camaras = 0;
        for (const auto& e : n.value("entradas", json::array()))
            if (!e.value("pedido", false)) ++camaras;
        return tn("%n cámara · %1", camaras).arg(mira) + enEscena;
    }
    if (tipo == "salida") return t("«%1»").arg(texto(p.value("nombre", json("")))) + enEscena;
    if (tipo == "retardo") return tn("%n cuadro de atraso", static_cast<int>(dbl(p, "cuadros")));
    if (tipo == "ptz") return p.value("activo", true) == false ? t("Apagado") : t("Pan, tilt y zoom digital (Cámaras PTZ)");
    if (tipo == "seguir") {
        if (p.value("activo", true) == false) return t("Apagado");
        const QString obj = texto(p.value("objetivo", json("")));
        return !obj.isEmpty() ? t("Sigue: %1").arg(obj) : t("Sin nadie elegido");
    }
    if (tipo == "rostros") {
        const std::string uso = p.value("uso", std::string());
        QString s = uso == "mirar" ? t("Personas mira") : uso == "autoalta" ? t("Personas anota sola") : uso == "puerta" ? t("Puerta: acredita")
                    : uso == "seguir" ? t("Sigue a una cara") : t("Caras");
        if (p.value("marcar", false)) s += " · " + (p.contains("estilo") && p["estilo"].is_string() ? q(p["estilo"].get<std::string>()) : QStringLiteral("recuadro"));
        if (const QString tx = texto(p.value("texto", json(""))); !tx.isEmpty()) s += " · «" + tx + "»";
        return s;
    }
    return {};
}

/// El grafo (o el estudio) con el `resumen` de cada nodo, para la vista.
json conResumenes(json g) {
    if (!g.is_object() || !g.contains("nodos") || !g["nodos"].is_array()) return g;
    const json cables = g.value("cables", json::array());
    for (json& n : g["nodos"]) {
        if (!n.is_object()) continue;
        int conectadas = 0;
        for (const auto& c : cables)
            if (c.is_object() && c.value("hacia", json()) == n.value("id", json())) ++conectadas;
        n["resumen"] = resumenDeNodo(n, conectadas).toStdString();
    }
    return g;
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
    // Lo que se mueve: cada propiedad avisa sola (el reloj o la red no redibujan el automixer).
    std::vector<void (VideoViewModel::*)()> avisar;
    const auto poner = [&](const char* k, auto& campo, auto nuevo, void (VideoViewModel::*senal)()) {
        if (!difiere(k) || campo == nuevo) return;
        campo = std::move(nuevo);
        avisar.push_back(senal);
    };
    poner("red", estadoRed_, aVariant(x.value("red", json::array())).toList(), &VideoViewModel::redCambio);
    poner("diagnostico", diagnostico_, aVariant(x.value("diagnostico", json::array())).toList(), &VideoViewModel::diagnosticoCambio);
    poner("programador", programador_, aVariant(x.value("programador", json::object())).toMap(), &VideoViewModel::programadorCambio);
    poner("reloj", reloj_, aVariant(x.value("reloj", json::object())).toMap(), &VideoViewModel::relojCambio);
    poner("latencia", latencia_, aVariant(x.value("latencia", json::object())).toMap(), &VideoViewModel::latenciaCambio);
    poner("automixer", automixer_, aVariant(x.value("automixer", json::object())).toMap(), &VideoViewModel::automixerCambio);
    poner("filtros_audio", filtrosAudio_, aVariant(x.value("filtros_audio", json::object())).toMap(), &VideoViewModel::filtrosAudioCambio);
    poner("escenas", escenas_, listaDeTextos(x.value("escenas", json::array())), &VideoViewModel::escenasCambio);
    poner("hay_superres", haySuperres_, x.value("hay_superres", false), &VideoViewModel::haySuperresCambio);
    const bool eventos = difiere("eventos");
    antes_ = x;
    if (estructura) {
        entradas_ = listaDeTextos(x.value("entradas", json::array()));
        entradasInfo_ = aVariant(x.value("info", json::array())).toList();
        estudio_ = aVariant(conResumenes(x.value("estudio", json::object()))).toMap();
        // La elegida: la que se eligió acá; si no hay o se fue, la primera.
        if ((entrada_.isEmpty() || !entradas_.contains(entrada_)) && !entradas_.isEmpty()) entrada_ = entradas_.first();
        grafo_ = entrada_.isEmpty() ? QVariantMap{}
                                    : aVariant(conResumenes(x.value("grafos", json::object()).value(entrada_.toStdString(), json::object()))).toMap();
    }
    // Los eventos son los de la elegida (que puede haber cambiado con la estructura).
    if (eventos || estructura) {
        QVariantMap e = aVariant(x.value("eventos", json::object()).value(entrada_.toStdString(), json::object())).toMap();
        if (e != eventos_) {
            eventos_ = std::move(e);
            avisar.push_back(&VideoViewModel::eventosCambio);
        }
    }
    if (estructura) emit cambio();
    for (const auto senal : avisar) emit(this->*senal)();
}

void VideoViewModel::seleccionar(const QString& entrada) {
    if (entrada == entrada_) return;
    entrada_ = entrada;
    grafo_ = aVariant(conResumenes(v().value("grafos", json::object()).value(entrada_.toStdString(), json::object()))).toMap();
    eventos_ = aVariant(v().value("eventos", json::object()).value(entrada_.toStdString(), json::object())).toMap();
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
