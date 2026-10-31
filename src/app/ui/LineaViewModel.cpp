// src/app/ui/LineaViewModel.cpp — ver LineaViewModel.hpp
#include "ui/LineaViewModel.hpp"
#include "ui/LineaProductor.hpp"

#include <QQmlEngine>

#include "control/Acciones.hpp"
#include "control/BusEventos.hpp"
#include "emision/BroadcastEngine.hpp"
#include "evento/Condicion.hpp"
#include "evento/Corrida.hpp"
#include "evento/LineaEdicion.hpp"
#include "graficos/Emisor.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/FrameService.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QPainter>
#include <QPointer>
#include <QSet>

#include <algorithm>

namespace ba::app {

using nlohmann::json;
namespace ln = evento::linea;

namespace {

int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("v")
        .toVariant();
}

constexpr int64_t kZoomMinMs = 10'000;
constexpr int64_t kZoomMaxMs = 4 * 3600'000;

/// El texto canónico de una tecla (el mismo que guarda Acciones), para comparar.
QString canonica(const QString& tecla) { return QKeySequence(tecla).toString(QKeySequence::PortableText); }

/// La acción de Acciones (Botonera, Companion, atajos editables) de cada atajo de la página.
QString accionDe(const QString& atajo) {
    if (atajo.startsWith("estante:")) return "linea_adlib:" + atajo.mid(8);
    if (atajo.startsWith("salida:")) return {};
    return "linea_" + atajo;
}

} // namespace

LineaViewModel::LineaViewModel(EventoViewModel& evento, BroadcastEngine& engine, FrameService* frames,
                               control::Acciones* acciones, control::BusEventos* bus, QObject* parent)
    : QObject(parent), ev_(evento), engine_(engine), frames_(frames), acciones_(acciones), bus_(bus) {
    // Todo lo que avisa el evento termina en UN recálculo (en la próxima vuelta del bucle).
    for (auto senal : {&EventoViewModel::guionChanged, &EventoViewModel::correChanged, &EventoViewModel::relojChanged,
                       &EventoViewModel::sucioChanged})
        connect(&ev_, senal, this, &LineaViewModel::pedirRecalculo);
    // Lo que no cambia con el evento: la línea de «ahora», la edad de las fotos.
    segundo_.setInterval(1000);
    // Corriendo, el evento ya avisa cada segundo (relojChanged): este es para cuando no corre.
    connect(&segundo_, &QTimer::timeout, this, [this]() {
        if (!corriendo()) recalcular();
    });
    if (acciones_) {
        // El estante en la Botonera: un adlib por ítem («linea_adlib:<id>»).
        acciones_->ganchos().estante = [this]() {
            const ln::Linea& l = ev_.corridaLinea() ? ev_.corridaLinea()->linea() : ev_.linea();
            return ln::estante(l);
        };
        // Los atajos de Botonera → Atajos también son pulsos («tecla:F5»).
        acciones_->ganchos().pulso = [this](const std::string& clave) { pulsar(clave); };
        connect(acciones_, &control::Acciones::cambio, this, &LineaViewModel::rehacerTeclas);
    }
    rehacerTeclas();
    // El productor (L9): vigila linea/+/overunder y propone, sin ejecutar.
    if (bus_) productor_ = std::make_unique<LineaProductor>(ev_, *bus_);
    if (productor_)
        connect(productor_.get(), &LineaProductor::aviso, this, [this](const QString& t) { emit ev_.notify(t, "warning"); });
    pedirRecalculo();
}

QObject* LineaViewModel::productorQml() const {
    if (productor_) QQmlEngine::setObjectOwnership(productor_.get(), QQmlEngine::CppOwnership);
    return productor_.get();
}

LineaViewModel::~LineaViewModel() {
    if (acciones_) {
        acciones_->ganchos().estante = nullptr;
        acciones_->ganchos().pulso = nullptr;
    }
    if (frames_)
        for (const auto& [escena, ms] : vivas_) frames_->mirarMiniatura(QString::fromStdString(escena), false);
}

// --- Quién mira ---------------------------------------------------------------------------

void LineaViewModel::setMirando(bool on) {
    if (on == mirando_) return;
    mirando_ = on;
    if (on) {
        segundo_.start();
        recalcular();
    } else {
        segundo_.stop();
        // Sin nadie mirando, las vistas vivas se sueltan (a los 10 s, por si vuelve enseguida).
        QTimer::singleShot(kSoltarVivaMs, this, [this]() {
            if (mirando_ || !frames_) return;
            for (const auto& [escena, ms] : vivas_) frames_->mirarMiniatura(QString::fromStdString(escena), false);
            vivas_.clear();
        });
    }
    emit mirandoChanged();
}

