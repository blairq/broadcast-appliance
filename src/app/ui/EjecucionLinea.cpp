// ============================================================================
// src/app/ui/EjecucionLinea.cpp — ver EjecucionLinea.hpp
// ============================================================================
#include "ui/EjecucionLinea.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <algorithm>
#include <cmath>

#include "arranque/AppRuntime.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/Votaciones.hpp"
#include "control/Dsk.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "medios/Playlist.hpp"
#include "ptz/ControlCamaras.hpp"
#include "sala/OradoresSala.hpp"
#include "sala/Teleprompter.hpp"
#include "ui/CapturaError.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/ConduccionLinea.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/GuionViewModel.hpp"

using ::ba::app::errorDe;

namespace ba::app {
namespace {

using json = nlohmann::json;
using evento::linea::Accion;

int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

/// El nombre fijo de la música del evento: un tema nuevo reemplaza al anterior en vez
/// de encimarse, que es lo peor que puede pasar en vivo.
constexpr const char* kMusica = "Música del evento";
/// La escena y la fuente que usa el cue de video. Nombres fijos: así un video nuevo
/// reemplaza al anterior en vez de encimarse, y el operador puede componer esa escena una
/// vez y que le sirva para todos. OJO: una escena TAMBIÉN es una fuente, y los nombres son
/// únicos entre todas (si la fuente se llamara igual que su escena, se le agrega un « 2»
/// y después nada la encuentra por nombre).
constexpr const char* kEscenaVideo = "Video del evento";
constexpr const char* kVideo = "Video";

} // namespace

EjecucionLinea::EjecucionLinea(ConduccionLinea& n, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                               ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent)
    : QObject(parent), n_(n), guion_(guion), rt_(rt), ventanilla_(ventanilla), sala_(sala), graficos_(graficos) {
    // Los medios que suenan llegan en la foto (parte Medidas): ahí se ve si terminó el de
    // una pieza que termina con su medio.
    connect(&ventanilla_, &VentanillaMotor::cambio, this, [this](unsigned p) {
        if ((p & FotoMotor::Medidas) != 0 && !mediosVigilados_.empty()) mirarMedios();
    });
}

EjecucionLinea::~EjecucionLinea() = default;

std::string EjecucionLinea::salidaDe(const Accion& a) const { return a.salida.empty() ? n_.salidaPorOmision() : a.salida; }

void EjecucionLinea::reiniciar() {
    graficosDePieza_.clear();
    musicaDePieza_.clear();
    dskDePieza_.clear();
    ptzPendientes_.clear();
    mediosVigilados_.clear();
    playlistDePieza_.clear();
}

void EjecucionLinea::pieza(const evento::linea::Pieza& z, const Accion& a, const QString& titulo) {
    const std::string salida = salidaDe(a);
    // Una acción del asistente programada en la línea: se aprobó al escribirla y no frena
    // el vivo (se hace por el mismo servidor de herramientas).
    if (z.tipo == "asistente") {
        const std::string herramienta = z.datos.value("herramienta", std::string());
        if (!herramienta_ || herramienta.empty()) {
            n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo hacer «%1»: el servidor de herramientas no está").arg(titulo), "error");
            return;
        }
        QPointer<EjecucionLinea> yo(this);
        herramienta_(herramienta, z.datos.value("args", json::object()), [yo, titulo](bool ok, const std::string& texto) {
            if (!yo) return;
            yo->n_.anotar(ok ? QCoreApplication::translate("EventoViewModel", "Hecho: %1").arg(titulo)
                             : QCoreApplication::translate("EventoViewModel", "No se pudo hacer «%1»: %2").arg(titulo, QString::fromStdString(texto)),
                          ok ? "ok" : "error");
        });
        return;
    }
    if (ejecutarPiezaLinea(z, a, titulo)) return;
    evento::Tipo t{};
    if (!evento::tipoDeNombre(z.tipo, t)) {
        // Honestidad: el tipo existe en la línea pero todavía no se ejecuta.
        n_.anotar(QCoreApplication::translate("EventoViewModel", "Todavía no sé ejecutar una pieza de tipo «%1»: %2").arg(QString::fromStdString(z.tipo), titulo), "error");
        return;
    }
    const evento::linea::Pista* pi = n_.corrida() ? n_.corrida()->linea().pista(z.pista) : nullptr;
    ejecutarCue(n_.cueDePieza(z, a.paso), a.paso, salida + "/" + a.paso + "/" + a.pieza, salida, pi == nullptr || pi->salida.empty());
}

bool EjecucionLinea::esDeLaLinea(const std::string& salida, const std::string& escena) {
    // Lo que cambia la línea misma (tomarEscena, el video) no cuenta como «a mano»: su aviso
    // llega encolado desde el dueño del motor, después de la orden.
    auto it = escenasDeLaLinea_.find(salida);
    if (it == escenasDeLaLinea_.end()) return false;
    auto x = it->second.find(escena);
    if (x == it->second.end()) return false;
    it->second.erase(x);
    return true;
}

void EjecucionLinea::alAireCambio() {
    // Una cámara pudo haber salido del aire: las PTZ que la esperaban, ahora.
    ptz::ControlCamaras* cams = n_.sistema().camaras;
    if (cams == nullptr) return;
    std::vector<std::string> listas;
    for (const auto& [clave, x] : ptzPendientes_)
        if (cams->chequearMovimiento(QString::fromStdString(x.camara)).isEmpty()) listas.push_back(clave);
    for (const std::string& clave : listas)
        moverPtz(clave, false);
}



std::string EjecucionLinea::alPlaylist() {
    const SistemaLinea s = n_.sistema();
    if (s.playlist == nullptr || playlistDePieza_.empty()) return {};
    const bool suena = s.playlist->estado().value("reproduciendo", false);
    if (suena) playlistAndando_ = true;
    if (suena || !playlistAndando_) return {};
    const std::string pieza = playlistDePieza_;
    playlistDePieza_.clear();
    playlistAndando_ = false;
    return pieza;
}

json EjecucionLinea::ptzPendientes(const std::function<std::string(int64_t)>& hora) const {
    json ptz = json::array();
    for (const auto& [clave, x] : ptzPendientes_)
        ptz.push_back({{"pieza", x.pieza}, {"paso", x.paso}, {"salida", x.salida}, {"camara", x.camara}, {"posicion", x.posicion},
                       {"desde", hora(x.desdeMs)}});
    return ptz;
}


std::string EjecucionLinea::prepararEscenaDeVideo(AppRuntime& rt, const std::string& escenaV, const std::string& fuenteV,
                                                  bool* creada) {
    // En el dueño del motor (lo pide el cue de video). Una por salida (la por omisión, la
    // de siempre): cada salida pasa su video.
    const auto escenas = rt.engine().sceneNames();
    const bool existe = std::find(escenas.begin(), escenas.end(), escenaV) != escenas.end();
    if (!existe) {
        if (!rt.addScene(escenaV))
            return QCoreApplication::translate("EventoViewModel", "No se pudo crear la escena del video: %1").arg(QString::fromStdString(rt.lastError())).toStdString();
        // Protegida: si alguien la borra, todos los cues de video del guion
        // dejan de funcionar y no es obvio por qué.
        rt.setSceneProtected(escenaV, true);
        if (creada != nullptr) *creada = true;
    }
    // La fuente de video: se crea una vez y después solo se le cambia el
    // archivo. Ocupa el cuadro entero, que es lo que uno espera de un video.
    const auto fuentes = rt.engine().sceneSources(escenaV);
    if (std::find(fuentes.begin(), fuentes.end(), fuenteV) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = fuenteV;
        spec.rect = CanvasRect{0.0, 0.0, 1.0, 1.0};
        spec.settingsJson = R"({"clear_on_media_end":true})";
        if (!rt.addSource(escenaV, spec))
            return QCoreApplication::translate("EventoViewModel", "No se pudo crear la fuente de video: %1").arg(QString::fromStdString(rt.lastError())).toStdString();
    }
    return {};
}

void EjecucionLinea::sacarAlAvanzar(const std::string& salida) {
    if (graficos_ == nullptr) return;
    for (auto it = alAvanzar_.begin(); it != alAvanzar_.end();) {
        if (!salida.empty() && it->first != salida) {
            ++it;
            continue;
        }
        for (const auto& [fuente, gen] : it->second) graficos_->sacar(fuente, gen);
        it = alAvanzar_.erase(it);
    }
}

std::optional<std::string> EjecucionLinea::salidaDelMotor(const std::string& salida) const {
    // Los ids de la línea son los de las salidas del motor; la por omisión de la línea,
    // si el motor no tiene una con ese id, es la por omisión del motor ("").
    if (ventanilla_.foto().salida(salida) != nullptr) return salida;
    if (salida == n_.salidaPorOmision()) return std::string();
    return std::nullopt;
}

const FotoMotor::Salida* EjecucionLinea::aireDe(const std::string& salida) const {
    const std::optional<std::string> id = salidaDelMotor(salida);
    return id ? ventanilla_.foto().salida(*id) : nullptr;
}

void EjecucionLinea::pedirAlConmutador(const char* que, const std::string& salida,
                                        std::function<std::string(escenas::Conmutador&)> orden,
                                        std::function<void(const std::string& error)> listo) {
    const std::optional<std::string> id = salidaDelMotor(salida);
    escenas::Conmutadores* cs = n_.sistema().conmutadores;
    if (!id || cs == nullptr) {
        if (listo) listo(QCoreApplication::translate("EventoViewModel", "la salida «%1» no tiene conmutador de escenas").arg(QString::fromStdString(salida)).toStdString());
        return;
    }
    // El conmutador es del dueño del motor: la orden corre allá (en orden, sin esperar).
    ventanilla_.pedir(
        this, que,
        [cs, id = *id, salida, orden = std::move(orden)]() -> std::string {
            escenas::Conmutador* c = id.empty() ? &cs->porOmision() : cs->de(id);
            if (c == nullptr)
                return QCoreApplication::translate("EventoViewModel", "la salida «%1» no tiene conmutador de escenas").arg(QString::fromStdString(salida)).toStdString();
            return orden(*c);
        },
        [listo = std::move(listo)](const std::string& e) {
            if (listo) listo(e);
        });
}

void EjecucionLinea::tomarEscena(const std::string& escena, const nlohmann::json& datos, const std::string& salida,
                                  std::function<void(const std::string& error)> listo) {
    if (escena.empty()) return listo(QCoreApplication::translate("EventoViewModel", "el cue no dice qué escena poner").toStdString());
    // Ya al aire (la puso el operador a mano, o la repite el cue): no hay nada que tomar.
    const FotoMotor::Salida* s = aireDe(salida);
    if (s != nullptr && s->alAire == escena) return listo({});
    // Lo cambia la línea: no es «a mano» (el aviso del conmutador llega después).
    escenasDeLaLinea_[salida].insert(escena);
    const std::string tipo = datos.value("transicion", std::string());
    const int ms = datos.value("ms", -1);
    pedirAlConmutador(
        "linea_tomar", salida,
        [escena, tipo, ms](escenas::Conmutador& c) {
            if (c.alAire() == escena) return std::string();
            std::string e;
            if (c.armada() != escena) e = c.armar(escena);
            // La transición del cue si dice; si no, la de entrada de la escena (o la global).
            if (e.empty()) e = c.take(tipo, ms);
            return e;
        },
        [this, salida, escena, listo = std::move(listo)](const std::string& e) {
            if (!e.empty()) olvidarEscenaDeLaLinea(salida, escena);
            listo(e);
        });
}

void EjecucionLinea::olvidarEscenaDeLaLinea(const std::string& salida, const std::string& escena) {
    auto it = escenasDeLaLinea_.find(salida);
    if (it == escenasDeLaLinea_.end()) return;
    if (auto x = it->second.find(escena); x != it->second.end()) it->second.erase(x);
}

void EjecucionLinea::armar(const std::string& escena, const std::string& salida) {
    if (n_.ensayo() || escena.empty()) return;
    // La que ya está al aire no se arma (el TAKE no tendría sentido) y la ya armada, tampoco.
    if (const FotoMotor::Salida* s = aireDe(salida); s != nullptr && (escena == s->alAire || escena == s->armada)) return;
    pedirAlConmutador(
        "linea_armar", salida,
        [escena](escenas::Conmutador& c) {
            if (escena == c.alAire() || escena == c.armada()) return std::string();
            return c.armar(escena);
        },
        [this, escena, salida](const std::string& e) {
            if (!e.empty()) {
                n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "No se pudo armar «%1» en el PVW: %2").arg(QString::fromStdString(escena), QString::fromStdString(e))),
                       "error");
                return;
            }
            n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "Armada en el PVW: %1").arg(QString::fromStdString(escena))), "info");
        });
}