void LineaViewModel::setMirandoLinea(bool on) {
    if (on == mirandoLinea_) return;
    mirandoLinea_ = on;
    emit mirandoChanged();
    pedirRecalculo();
}

QStringList LineaViewModel::salidas() const {
    QStringList out;
    for (const std::string& s : ev_.linea().salidasEfectivas()) out << QString::fromStdString(s);
    return out;
}

std::string LineaViewModel::salidaEfectiva() const {
    const auto ss = ev_.linea().salidasEfectivas();
    return std::find(ss.begin(), ss.end(), salida_) != ss.end() ? salida_ : ev_.salidaPorOmision();
}

void LineaViewModel::setSalida(const QString& s) {
    if (s.toStdString() == salida_) return;
    salida_ = s.toStdString();
    // Los atajos de la Botonera manejan la misma (si es una salida del programa).
    if (acciones_) acciones_->setSalida(salida_);
    emit salidaChanged();
    pedirRecalculo();
}

QString LineaViewModel::estado() const { return ev_.estado(); }

bool LineaViewModel::corriendo() const {
    const QString e = ev_.estado();
    return e == "corriendo" || e == "pausado";
}

// --- El recálculo -------------------------------------------------------------------------

void LineaViewModel::pedirRecalculo() {
    if (recalculoPedido_) return;
    recalculoPedido_ = true;
    QTimer::singleShot(0, this, &LineaViewModel::recalcular);
}

bool LineaViewModel::intocable(const std::string& paso, std::string* porque) const {
    // Como linea_editar: en vivo, lo emitido (y lo que está al aire) no se toca.
    const ln::Corrida* c = ev_.corridaLinea();
    if (c == nullptr || !corriendo()) return false;
    for (const ln::Instancia& i : c->instancias()) {
        if (i.paso != paso) continue;
        if (porque)
            *porque = (i.hastaMs == 0 ? tr("Está al aire: lo emitido no se edita.")
                                      : tr("Ya salió al aire: lo emitido no se edita."))
                          .toStdString();
        return true;
    }
    return false;
}

void LineaViewModel::recalcular() {
    recalculoPedido_ = false;
    const int64_t t = ahoraMs();
    const json estado = ev_.lineaEstado();
    publicarBus(estado);
    const QString est = ev_.estado();
    const bool cambioEstado = est != estado_;
    estado_ = est;
    if (!mirando_) {
        if (cambioEstado) emit vistaChanged();
        return;
    }

    const ln::Corrida* c = ev_.corridaLinea();
    const ln::Linea& plan = ev_.linea();
    // Lo pendiente sobre el plan: si ya no se puede aplicar (cambió lo de abajo), se descarta.
    ln::Edicion ed;
    const bool hayPendientes = !pendientes_.empty();
    if (hayPendientes) {
        ed = ln::editar(plan, pendientes_, [this](const std::string& p) { return intocable(p, nullptr); });
        if (!ed.ok) {
            pendientes_ = json::array();
            pendientesUi_.clear();
            emit pendientesChanged();
            emit ev_.notify(tr("Los cambios sin confirmar ya no entran en la línea: se descartaron (%1).")
                           .arg(QString::fromStdString(ed.error)),
                       "warn");
        }
    }
    const bool conEdicion = hayPendientes && ed.ok;
    // La línea que se ve (con lo pendiente) y la que corre (para la conducción).
    ln::Resolucion real;
    if (c) {
        ln::Corrida vista = *c;
        if (conEdicion) vista.setLinea(ed.linea);
        resolucionVista_ = vista.resolver(t);
        lineaVista_ = vista.linea();
        real = conEdicion ? c->resolver(t) : resolucionVista_;
    } else {
        ln::Entrada e = ev_.entradaLinea(false);
        e.ahoraMs = t;
        lineaVista_ = conEdicion ? ed.linea : plan;
        resolucionVista_ = ln::resolver(lineaVista_, e);
        real = conEdicion ? ln::resolver(plan, e) : resolucionVista_;
    }
    const ln::Linea& lineaReal = c ? c->linea() : plan;
    const std::string s = salidaEfectiva();

    ln::ContextoVista ctx;
    ctx.ahoraMs = t;
    ctx.salida = s;
    if (c) {
        ctx.instancias = c->instancias();
        if (const ln::Instancia* i = c->alAire(s))
            if (const ln::Paso* p = lineaReal.paso(i->paso))
                for (const ln::Pieza& z : p->piezas)
                    if (c->disparada(s, z.id)) ctx.disparadas.insert(z.id);
    }
    ctx.editable = [this](const std::string& paso, std::string& porque) { return !intocable(paso, &porque); };
    if (conEdicion)
        for (const json& d : ln::diferencias(plan, ed.linea)) ctx.pendientes.insert(d.value("id", std::string()));
    ctx.hora = [this](int64_t ms) { return ev_.horaDe(ms); };
    ctx.miniatura = [s](const std::string& escena, ln::FuenteMini f) -> json {
        // Las claves de FrameService: el programa y el PVW de la salida, o la vista chica.
        std::string clave;
        if (f == ln::FuenteMini::Programa) clave = "program:" + s;
        else if (f == ln::FuenteMini::Pvw) clave = "armada:" + s;
        else if (f == ln::FuenteMini::Viva) clave = "mini:" + escena;
        return {{"fuente", ln::nombreDe(f)}, {"clave", clave}, {"escena", escena}};
    };

    // La escaleta y el Storyboard (lo que viene).
    json esc = ln::escaleta(lineaVista_, resolucionVista_, ctx);
    bool cambio = cambioEstado;
    if (esc != escaletaJ_) {
        escaletaJ_ = esc;
        escaleta_ = aQv(esc).toList();
        json sb = json::array();
        for (const json& b : esc)
            for (const json& p : b["pasos"]) {
                const std::string e = p.value("estado", std::string());
                if (e == "alAire" || e == "proximo" || e == "futuro") sb.push_back(p);
            }
        storyboard_ = aQv(sb).toList();
        cambio = true;
    }
    // La conducción: lo real (sin lo pendiente), sobre el estado de la salida.
    json sal = json::object();
    for (const json& x : estado.value("salidas", json::array()))
        if (x.value("salida", std::string()) == s) sal = x;
    ln::ContextoVista ctxReal = ctx;
    ctxReal.pendientes.clear();
    json cond = ln::conduccion(lineaReal, real, ctxReal, sal);
    cond["hora"] = ev_.horaDe(t);
    for (const char* k : {"transcurridoSeg", "pausadaSeg", "arranque"})
        if (estado.contains(k)) cond[k] = estado[k];
    // Las PTZ que esperan que su cámara salga del aire (de la línea entera: las
    // cámaras se comparten), para «mover igual» o «no mover» desde la conducción.
    cond["ptzPendientes"] = estado.value("ptzPendientes", json::array());
    if (cond != conduccionJ_) {
        conduccionJ_ = cond;
        conduccion_ = aQv(cond).toMap();
        cambio = true;
    }
    const json est2 = ln::estante(lineaReal);
    std::vector<std::string> pulsos;
    for (const std::string& p : ln::pulsosDe(lineaReal))
        if (p.rfind("tecla:", 0) == 0) pulsos.push_back(p.substr(6));
    if (est2 != estanteJ_ || pulsos != pulsos_) {
        estanteJ_ = est2;
        estante_ = aQv(est2).toList();
        pulsos_ = std::move(pulsos);
        rehacerTeclas();
        cambio = true;
    }
    // La edad de las fotos («hace 20 s»).
    QVariantMap edades;
    for (const auto& [escena, ms] : fotoMs_)
        if (fotos_.contains(QString::fromStdString(escena))) edades[QString::fromStdString(escena)] = static_cast<int>((t - ms) / 1000);
    if (edades != edadFotos_) {
        edadFotos_ = edades;
        cambio = true;
    }
    if (cambio) emit vistaChanged();

    // La Línea multipista: la ventana con «ahora» a un tercio (o corrida a mano).
    if (mirandoLinea_) {
        int64_t foco = t + corrimientoMs_;
        if (corrimientoMs_ == 0 && !corriendo())
            if (const ln::SalidaResuelta* sr = resolucionVista_.salida(s); sr && !sr->pasos.empty())
                foco = std::max(t, sr->pasos.front().frenteMs); // sin correr: desde que arranca
        ventana_.desdeMs = foco - zoomMs_ / 3;
        ventana_.hastaMs = ventana_.desdeMs + zoomMs_;
        const json mp = ln::multipista(lineaVista_, resolucionVista_, ctx, ventana_, ev_.entradaLinea(c != nullptr).medianocheMs);
        if (mp != multipistaJ_) {
            multipistaJ_ = mp;
            multipista_ = aQv(mp).toMap();
            emit multipistaChanged();
        }
    }

    if (const ln::SalidaResuelta* sr = resolucionVista_.salida(s)) {
        miniaturas(ln::presupuestoMiniaturas(lineaVista_, *sr), t);
        miniaturasConGraficos(*sr, t);
    }
}