void EjecucionLinea::sacar(const Accion& a) {
    const std::string salida = a.salida.empty() ? n_.salidaPorOmision() : a.salida;
    const std::string clave = salida + "/" + a.paso + "/" + a.pieza;
    if (const auto it = graficosDePieza_.find(clave); it != graficosDePieza_.end()) {
        // Con su generación: si después se puso otra cosa en esa fuente, no se toca.
        if (graficos_ != nullptr) graficos_->sacar(it->second.first, it->second.second);
        graficosDePieza_.erase(it);
    }
    if (const auto it = musicaDePieza_.find(salida); it != musicaDePieza_.end() && it->second == clave) {
        ventanilla_.pedir("linea_musica", [&rt = rt_, m = musicaDe(salida)]() { rt.engine().stopMedia(m); });
        musicaDePieza_.erase(it);
        n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "Música: para")), "ok");
    }
    // La capa que subió la pieza baja con ella (una fija, como la de LSA, no: pide confirmación).
    if (const auto it = dskDePieza_.find(clave); it != dskDePieza_.end()) {
        if (control::Dsk* d = n_.sistema().dsk) {
            const std::string e = d->mostrar(it->second.first, false, false, it->second.second);
            if (!e.empty()) n_.anotar(n_.enSalida(salida, QString::fromStdString(e)), "info");
        }
        dskDePieza_.erase(it);
    }
    // Una PTZ que esperaba y ya no va: no se mueve más.
    ptzPendientes_.erase(clave);
    mediosVigilados_.erase(a.pieza);
    if (playlistDePieza_ == a.pieza) playlistDePieza_.clear();
}