void LineaViewModel::publicarBus(const json& estado) {
    if (bus_ == nullptr) return;
    // Lo que cambia de a poco (no los segundos que lleva): el bus no avisa si no cambió.
    for (const json& x : estado.value("salidas", json::array())) {
        const std::string s = x.value("salida", std::string());
        json v{{"estado", estado.value("estado", std::string())},
               {"hold", x.value("hold", false)},
               {"alAire", nullptr},
               {"proximo", nullptr},
               {"overUnderSeg", x.value("overUnderSeg", 0)}};
        const json a = x.value("alAire", json()), p = x.value("proximo", json()), h = x.value("proximoHardStart", json());
        if (a.is_object()) v["alAire"] = {{"paso", a.value("paso", "")}, {"nombre", a.value("nombre", "")}};
        if (p.is_object())
            v["proximo"] = {{"paso", p.value("paso", "")},
                            {"nombre", p.value("nombre", "")},
                            {"automatico", p.value("automatico", false)},
                            {"entraSoloEnSeg", p.value("entraSoloEnSeg", 0)}};
        if (h.is_object())
            v["proximoHardStart"] = {{"paso", h.value("paso", "")}, {"hora", h.value("hora", json())}, {"gapSeg", h.value("gapSeg", 0)}};
        bus_->publicar("linea/" + s + "/estado", v);
        bus_->publicar("linea/" + s + "/proximo", v["proximo"]);
        // El over/under aparte: lo vigila el productor (ui/LineaProductor) y lo puede
        // mirar un disparador sin despertarse con cada cambio del estado.
        bus_->publicar("linea/" + s + "/overunder", {{"seg", x.value("overUnderSeg", 0)},
                                                      {"fin", x.value("fin", json())},
                                                      {"finPlaneado", x.value("finPlaneado", json())},
                                                      {"corrida", estado.value("estado", std::string())}});
    }
}

// --- Las miniaturas ------------------------------------------------------------------------

void LineaViewModel::miniaturas(const std::map<std::string, ln::FuenteMini>& presupuesto, int64_t ahora) {
    if (frames_ == nullptr) return;
    for (const auto& [escena, f] : presupuesto) {
        if (f != ln::FuenteMini::Viva) continue;
        if (!vivas_.count(escena)) frames_->mirarMiniatura(QString::fromStdString(escena), true);
        vivas_[escena] = ahora;
    }
    // Las que ya no hacen falta, a los 10 s.
    for (auto it = vivas_.begin(); it != vivas_.end();) {
        if (ahora - it->second < kSoltarVivaMs) {
            ++it;
            continue;
        }
        frames_->mirarMiniatura(QString::fromStdString(it->first), false);
        it = vivas_.erase(it);
    }
    // Las fotos: de a una, solo de lo que se ve y cuando la anterior ya tiene sus 20 s.
    if (fotoEnCurso_) return;
    for (const auto& [escena, f] : presupuesto) {
        if (f != ln::FuenteMini::Foto) continue;
        const auto v = fotosVisibles_.find(escena);
        if (v == fotosVisibles_.end() || v->second <= 0) continue;
        const auto ya = fotoMs_.find(escena);
        if (ya != fotoMs_.end() && ahora - ya->second < kFotoCadaMs) continue;
        tomarFoto(escena);
        return;
    }
}

void LineaViewModel::tomarFoto(const std::string& escena) {
    fotoEnCurso_ = true;
    fotoMs_[escena] = ahoraMs();
    QPointer<LineaViewModel> yo(this);
    // La captura espera el cuadro en otro hilo y contesta desde ahí: se vuelve a este.
    engine_.capturarAparte(escena, 320, [yo, escena](const std::string& error, std::vector<std::uint8_t> rgba, int w, int h) {
        QImage img;
        if (error.empty() && w > 0 && h > 0 && rgba.size() >= static_cast<std::size_t>(w) * h * 4)
            img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, escena, img]() {
                if (!yo) return;
                yo->fotoEnCurso_ = false;
                if (img.isNull()) return;
                yo->fotos_[QString::fromStdString(escena)] = img;
                emit yo->fotosChanged();
            },
            Qt::QueuedConnection);
    });
}

void LineaViewModel::mirarFoto(const QString& escena, bool mirar) {
    if (escena.isEmpty()) return;
    int& n = fotosVisibles_[escena.toStdString()];
    n = std::max(0, n + (mirar ? 1 : -1));
    if (mirar && n == 1) pedirRecalculo(); // si no tiene foto, que la saque ya
}

// --- Las miniaturas «con gráficos» (L10) ------------------------------------------------------
//
// «Con lo que va a tener»: la escena del paso con los gráficos que el paso pone solos
// (no los adlibs), cada uno con sus valores y en su cuadro estable. Se dibuja APARTE,
// nunca en el aire: la foto de la escena (la de capturarAparte, cada 20 s) y encima cada
// gráfico dibujado por el renderizador de gráficos (Emisor::dibujarQuieto), al tamaño de
// su fuente y en el LUGAR que esa fuente ocupa en la escena (itemsVisibles: fracciones
// del lienzo). Si la fuente no está en la escena (va en un DSK, o todavía no existe), a
// pantalla completa. Los de plugin contestan después: la compuesta se arma al llegar.
//
// EL PRESUPUESTO. Solo el próximo y los dos que siguen; cada gráfico se dibuja UNA vez
// por (gráfico, valores, fuente) y de a uno cada 2 s (el renderizador hace esperar a
// quien pide); componer es pintar imágenes chicas: la capa se guarda ya achicada al
// ancho de la miniatura. Lo que sale del presupuesto se suelta.

namespace {

struct GraficoDePaso {
    std::string grafico, fuente, clave;
    json valores;
};

/// Los gráficos que un paso pone solo, con la clave de su capa (gráfico, fuente, valores).
std::vector<GraficoDePaso> graficosDePaso(const ln::Paso& p) {
    std::vector<GraficoDePaso> out;
    for (const ln::Pieza& z : p.piezas) {
        if (z.tipo != "grafico" || z.desde.modo == ln::Ancla::Modo::Manual) continue;
        const std::string accion = z.datos.value("accion", std::string("mostrar"));
        GraficoDePaso g{z.datos.value("grafico", std::string()), z.datos.value("fuente", std::string()), {},
                        z.datos.value("valores", json::object())};
        if (accion != "mostrar" || g.grafico.empty()) continue;
        g.clave = g.grafico + "|" + g.fuente + "|" + g.valores.dump(-1, ' ', false, json::error_handler_t::replace);
        out.push_back(std::move(g));
    }
    return out;
}

/// Ancho de una capa guardada: el de la miniatura (la foto es de 320).
constexpr int kAnchoCapa = 320;

} // namespace

void LineaViewModel::miniaturasConGraficos(const ln::SalidaResuelta& sr, int64_t ahora) {
    graficos::Emisor* em = ev_.emisorGraficos();
    std::set<std::string> quedan;
    bool cambio = false;
    std::size_t vistos = 0;
    for (const ln::PasoResuelto& pr : sr.pasos) {
        if (pr.estado != ln::EstadoPaso::Proximo && pr.estado != ln::EstadoPaso::Futuro) continue;
        if (vistos++ >= kConGraficos) break;
        const ln::Paso* p = lineaVista_.paso(pr.paso);
        if (p == nullptr || em == nullptr) continue;
        const auto gs = graficosDePaso(*p);
        const std::string escena = ln::escenaDePaso(lineaVista_, *p);
        if (gs.empty() || escena.empty()) continue;
        quedan.insert(p->id);
        // La foto de la escena: la que hay o una nueva (de a una, cada 20 s).
        const QString qe = QString::fromStdString(escena);
        const auto ya = fotoMs_.find(escena);
        if (!fotoEnCurso_ && (ya == fotoMs_.end() || ahora - ya->second >= kFotoCadaMs)) tomarFoto(escena);
        if (!fotos_.contains(qe)) continue;
        const QImage foto = fotos_.value(qe).value<QImage>();
        if (foto.isNull()) continue;
        // Cada gráfico, una vez (y de a uno cada tanto).
        std::string firma = escena + "|" + std::to_string(fotoMs_[escena]);
        bool completos = true;
        for (const GraficoDePaso& g : gs) {
            firma += "|" + g.clave;
            if (capas_.count(g.clave)) continue;
            completos = false;
            if (capasEnCurso_.count(g.clave) || ahora - ultimoDibujoMs_ < kDibujoCadaMs) continue;
            ultimoDibujoMs_ = ahora;
            capasEnCurso_.insert(g.clave);
            const QJsonObject qv = QJsonDocument::fromJson(QByteArray::fromStdString(g.valores.dump())).object();
            QPointer<LineaViewModel> yo(this);
            // Nula si no se pudo: la compuesta va sin ese gráfico (y no se vuelve a pedir).
            em->dibujarQuieto(QString::fromStdString(g.grafico), qv, QString::fromStdString(g.fuente),
                              [yo, clave = g.clave](QImage img, QString) {
                                  if (!yo) return;
                                  yo->capasEnCurso_.erase(clave);
                                  if (!img.isNull() && img.width() > kAnchoCapa)
                                      img = img.scaledToWidth(kAnchoCapa, Qt::SmoothTransformation);
                                  yo->capas_[clave] = img;
                                  if (yo->capas_.size() > 64) yo->capas_.erase(yo->capas_.begin());
                                  yo->pedirRecalculo(); // la compuesta, con esta capa
                              });
        }
        if (!completos || firmas_[p->id] == firma) continue;
        // Dónde va cada uno: el lugar de su fuente en la escena (en la foto de ahora).
        std::map<std::string, CanvasRect> lugares;
        for (const auto& [fuente, r] : engine_.itemsVisibles(escena)) lugares[fuente] = r;
        QImage out = foto.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter pintor(&out);
        pintor.setRenderHint(QPainter::SmoothPixmapTransform);
        for (const GraficoDePaso& g : gs) {
            const QImage& capa = capas_[g.clave];
            if (capa.isNull()) continue;
            const auto l = lugares.find(g.fuente);
            const QRectF donde = l == lugares.end() ? QRectF(out.rect())
                                                    : QRectF(l->second.x * out.width(), l->second.y * out.height(),
                                                             l->second.w * out.width(), l->second.h * out.height());
            pintor.drawImage(donde, capa);
        }
        pintor.end();
        firmas_[p->id] = firma;
        conGraficos_[QString::fromStdString(p->id)] = out;
        cambio = true;
    }
    // Lo que salió del presupuesto (ya entró, se movió) se suelta.
    for (auto it = firmas_.begin(); it != firmas_.end();) {
        if (quedan.count(it->first)) {
            ++it;
            continue;
        }
        conGraficos_.remove(QString::fromStdString(it->first));
        it = firmas_.erase(it);
        cambio = true;
    }
    if (cambio) emit fotosChanged();
}