void EjecucionLinea::ejecutarCue(const evento::Cue& cue, const std::string& paso, const std::string& clave, const std::string& salida,
                                  bool global) {
    const evento::Cue* c = &cue;
    const QString titulo = c->texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c->tipo))
                                            : QString::fromStdString(c->texto);

    // Todo por el runtime y los conmutadores, pedido al dueño del motor: el runtime avisa
    // (la tarjeta Dirección, el mezclador, el tally) y lo deja guardado en la configuración.
    switch (c->tipo) {
        case evento::Tipo::Escena: {
            const std::string escena = c->datos.value("escena", std::string());
            // Sin transición propia, la del paso (si dice).
            json datos = c->datos;
            const evento::linea::Paso* p = n_.corrida() ? n_.corrida()->linea().paso(paso) : nullptr;
            if (!datos.contains("transicion") && p != nullptr && p->transicion.is_object()) {
                datos["transicion"] = p->transicion.value("tipo", std::string());
                datos["ms"] = p->transicion.value("ms", -1);
            }
            tomarEscena(escena, datos, salida, [this, escena, salida](const std::string& err) {
                if (!err.empty()) {
                    n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "No se pudo poner la escena «%1»: %2").arg(QString::fromStdString(escena), QString::fromStdString(err))),
                           "error");
                    n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: %1").arg(QString::fromStdString(err)), "error");
                    return;
                }
                n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "Al aire: %1").arg(QString::fromStdString(escena))), "ok");
            });
            return;
        }
        case evento::Tipo::Orador: {
            const QString quien = QString::fromStdString(c->datos.value("participante", std::string()));
            if (sala_ == nullptr || quien.isEmpty()) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo dar la palabra: no dice a quién"), "error");
                return;
            }
            // Las mismas operaciones que la página Sala y el MCP (sala::OradoresSala).
            if (c->duracionSeg > 0) sala_->oradores().tiempo(quien.toStdString(), c->duracionSeg);
            sala_->oradores().alAire(quien.toStdString(), true);
            n_.anotar(QCoreApplication::translate("EventoViewModel", "Habla %1").arg(quien), "ok");
            return;
        }
        case evento::Tipo::Musica: {
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            // El nombre es fijo (kMusica): asi un tema nuevo reemplaza al anterior
            // en vez de encimarse, que es lo peor que puede pasar en vivo.
            const std::string medio = musicaDe(salida);
            if (accion == "parar") {
                ventanilla_.pedir("linea_musica", [&rt = rt_, medio]() { rt.engine().stopMedia(medio); });
                musicaDePieza_.erase(salida);
                n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "Música: para")), "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = guion_.medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No encontré la música \"%1\" en los medios del evento").arg(rel), "error");
                n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: falta «%1» en los medios del evento").arg(rel), "error");
                return;
            }
            const bool repetir = c->datos.value("repetir", false);
            // Con varias salidas, la música de cada una suena solo en su mezcla (las salidas
            // del motor, de su foto). Todo en el dueño, en una orden.
            std::vector<std::string> mezclas;
            if (n_.corrida() && n_.corrida()->linea().salidasEfectivas().size() > 1)
                for (const FotoMotor::Salida& x : ventanilla_.foto().salidas) mezclas.push_back(x.id);
            const FotoMotor::Salida* propia = aireDe(salida);
            ventanilla_.pedir(
                this, "linea_musica",
                [&rt = rt_, medio, r = ruta.toStdString(), repetir, mezclas, propia = propia ? propia->id : salida]() -> std::string {
                    if (!rt.engine().playMedia(medio, r, repetir)) return rt.engine().lastError();
                    for (const std::string& m : mezclas) rt.setMuted(medio, m != propia, m);
                    return {};
                },
                [this, rel](const std::string& e) {
                    if (!e.empty()) n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo reproducir %1: %2").arg(rel, QString::fromStdString(e)), "error");
                });
            musicaDePieza_[salida] = clave; // la saca su pieza cuando se termina su vida (si la tiene)
            const std::string pieza = clave.substr(clave.rfind('/') + 1);
            if (!repetir) mediosVigilados_[pieza] = medio; // para «medio:<pieza>.termino»
            n_.anotar(n_.enSalida(salida, repetir ? QCoreApplication::translate("EventoViewModel", "Música: %1 (en bucle)").arg(rel) : QCoreApplication::translate("EventoViewModel", "Música: %1").arg(rel)), "ok");
            return;
        }
        case evento::Tipo::Teleprompter: {
            // El texto vive en Discursos/ de la carpeta del evento, no pegado
            // en la tablet: asi viaja con el evento y el asistente puede
            // ayudar a escribirlo o acortarlo.
            const QString rel = QString::fromStdString(c->datos.value("discurso", std::string()));
            if (sala_ == nullptr || rel.isEmpty()) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo cargar el discurso: no dice cuál"), "error");
                return;
            }
            // La misma carga que la página Teleprompter (sala::Teleprompter): el
            // discurso de Discursos/ con su ruta de título. `arrancar` en los
            // datos del cue: por defecto el texto queda LISTO pero parado, que
            // es lo que se quiere casi siempre (el orador todavia esta
            // caminando hacia el atril).
            // En cuál (cada uno con su texto y su página); sin decir, el principal.
            const std::string cual = c->datos.value("prompter", std::string());
            if (!cual.empty() && !guion_.listaPrompters().existe(cual)) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No hay un teleprompter «%1» en el evento").arg(QString::fromStdString(cual)), "error");
                return;
            }
            // El texto, de la caché del guion (Discursos/ se lee en el fondo al abrir y cuando
            // cambia): en vivo no se va al disco.
            bool ok = false;
            const QString texto = guion_.textoDeDiscurso(rel, &ok);
            if (ok) {
                auto tp = sala_->teleprompter().para(cual);
                tp.cargarTexto(rel.toStdString(), texto.toStdString());
                if (c->datos.value("arrancar", false)) tp.arrancar(true);
            }
            if (!ok) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo abrir el discurso: %1").arg(rel), "error");
                n_.avisar(QCoreApplication::translate("EventoViewModel", "No encontré el discurso \"%1\" en la carpeta del evento").arg(rel), "error");
                return;
            }
            n_.anotar(QCoreApplication::translate("EventoViewModel", "Teleprompter: %1").arg(rel), "ok");
            return;
        }
        case evento::Tipo::Grafico: {
            // El gráfico sale en una FUENTE de escena, que el guion nombra. Si
            // esa fuente no está en ninguna escena, el gráfico no lo ve nadie:
            // se dice, en vez de dar el cue por hecho.
            const std::string accion = c->datos.value("accion", std::string("mostrar"));
            // Prender o apagar un rotulo que se mantiene solo: «durante las
            // entrevistas, zocalo automatico; en el cierre, apagado».
            if (accion == "prender" || accion == "apagar") {
                if (vinculos_ == nullptr) {
                    n_.anotar(QCoreApplication::translate("EventoViewModel", "No hay motor de gráficos para los vínculos"), "error");
                    return;
                }
                const QString id = QString::fromStdString(c->datos.value("vinculo", std::string()));
                if (!vinculos_->activar(id, accion == "prender")) {
                    n_.anotar(QCoreApplication::translate("EventoViewModel", "No existe el vínculo \"%1\"").arg(id), "error");
                    n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: no existe el vínculo \"%1\"").arg(id), "error");
                    return;
                }
                n_.anotar(
                    (accion == "prender" ? QCoreApplication::translate("EventoViewModel", "Prende el rótulo automático %1") : QCoreApplication::translate("EventoViewModel", "Apaga el rótulo automático %1")).arg(id),
                    "ok");
                return;
            }
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (graficos_ == nullptr) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No hay renderizador de gráficos en este bundle"), "error");
                return;
            }
            const std::uint64_t id = graficos_->fuentePorNombre(fuente);
            if (id == 0) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No hay ninguna fuente de gráfico llamada \"%1\" en las escenas").arg(fuente), "error");
                n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: falta la fuente de gráfico \"%1\"").arg(fuente),
                            "error");
                return;
            }
            if (accion == "ocultar") {
                const QString err = graficos_->ocultar(id);
                if (!err.isEmpty()) {
                    n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo sacar el gráfico: %1").arg(err), "error");
                    return;
                }
                n_.anotar(QCoreApplication::translate("EventoViewModel", "Saca el gráfico de %1").arg(fuente), "ok");
                return;
            }
            const QString grafico = QString::fromStdString(c->datos.value("grafico", std::string()));
            QJsonObject valores;
            if (c->datos.contains("valores"))
                valores =
                    QJsonDocument::fromJson(QByteArray::fromStdString(c->datos["valores"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
            const int dur = c->datos.value("duracionMs", 0);
            const bool repetir = c->datos.value("repetir", false);
            // CUÁNDO SE VA: lo que diga la cue (seVa); si no dice, «sola» si trae un
            // «cuándo se va» (salidaEn), y si no, lo que diga la ficha del gráfico (los
            // títulos y placas: al avanzar), o queda.
            std::string seVa = c->datos.value("seVa", std::string());
            if (seVa.empty())
                seVa = valores.value("salidaEn").toInt(0) > 0
                           ? "sola"
                           : graficos_->catalogo().seVaDe(grafico).toStdString();
            if (seVa != "sola") valores.remove("salidaEn"); // al avanzar o queda: no se va solo
            const QString err = graficos_->mostrar(grafico, valores, id, dur, repetir);
            if (!err.isEmpty()) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo sacar al aire el gráfico \"%1\": %2").arg(grafico, err), "error");
                n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue de gráfico falló: %1").arg(err), "error");
                return;
            }
            // La fuente es una sola: si otra salida la tenía para «al avanzar», ahora es de esta.
            for (auto& [s, lista] : alAvanzar_)
                lista.erase(std::remove_if(lista.begin(), lista.end(), [id](const auto& a) { return a.first == id; }), lista.end());
            if (seVa == "al-avanzar") alAvanzar_[salida].emplace_back(id, graficos_->generacion(id));
            graficosDePieza_[clave] = {id, graficos_->generacion(id)};
            n_.anotar((seVa == "al-avanzar" ? QCoreApplication::translate("EventoViewModel", "Al aire el gráfico %1 en %2 (se va al avanzar)")
                    : seVa == "sola"       ? QCoreApplication::translate("EventoViewModel", "Al aire el gráfico %1 en %2 (se va solo)")
                                           : QCoreApplication::translate("EventoViewModel", "Al aire el gráfico %1 en %2"))
                       .arg(grafico, fuente),
                   "ok");
            return;
        }
        case evento::Tipo::Carrusel: {
            // Fija carrusel y lámina (no «la siguiente»): si en el medio se movió a mano desde
            // la tarjeta, el guion lo vuelve a donde dice. Las mismas operaciones que la tarjeta.
            const QString nombre = QString::fromStdString(c->datos.value("carrusel", std::string()));
            const int lamina = evento::numeroDeLamina(c->datos);
            if (carruseles_ == nullptr) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No hay carruseles en este programa"), "error");
                return;
            }
            const QString err = errorDe([&] { carruseles_->mostrar(nombre, lamina); });
            if (!err.isEmpty()) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo poner el carrusel: %1").arg(err), "error");
                n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue de carrusel falló: %1").arg(err), "error");
                return;
            }
            n_.anotar(QCoreApplication::translate("EventoViewModel", "Carrusel «%1», lámina %2").arg(nombre).arg(lamina), "ok");
            return;
        }
        case evento::Tipo::Espera: n_.anotar(QCoreApplication::translate("EventoViewModel", "Espera tu confirmación: %1").arg(titulo), "info"); return;
        case evento::Tipo::Nota:
        case evento::Tipo::Pausa: n_.anotar(titulo, "info"); return;
        case evento::Tipo::Audio: {
            const std::string accion = c->datos.value("accion", std::string());
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (fuente.isEmpty()) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "El cue de audio no dice sobre que fuente actuar"), "error");
                return;
            }
            const std::string f = fuente.toStdString();
            // En qué mezcla: la de su salida; desde una pista global, en todas (así una
            // salida no le apaga el micrófono a otra: eso solo lo hace una pista global).
            std::vector<std::string> mezclas;
            if (const FotoMotor::Salida* propia = aireDe(salida)) mezclas.push_back(propia->id);
            else mezclas.push_back(salida);
            if (global) {
                mezclas.clear();
                for (const FotoMotor::Salida& x : ventanilla_.foto().salidas) mezclas.push_back(x.id);
            }
            // En el dueño (la mezcla es del motor): true si se pudo en todas.
            const auto enTodas = [this, mezclas](const char* que, std::function<bool(AppRuntime&, const std::string&)> hacer,
                                                 std::function<void(bool)> listo) {
                ventanilla_.pedir(
                    this, que,
                    [&rt = rt_, mezclas, hacer = std::move(hacer)]() {
                        bool ok = !mezclas.empty();
                        for (const std::string& m : mezclas)
                            ok = hacer(rt, m) && ok;
                        return ok;
                    },
                    std::move(listo));
            };
            if (accion == "silenciar" || accion == "activar") {
                const bool mudo = accion == "silenciar";
                enTodas("linea_audio", [f, mudo](AppRuntime& rt, const std::string& m) { return rt.setMuted(f, mudo, m); },
                        [this, fuente, mudo](bool ok) {
                            if (!ok) {
                                n_.anotar(QCoreApplication::translate("EventoViewModel", "No existe la fuente de audio \"%1\"").arg(fuente), "error");
                                n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: no existe la fuente \"%1\"").arg(fuente), "error");
                                return;
                            }
                            n_.anotar((mudo ? QCoreApplication::translate("EventoViewModel", "Silencia %1") : QCoreApplication::translate("EventoViewModel", "Activa %1")).arg(fuente), "ok");
                        });
                return;
            }
            if (accion == "volumen") {
                // En dB, que es como piensa el operador. 0 = sin cambio.
                const double db = c->datos.value("db", 0.0);
                const float mul = static_cast<float>(std::pow(10.0, db / 20.0));
                enTodas("linea_audio", [f, mul](AppRuntime& rt, const std::string& m) { return rt.setVolume(f, mul, m); },
                        [this, fuente, db](bool ok) {
                            if (!ok) return n_.anotar(QCoreApplication::translate("EventoViewModel", "No existe la fuente de audio \"%1\"").arg(fuente), "error");
                            n_.anotar(fuente + " a " + QString::number(db, 'f', 1) + " dB", "ok");
                        });
                return;
            }
            n_.anotar(QCoreApplication::translate("EventoViewModel", "No sé qué es la acción de audio \"%1\" (silenciar, activar o volumen)").arg(QString::fromStdString(accion)),
                   "error");
            return;
        }
        case evento::Tipo::Video: {
            // Un video tiene que VERSE, o sea estar en una escena, y las
            // escenas las arma el operador: meterle una fuente a la escena que
            // está al aire le desarmaría lo que compuso.
            //
            // Por eso el video del guion tiene SU escena, una sola, que se crea
            // sola la primera vez y queda PROTEGIDA: es la que el guion llena y
            // pone al aire. El operador puede componerla como quiera —ponerle
            // un fondo, un logo— y lo único que el guion le cambia es el
            // archivo de la fuente de video.
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            if (accion == "parar") {
                // Sin archivo, la fuente queda en negro. No se la saca de la
                // escena: recrearla en cada video costaria mas y se veria.
                ventanilla_.pedir("linea_video",
                                  [&rt = rt_, v = videoDe(salida)]() { rt.updateSourceSettings(v, R"({"local_file":""})"); });
                n_.anotar(QCoreApplication::translate("EventoViewModel", "Video: para"), "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = guion_.medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No encontré el video \"%1\" en los medios del evento").arg(rel), "error");
                n_.avisar(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: falta «%1» en los medios del evento").arg(rel), "error");
                return;
            }
            const bool repetir = c->datos.value("repetir", false);
            // Se le cambia el ARCHIVO a la fuente que ya esta en la escena, no
            // se crea una nueva: `playMedia` sirve para la musica, que va por
            // un canal global de audio y no se ve.
            const QJsonObject ajustes{{"local_file", ruta}, {"looping", repetir}, {"clear_on_media_end", true}};
            const bool alAire = c->datos.value("alAire", true);
            const std::string escenaV = escenaVideoDe(salida), fuenteV = videoDe(salida);
            // Lo que no es «a mano»: la escena del video, si va al aire.
            if (alAire) escenasDeLaLinea_[salida].insert(escenaV);
            const std::optional<std::string> id = salidaDelMotor(salida);
            escenas::Conmutadores* cs = n_.sistema().conmutadores;
            struct Hecho {
                std::string error, aviso;
                bool creada = false;
            };
            // Todo en el dueño, en una orden: la escena y la fuente (si faltan), el archivo,
            // rebobinar y, recién DESPUÉS de que el archivo esté cargado, al aire (al revés se
            // ve un cuadro en negro mientras abre).
            ventanilla_.pedir(
                this, "linea_video",
                [&rt = rt_, escenaV, fuenteV, cfg = QJsonDocument(ajustes).toJson(QJsonDocument::Compact).toStdString(), alAire,
                 id, cs]() {
                    Hecho h;
                    h.error = prepararEscenaDeVideo(rt, escenaV, fuenteV, &h.creada);
                    if (!h.error.empty()) return h;
                    if (!rt.updateSourceSettings(fuenteV, cfg)) {
                        h.aviso = rt.lastError();
                        return h;
                    }
                    // Y se rebobina: si el archivo es el mismo que ya estaba, el motor no
                    // tiene motivo para volver a empezar y el cue no mostraria nada.
                    rt.engine().restartMedia(fuenteV);
                    if (alAire) {
                        // Por el conmutador de su salida (corte directo, como antes): el tally, el
                        // AFV y la escena guardada se enteran igual que con cualquier otro cambio.
                        escenas::Conmutador* cm = cs == nullptr || !id ? nullptr : id->empty() ? &cs->porOmision() : cs->de(*id);
                        h.error = cm ? cm->poner(escenaV) : QCoreApplication::translate("EventoViewModel", "no hay conmutador de escenas").toStdString();
                        if (!h.error.empty())
                            h.error = QCoreApplication::translate("EventoViewModel", "No se pudo poner al aire la escena del video: %1").arg(QString::fromStdString(h.error)).toStdString();
                    }
                    return h;
                },
                [this, salida, rel, repetir, escenaV, alAire, clave, fuenteV](const Hecho& h) {
                    if (h.creada) n_.anotar(QCoreApplication::translate("EventoViewModel", "Creé la escena «%1» para los videos").arg(QString::fromStdString(escenaV)), "info");
                    if (!h.aviso.empty() || !h.error.empty()) {
                        if (alAire) olvidarEscenaDeLaLinea(salida, escenaV);
                        if (!h.aviso.empty()) return n_.anotar(QCoreApplication::translate("EventoViewModel", "No se pudo cargar %1: %2").arg(rel, QString::fromStdString(h.aviso)), "error");
                        return n_.anotar(n_.enSalida(salida, QString::fromStdString(h.error)), "error");
                    }
                    if (!repetir) mediosVigilados_[clave.substr(clave.rfind('/') + 1)] = fuenteV; // «medio:<pieza>.termino»
                            n_.anotar(n_.enSalida(salida, repetir ? QCoreApplication::translate("EventoViewModel", "Video: %1 (en bucle)").arg(rel) : QCoreApplication::translate("EventoViewModel", "Video: %1").arg(rel)), "ok");
                });
            return;
        }
        case evento::Tipo::Pantalla: {
            // Lo mismo que pantalla_mostrar: `contenido` es el texto de un
            // cartel, una dirección (url) o el nombre de una vista (programa,
            // reloj, prompter, tally, votacion).
            const std::string pantalla = c->datos.value("pantalla", std::string());
            const std::string contenido = c->datos.value("contenido", std::string());
            std::string tipo = c->datos.value("tipo", std::string());
            if (tipo.empty())
                tipo = contenido.rfind("http", 0) == 0 ? "url"
                       : (contenido == "programa" || contenido == "reloj" || contenido == "prompter" ||
                          contenido == "tally" || contenido == "votacion")
                           ? contenido
                           : "mensaje";
            json args{{"pantalla", pantalla}, {"tipo", tipo}};
            if (tipo == "url") args["url"] = contenido;
            if (tipo == "mensaje" || tipo == "qr") args["texto"] = contenido;
            if (c->datos.contains("pie")) args["pie"] = c->datos["pie"];
            if (!herramienta_) {
                n_.anotar(QCoreApplication::translate("EventoViewModel", "No se puede mandar a la pantalla: el servidor de herramientas no está"), "error");
                return;
            }
            QPointer<EjecucionLinea> yo(this);
            herramienta_("pantalla_mostrar", args, [yo, titulo, pantalla](bool ok, const std::string& texto) {
                if (!yo) return;
                yo->n_.anotar((ok ? QCoreApplication::translate("EventoViewModel", "Pantalla «%1»: %2").arg(QString::fromStdString(pantalla), titulo)
                               : QCoreApplication::translate("EventoViewModel", "No se pudo mandar a «%1»: %2")
                                     .arg(QString::fromStdString(pantalla), QString::fromStdString(texto))),
                           ok ? "ok" : "error");
            });
            return;
        }
        default:
            // Honestidad: el tipo existe en el guion pero todavia no se
            // ejecuta. Se anota y la línea sigue, en vez de hacer como que
            // paso algo.
            n_.anotar(QCoreApplication::translate("EventoViewModel", "Todavía no sé ejecutar un cue de tipo \"%1\": %2")
                       .arg(QString::fromStdString(evento::nombreDeTipo(c->tipo)), titulo),
                   "error");
            return;
    }
}