// --- Los atajos -----------------------------------------------------------------------------

void LineaViewModel::rehacerTeclas() {
    // Los de 3.5 D. El que la persona reasignó en Botonera → Atajos (a la misma acción
    // «linea_…») lo atiende ese atajo; una tecla que usa otra acción no se pisa.
    QVariantList cand;
    const auto suma = [&cand](const QString& accion, const QString& tecla) { cand << QVariantMap{{"accion", accion}, {"tecla", tecla}}; };
    suma("go", "Space");
    suma("go", "F12");
    suma("hold", "H");
    suma("anterior", "F9");
    suma("siguiente", "F10");
    suma("saltar", "Shift+F9");
    for (int i = 1; i <= 4; ++i) suma(QString("salida:%1").arg(i), QString("Ctrl+%1").arg(i));
    for (const QVariant& v : estante_) {
        const QVariantMap m = v.toMap();
        if (!m.value("tecla").toString().isEmpty()) suma("estante:" + m.value("id").toString(), m.value("tecla").toString());
    }
    // Las teclas que espera una condición («tecla:F5»): un pulso, si nadie la usa.
    for (const std::string& t : pulsos_) suma("pulso:" + QString::fromStdString(t), QString::fromStdString(t));
    QSet<QString> ocupadas;
    QSet<QString> reasignadas;
    if (acciones_)
        for (const auto& a : acciones_->atajos()) {
            ocupadas.insert(canonica(QString::fromStdString(a.value("tecla", std::string()))));
            reasignadas.insert(QString::fromStdString(a.value("accion", std::string())));
        }
    QVariantList out;
    for (const QVariant& v : cand) {
        const QVariantMap m = v.toMap();
        if (ocupadas.contains(canonica(m.value("tecla").toString()))) continue;
        const QString a = accionDe(m.value("accion").toString());
        if (!a.isEmpty() && reasignadas.contains(a)) continue;
        out << m;
    }
    if (out == teclas_) return;
    teclas_ = out;
    emit teclasChanged();
}

QString LineaViewModel::atajo(const QString& accion) {
    if (accion == "go") return go();
    if (accion == "hold") return hold();
    if (accion == "anterior") return anterior();
    if (accion == "siguiente") return siguiente();
    if (accion == "saltar") return saltar();
    if (accion.startsWith("salida:")) {
        const QStringList ss = salidas();
        const int i = accion.mid(7).toInt() - 1;
        if (i < 0 || i >= ss.size()) return tr("La línea no tiene una salida %1.").arg(i + 1);
        setSalida(ss[i]);
        return {};
    }
    if (accion.startsWith("estante:")) return adlib(accion.mid(8));
    if (accion.startsWith("pulso:")) {
        pulsar("tecla:" + accion.mid(6).toStdString());
        return {};
    }
    return tr("No hay un atajo «%1».").arg(accion);
}

void LineaViewModel::pulsar(const std::string& clave) {
    // Por el bus, como la botonera: lo atiende la línea (y lo puede mirar un plugin).
    if (bus_ != nullptr) bus_->publicar("linea/pulso", clave, true);
    else ev_.lineaPulso(clave);
}

// --- Vivo -------------------------------------------------------------------------------------

QString LineaViewModel::resultado(const std::string& error) {
    pedirRecalculo();
    return QString::fromStdString(error);
}

QString LineaViewModel::go() {
    // Arrancar es otra cosa (con la revisión previa): un Espacio suelto no saca la línea al aire.
    if (!corriendo()) return tr("La línea no está corriendo: arrancala con «Arrancar».");
    return resultado(ev_.lineaGo(salidaEfectiva()));
}
QString LineaViewModel::hold() { return resultado(ev_.lineaHold(salidaEfectiva(), std::nullopt)); }
QString LineaViewModel::siguiente() { return resultado(ev_.lineaMoverProximo(salidaEfectiva(), 1)); }
QString LineaViewModel::anterior() { return resultado(ev_.lineaMoverProximo(salidaEfectiva(), -1)); }
QString LineaViewModel::saltar() { return resultado(ev_.lineaSaltar(salidaEfectiva())); }
QString LineaViewModel::repetir() { return resultado(ev_.lineaRepetir(salidaEfectiva())); }
QString LineaViewModel::irA(const QString& paso) { return resultado(ev_.lineaIr(salidaEfectiva(), paso.toStdString())); }
QString LineaViewModel::insertar(const QString& item, bool alAire) {
    return resultado(ev_.lineaInsertar(salidaEfectiva(), item.toStdString(), alAire));
}

QString LineaViewModel::adlib(const QString& id) {
    // Un paso del estante no se «dispara»: va de próximo.
    const ln::Linea& l = ev_.corridaLinea() ? ev_.corridaLinea()->linea() : ev_.linea();
    for (const ln::ItemEstante& it : l.estante)
        if (it.id == id.toStdString() && it.paso) return insertar(id, false);
    return resultado(ev_.lineaAdlib(salidaEfectiva(), id.toStdString()));
}

// --- La ventana ---------------------------------------------------------------------------

void LineaViewModel::zoom(int nivel) {
    int64_t z = zoomMs_;
    const ln::SalidaResuelta* sr = resolucionVista_.salida(salidaEfectiva());
    if (nivel == 1 && sr && !sr->bloques.empty()) {
        // El bloque al aire (o el primero): que entre entero desde «ahora».
        const ln::BloqueResuelto* b = &sr->bloques.front();
        if (const ln::PasoResuelto* p = sr->paso(sr->alAire))
            for (const ln::BloqueResuelto& x : sr->bloques)
                if (x.bloque == p->bloque) b = &x;
        z = (b->hastaMs - b->desdeMs) * 3 / 2;
    } else if (nivel == 2 && sr && !sr->pasos.empty()) {
        z = (sr->finMs - sr->pasos.front().frenteMs) * 3 / 2;
    } else if (nivel == 3) {
        z = 5 * 60'000;
    } else if (nivel == 4) {
        z = 60'000;
    }
    zoomMs_ = std::clamp(z, kZoomMinMs, kZoomMaxMs);
    corrimientoMs_ = 0;
    pedirRecalculo();
}

void LineaViewModel::acercar(double factor) {
    if (factor <= 0) return;
    zoomMs_ = std::clamp(static_cast<int64_t>(static_cast<double>(zoomMs_) * factor), kZoomMinMs, kZoomMaxMs);
    pedirRecalculo();
}

void LineaViewModel::correr(double fraccion) {
    corrimientoMs_ += static_cast<int64_t>(fraccion * static_cast<double>(zoomMs_));
    pedirRecalculo();
}

void LineaViewModel::alAhora() {
    corrimientoMs_ = 0;
    pedirRecalculo();
}

// --- Editar el futuro (pendiente hasta confirmar) -----------------------------------------------

QString LineaViewModel::sumarPendiente(const json& op) {
    json ops = pendientes_;
    ops.push_back(op);
    const ln::Edicion ed = ln::editar(ev_.linea(), ops, [this](const std::string& p) { return intocable(p, nullptr); });
    if (!ed.ok) return QString::fromStdString(ed.error);
    pendientes_ = std::move(ops);
    // En palabras: lo que cambió contra el plan (no la lista de operaciones).
    QVariantList ui;
    for (const json& d : ln::diferencias(ev_.linea(), ed.linea)) {
        const std::string que = d.value("que", std::string()), id = d.value("id", std::string());
        const ln::Paso* p = ed.linea.paso(id);
        const QString nombre = QString::fromStdString(p ? p->nombre : id);
        ui << QVariantMap{{"id", QString::fromStdString(id)},
                          {"texto", que == "movido" ? tr("«%1» cambia de lugar").arg(nombre)
                                    : que == "agregado" ? tr("«%1» se agrega").arg(nombre)
                                    : que == "quitado" ? tr("«%1» se quita").arg(nombre)
                                                        : tr("«%1» cambia").arg(nombre)}};
    }
    pendientesUi_ = ui;
    emit pendientesChanged();
    pedirRecalculo();
    return {};
}