std::string EjecucionLinea::musicaDe(const std::string& salida) const {
    return salida.empty() || salida == n_.salidaPorOmision() ? std::string(kMusica) : std::string(kMusica) + " · " + salida;
}

std::string EjecucionLinea::escenaVideoDe(const std::string& salida) const {
    return salida.empty() || salida == n_.salidaPorOmision() ? std::string(kEscenaVideo) : std::string(kEscenaVideo) + " · " + salida;
}

std::string EjecucionLinea::videoDe(const std::string& salida) const {
    return salida.empty() || salida == n_.salidaPorOmision() ? std::string(kVideo) : std::string(kVideo) + " · " + salida;
}

bool EjecucionLinea::ejecutarPiezaLinea(const evento::linea::Pieza& z, const Accion& a, const QString& titulo) {
    const std::string salida = a.salida.empty() ? n_.salidaPorOmision() : a.salida;
    const std::string clave = salida + "/" + a.paso + "/" + a.pieza;
    const SistemaLinea s = n_.sistema();
    const auto falla = [&](const QString& porque) {
        n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "No se pudo hacer «%1»: %2").arg(titulo, porque)), "error");
        n_.avisar(QCoreApplication::translate("EventoViewModel", "La pieza «%1» no se pudo hacer: %2").arg(titulo, porque), "error");
    };
    const auto bien = [&](const QString& que) { n_.anotar(n_.enSalida(salida, que), "ok"); };
    const json& d = z.datos;

    if (z.tipo == "ptz") {
        // {camara, posicion, segundos?, siOcupada: esperar|mover|saltar}
        if (s.camaras == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay control de cámaras")), true;
        PtzPendiente x{salida, a.paso, a.pieza, d.value("camara", std::string()), d.value("posicion", std::string()),
                       d.value("segundos", -1.0), ahoraMs()};
        if (x.camara.empty() || x.posicion.empty()) return falla(QCoreApplication::translate("EventoViewModel", "la PTZ no dice qué cámara ni a qué posición")), true;
        const QString motivo = s.camaras->chequearMovimiento(QString::fromStdString(x.camara));
        const std::string si = d.value("siOcupada", std::string("esperar"));
        ptzPendientes_[clave] = x;
        if (motivo.isEmpty() || si == "mover") {
            moverPtz(clave, !motivo.isEmpty());
        } else if (si == "saltar") {
            ptzPendientes_.erase(clave);
            n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1»: no se mueve (%2)").arg(titulo, motivo)), "info");
        } else {
            // Ocupada: espera a que se libere, y la persona puede decir «mover igual».
            n_.anotar(n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1» espera: %2").arg(titulo, motivo)), "info");
            n_.avisar(QCoreApplication::translate("EventoViewModel", "%1 La PTZ «%2» se hace cuando se libere (o «mover igual»).").arg(motivo, titulo), "info");
            emit n_.correChanged();
        }
        return true;
    }

    if (z.tipo == "dsk") {
        // {capa: 1..4, accion: subir|bajar, confirmada?}: en las capas de ESTA salida.
        if (s.dsk == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay capas DSK")), true;
        const int capa = d.value("capa", 0);
        const bool subir = d.value("accion", std::string("subir")) != "bajar";
        const std::string e = s.dsk->mostrar(capa, subir, d.value("confirmada", false), salida);
        if (!e.empty()) return falla(QString::fromStdString(e)), true;
        if (subir) dskDePieza_[clave] = {capa, salida}; // baja cuando se termina su vida
        else dskDePieza_.erase(clave);
        bien(subir ? QCoreApplication::translate("EventoViewModel", "Sube la capa %1").arg(capa) : QCoreApplication::translate("EventoViewModel", "Baja la capa %1").arg(capa));
        return true;
    }

    if (z.tipo == "asamblea") {
        // {accion: tratar (punto) | siguiente | punto (punto, estado, resultado?) |
        //  votacion (que: abrir|pausar|reanudar|cerrar|a_quorum, tema, menu, segundos,
        //  resultado: al cerrar, también el resultado oficial; de fábrica, sí)}
        const std::string accion = d.value("accion", std::string());
        const auto numero = [&](const char* k) {
            const json& v = d.contains(k) ? d[k] : json();
            return v.is_number() ? v.get<int>() : v.is_string() ? std::atoi(v.get<std::string>().c_str()) : 0;
        };
        if (accion == "tratar" || accion == "siguiente" || accion == "punto") {
            if (s.ordenDelDia == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay orden del día")), true;
            const int n = numero("punto");
            const std::string e = accion == "siguiente" ? s.ordenDelDia->siguiente()
                                  : accion == "tratar"  ? s.ordenDelDia->punto(n, "en_tratamiento", "")
                                                        : s.ordenDelDia->punto(n, d.value("estado", std::string()), d.value("resultado", std::string()));
            if (!e.empty()) return falla(QString::fromStdString(e)), true;
            bien(accion == "siguiente" ? QCoreApplication::translate("EventoViewModel", "Orden del día: el punto que sigue") : QCoreApplication::translate("EventoViewModel", "Orden del día: punto %1").arg(n));
            return true;
        }
        if (accion == "votacion") {
            if (s.votaciones == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay votaciones")), true;
            const std::string que = d.value("que", std::string());
            const bool conResultado = que == "cerrar" && d.value("resultado", true);
            QPointer<EjecucionLinea> yo(this);
            QPointer<asamblea::Votaciones> vs(s.votaciones);
            s.votaciones->accion(que, d, "linea", [yo, vs, titulo, salida, conResultado](const std::string& e) {
                if (!yo) return;
                if (!e.empty()) {
                    yo->n_.anotar(yo->n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "Votación (%1): %2").arg(titulo, QString::fromStdString(e))), "error");
                    return;
                }
                yo->n_.anotar(yo->n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "Votación: %1").arg(titulo)), "ok");
                // Cerrada: el resultado oficial (a Quórum y al bus: sesion/votacion).
                if (conResultado && vs)
                    vs->accion("a_quorum", json::object(), "linea", [yo, salida](const std::string& e2) {
                        if (yo && !e2.empty())
                            yo->n_.anotar(yo->n_.enSalida(salida, QCoreApplication::translate("EventoViewModel", "El resultado no se pudo cargar: %1").arg(QString::fromStdString(e2))),
                                       "error");
                    });
            });
            return true;
        }
        return falla(QCoreApplication::translate("EventoViewModel", "«%1» no es una acción de la asamblea (tratar, siguiente, punto, votacion)").arg(QString::fromStdString(accion))),
               true;
    }

    if (z.tipo == "playlist") {
        // {accion: play|next|prev|stop, indice?, autoNext?, loop?}: la línea manda sobre la
        // playlist, que sigue siendo la dueña del in/out y del auto-next.
        if (s.playlist == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay playlist")), true;
        if (d.contains("autoNext")) s.playlist->setAutoNext(d.value("autoNext", true));
        if (d.contains("loop")) s.playlist->setLoop(d.value("loop", false));
        const std::string accion = d.value("accion", std::string("play"));
        const std::string e = s.playlist->control(accion, d.value("indice", -1));
        if (!e.empty()) return falla(QString::fromStdString(e)), true;
        if (accion == "play") {
            playlistDePieza_ = a.pieza; // su fin (medio:<pieza>.termino) es cuando la playlist se para sola
            playlistAndando_ = false;
        } else if (accion == "stop") {
            playlistDePieza_.clear();
        }
        bien(QCoreApplication::translate("EventoViewModel", "Playlist: %1").arg(QString::fromStdString(accion)));
        return true;
    }
    return false;
}

void EjecucionLinea::moverPtz(const std::string& clave, bool forzar) {
    const auto it = ptzPendientes_.find(clave);
    if (it == ptzPendientes_.end()) return;
    const PtzPendiente x = it->second;
    ptzPendientes_.erase(it);
    ptz::ControlCamaras* cams = n_.sistema().camaras;
    if (cams == nullptr) return;
    const QVariantMap r = cams->irAPosicion(QString::fromStdString(x.camara), QString::fromStdString(x.posicion), x.segundos);
    if (!r.value("ok").toBool()) {
        n_.anotar(n_.enSalida(x.salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1» a «%2»: %3")
                                      .arg(QString::fromStdString(x.camara), QString::fromStdString(x.posicion), r.value("error").toString())),
               "error");
    } else {
        n_.anotar(n_.enSalida(x.salida, (forzar ? QCoreApplication::translate("EventoViewModel", "PTZ «%1» a «%2» (movida igual, estaba al aire)") : QCoreApplication::translate("EventoViewModel", "PTZ «%1» a «%2»"))
                                      .arg(QString::fromStdString(x.camara), QString::fromStdString(x.posicion))),
               "ok");
    }
    emit n_.correChanged();
}

void EjecucionLinea::mirarMedios() {
    std::map<std::string, std::string> estado;
    for (const auto& m : ventanilla_.foto().medios) estado[m.value("fuente", std::string())] = m.value("estado", std::string());
    std::vector<std::string> terminaron;
    for (const auto& [pieza, medio] : mediosVigilados_) {
        const auto e = estado.find(medio);
        const std::string st = e == estado.end() ? std::string("none") : e->second;
        // Un medio que todavía no arrancó no «terminó».
        if (st == "playing") mediosAndando_.insert(pieza);
        else if (mediosAndando_.count(pieza) && (st == "ended" || st == "stopped" || st == "none" || st == "error"))
            terminaron.push_back(pieza);
    }
    for (const std::string& pieza : terminaron) {
        mediosVigilados_.erase(pieza);
        mediosAndando_.erase(pieza);
        n_.medioTermino(pieza);
    }
    if (mediosVigilados_.empty()) mediosAndando_.clear();
}

std::string EjecucionLinea::decidirPtz(const std::string& pieza, const std::string& decision) {
    for (const auto& [clave, x] : ptzPendientes_) {
        if (x.pieza != pieza) continue;
        const std::string k = clave;
        if (decision == "mover") moverPtz(k, true);
        else if (decision == "saltar") {
            n_.anotar(n_.enSalida(x.salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1»: se deja sin mover").arg(QString::fromStdString(pieza))), "info");
            ptzPendientes_.erase(k);
            emit n_.correChanged();
        } else if (decision != "esperar") {
            return QCoreApplication::translate("EventoViewModel", "La decisión es «mover», «esperar» o «saltar».").toStdString();
        }
        return {};
    }
    return QCoreApplication::translate("EventoViewModel", "No hay ninguna PTZ «%1» esperando.").arg(QString::fromStdString(pieza)).toStdString();
}

} // namespace ba::app