QString LineaViewModel::estirar(const QString& paso, double dx) {
    const ln::Paso* p = lineaVista_.paso(paso.toStdString());
    if (p == nullptr) return tr("No hay un paso «%1».").arg(paso);
    std::string porque;
    if (intocable(p->id, &porque)) return QString::fromStdString(porque);
    const int64_t ms = p->duracionPlanMs() + ventana_.ms(dx) - ventana_.desdeMs;
    const int seg = static_cast<int>(std::max<int64_t>(1000, ms) / 1000);
    json campos{{"duracionPrevistaSeg", seg}};
    // Un paso abierto sigue abierto: solo cambia lo que se prevé.
    if (p->fin.modo == ln::ModoFin::Duracion) campos["fin"] = {{"duracion", seg}};
    return sumarPendiente({{"op", "cambiar"}, {"id", p->id}, {"campos", campos}});
}

QString LineaViewModel::mover(const QString& paso, double x) {
    const std::string id = paso.toStdString();
    std::string porque;
    if (intocable(id, &porque)) return QString::fromStdString(porque);
    const ln::SalidaResuelta* sr = resolucionVista_.salida(salidaEfectiva());
    if (sr == nullptr || lineaVista_.paso(id) == nullptr) return tr("No hay un paso «%1».").arg(paso);
    const int64_t ms = ventana_.ms(x);
    // Detrás del último que arranca antes de donde se soltó (sin contarse a sí mismo).
    const ln::PasoResuelto* detras = nullptr;
    for (const ln::PasoResuelto& pr : sr->pasos)
        if (pr.paso != id && pr.estado != ln::EstadoPaso::Salteado && pr.frenteMs <= ms) detras = &pr;
    if (detras == nullptr) {
        if (sr->pasos.empty()) return {};
        // Antes de todo: al principio del primer bloque.
        return sumarPendiente({{"op", "mover"}, {"id", id}, {"bloque", sr->pasos.front().bloque}, {"despuesDe", ""}});
    }
    return sumarPendiente({{"op", "mover"}, {"id", id}, {"bloque", detras->bloque}, {"despuesDe", detras->paso}});
}

QString LineaViewModel::moverPieza(const QString& pieza, double dx) {
    const std::string id = pieza.toStdString();
    for (const ln::Bloque& b : lineaVista_.bloques)
        for (const ln::Paso& p : b.pasos)
            for (const ln::Pieza& z : p.piezas) {
                if (z.id != id) continue;
                std::string porque;
                if (intocable(p.id, &porque)) return QString::fromStdString(porque);
                if (z.desde.modo != ln::Ancla::Modo::Ms)
                    return tr("«%1» no entra a una hora del paso: se mueve desde su ficha.").arg(QString::fromStdString(z.texto.empty() ? z.id : z.texto));
                const int64_t ms = std::clamp<int64_t>(z.desde.ms + ventana_.ms(dx) - ventana_.desdeMs, 0, p.duracionPlanMs());
                return sumarPendiente({{"op", "cambiar"}, {"id", id}, {"campos", {{"desde", {{"ms", ms}}}}}});
            }
    return tr("No hay una pieza «%1».").arg(pieza);
}

QString LineaViewModel::reordenar(const QString& paso, const QString& bloque, const QString& detrasDe) {
    const std::string id = paso.toStdString();
    if (id == detrasDe.toStdString()) return {};
    std::string porque;
    if (intocable(id, &porque)) return QString::fromStdString(porque);
    return sumarPendiente({{"op", "mover"}, {"id", id}, {"bloque", bloque.toStdString()}, {"despuesDe", detrasDe.toStdString()}});
}

QString LineaViewModel::confirmar() {
    if (pendientes_.empty()) return {};
    const json r = ev_.lineaEditar(pendientes_, false);
    if (!r.value("ok", false)) return QString::fromStdString(r.value("error", std::string()));
    descartar();
    return {};
}

void LineaViewModel::descartar() {
    pendientes_ = json::array();
    pendientesUi_.clear();
    emit pendientesChanged();
    pedirRecalculo();
}

} // namespace ba::app
