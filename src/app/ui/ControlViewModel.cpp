// src/app/ui/ControlViewModel.cpp — ver el .hpp

#include "ui/ControlViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "asamblea/ConferenciaDcn.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "control/Acciones.hpp"
#include "control/Botonera.hpp"
#include "control/BotoneraGateway.hpp"
#include "control/BusEventos.hpp"
#include "control/CarasBotonera.hpp"
#include "control/ControlWeb.hpp"
#include "control/ControlWebGateway.hpp"
#include "control/Dsk.hpp"
#include "control/SatelliteServidor.hpp"
#include "control/TemasWeb.hpp"
#include "emision/FotoMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "plugins/Plugins.hpp"
#include "ptz/ControlCamaras.hpp"
#include "sala/Direcciones.hpp"
#include "sala/Pedido.hpp"
#include "ui/AsambleaViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/ListModels.hpp"
#include "ui/PdfEscena.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlEngine>

#include <set>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

ControlViewModel::ControlViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    armarBotonera();
    satellite_ = std::make_unique<control::SatelliteServidor>(p_.rt.dataRoot(), *botonera_);
    // Las capas DSK: el modelo es del dueño del motor (lo que le pide al motor corre allá);
    // la interfaz lo ve por la foto (control::Dsk).
    {
        AppRuntime& rt = p_.rt;
        control::CapasDsk::Motor m;
        m.armar = [&rt](int capa, const std::vector<std::string>& cont, const nlohmann::json& recuadro, const std::string& salida) {
            return rt.engine().dskArmar(capa, cont, recuadro, salida);
        };
        m.mostrar = [&rt](int capa, bool visible, int ms, const std::string& salida) {
            return rt.engine().dskMostrar(capa, visible, ms, salida);
        };
        m.estado = [&rt](const std::string& salida) { return rt.engine().dskEstado(salida); };
        m.error = [&rt]() { return rt.engine().lastError(); };
        capasDsk_ = std::make_unique<control::CapasDsk>(rt.dataRoot(), std::move(m));
        capasDsk_->setAlCambiar([&rt]() { rt.fotoSucia(FotoMotor::Superficies); });
        // En la foto, con las capas de cada salida (se rearman con la lista de salidas y en
        // cada vuelta del motor, que es cuando se mueve un fundido).
        aporteDsk_ = rt.aportarALaFoto([c = capasDsk_.get()](FotoMotor& f, unsigned partes) {
            if ((partes & (FotoMotor::Superficies | FotoMotor::Salidas)) == 0) return;
            std::vector<std::string> ids;
            for (const FotoMotor::Salida& s : f.salidas) ids.push_back(s.id);
            f.dsk = c->paraLaFoto(ids);
        });
    }
    dsk_ = std::make_unique<control::Dsk>(*capasDsk_, p_.ventanilla);
    connect(dsk_.get(), &control::Dsk::fallo, this, [this](const QString& e) { emit notify(e, "error"); });
    acciones_ = std::make_unique<control::Acciones>(p_.rt.dataRoot());
    {
        auto& g = acciones_->ganchos();
        g.escenas = [this]() { return escenas(); };
        g.fuentesAudio = [this]() { return fuentesAudio(); };
        g.mudo = [this](const std::string& f, const std::string& salida) { return mudo(f, salida); };
        g.salidas = p_.salidas;
        g.dskArriba = [this](int capa, const std::string& salida) { return dsk_->arriba(capa, salida); };
    }
    dsk_->setAlCambiar([this](const nlohmann::json& c) {
        p_.bus.publicar("aire/dsk", c);
        emit dskCambio(); // lo que muestra una capa también está al aire
    });
    // Lo que el área lee de la foto: las escenas (para elegir el contenido de una capa) y
    // el tema web.
    connect(&p_.ventanilla, &VentanillaMotor::cambio, this, &ControlViewModel::alCambiarFoto);
    temaWeb_ = temaWebMandado_ = p_.ventanilla.foto().temaWeb;
    alCambiarFoto(FotoMotor::Escenas);
    ConferenceClient& cli = p_.sala.client();
    // La botonera con el gateway (tablets: eventos, credenciales, emparejar): su modelo.
    botoneraGw_ = std::make_unique<control::BotoneraGateway>(*botonera_, cli);
    connect(botoneraGw_.get(), &control::BotoneraGateway::aviso, this,
            [this](const QString& texto, const QString& nivel) { emit notify(texto, nivel); });
    // Los aparatos de plugins y del satélite (las tablets van por BotoneraGateway).
    connect(botonera_.get(), &control::Botonera::mensaje, this, [this](const QString& dev, const nlohmann::json& msg) {
        const std::string origen = botonera_->origenDe(dev.toStdString());
        if (origen == "plugin" && p_.plugins)
            p_.plugins->superficieEstado(dev.toStdString(), msg);
        else if (origen == "satellite" && satellite_)
            satellite_->mensaje(dev.toStdString(), msg);
    });
    // El control web (dashboard /control/): sus aparatos y el puente con el gateway (como la botonera).
    controlWeb_ = std::make_unique<control::ControlWeb>(p_.rt.dataRoot());
    controlWeb_->ganchos().salidas = p_.salidas;
    controlWebGw_ = std::make_unique<control::ControlWebGateway>(*controlWeb_, cli);
    // El tema de las páginas web: cada vez que la sala (re)arranca, se lo vuelve a pasar.
    connect(&p_.sala, &ConferenceViewModel::onlineChanged, this, [this]() {
        if (p_.sala.online()) mandarTemaWeb();
    });
    connect(controlWebGw_.get(), &control::ControlWebGateway::aviso, this,
            [this](const QString& texto, const QString& nivel) { emit notify(texto, nivel); });
    connect(controlWeb_.get(), &control::ControlWeb::solicitudNueva, this,
            [this](const QString&, const QString& nombre, const QString& ip) {
                emit notify(tr("«%1» (%2) pide entrar al control web: aprobalo en Servicios → Control web.").arg(nombre, ip),
                            "warn");
            });
    connect(botonera_.get(), &control::Botonera::solicitudNueva, this,
            [this](const QString&, const QString& nombre, const QString& perfil, const QString& ip) {
                emit notify(tr("«%1» (%2) pide emparejarse como botonera con el perfil «%3»: aprobalo en la página "
                               "Botonera.")
                                .arg(nombre, ip, perfil),
                            "warn");
            });
    // Los aparatos de hardware de los plugins (Stream Deck, MIDI…) son botoneras más.
    if (p_.plugins)
        p_.plugins->setSuperficies(
            [this](const std::string& id, const std::string& nombre, const nlohmann::json& cap) {
                botonera_->registrarLocal(id, nombre, "plugin", cap);
            },
            [this](const std::string& id) { botonera_->quitarLocal(id); },
            [this](const std::string& id, const nlohmann::json& msg) { botonera_->entrada(id, msg); });
}

ControlViewModel::~ControlViewModel() {
    // Que el dueño no lo use más para la foto (espera a la que esté armando).
    if (aporteDsk_ != 0) p_.rt.dejarDeAportar(aporteDsk_);
}

std::vector<std::string> ControlViewModel::escenas() const {
    std::vector<std::string> out;
    for (const FotoMotor::Escena& e : p_.ventanilla.foto().escenas) out.push_back(e.nombre);
    return out;
}

std::vector<std::string> ControlViewModel::fuentesAudio() const {
    std::vector<std::string> out;
    if (p_.tiras == nullptr) return out;
    for (int i = 0; i < p_.tiras->rowCount(); ++i)
        out.push_back(p_.tiras->data(p_.tiras->index(i), MixerModel::NameRole).toString().toStdString());
    return out;
}

bool ControlViewModel::mudo(const std::string& fuente, const std::string& salida) const {
    if (p_.tiras == nullptr) return false;
    for (int i = 0; i < p_.tiras->rowCount(); ++i)
        if (p_.tiras->data(p_.tiras->index(i), MixerModel::NameRole).toString().toStdString() == fuente)
            return p_.tiras->nivelEn(i, salida).value("mudo", false);
    return false;
}

void ControlViewModel::alCambiarFoto(unsigned partes) {
    const FotoMotor& f = p_.ventanilla.foto();
    if ((partes & FotoMotor::Escenas) != 0) {
        QStringList n;
        for (const FotoMotor::Escena& e : f.escenas) n << qs(e.nombre);
        for (const auto& [fuente, tipo] : f.fuentes)
            if (!n.contains(qs(fuente))) n << qs(fuente);
        if (n != nombresParaDsk_) {
            nombresParaDsk_ = n;
            emit nombresParaDskChanged();
        }
    }
    // El tema web: lo cambie quien lo cambie (esta vista, Configuración, el MCP), se le
    // pasa al gateway y la vista se entera.
    if ((partes & (FotoMotor::Superficies | FotoMotor::Ajustes)) != 0 && f.temaWeb != temaWeb_) {
        temaWeb_ = f.temaWeb;
        if (temaWeb_ != temaWebMandado_) mandarTemaWeb();
        emit temaWebChanged();
    }
}

void ControlViewModel::armarBotonera() {
    // La botonera: el modelo, los permisos y la ejecución viven acá; el gateway
    // es solo el borde de red (TLS, tokens, WebSocket de las tablets).
    control::Botonera::Ganchos g;
    g.escenas = [this]() { return escenas(); };
    g.salidas = p_.salidas;
    g.fuentesAudio = [this]() { return fuentesAudio(); };
    g.pdfs = [this]() {
        std::vector<std::pair<std::string, std::string>> out;
        if (p_.pdfEscena)
            for (const QVariant& v : p_.pdfEscena->documentos())
                out.emplace_back(v.toMap().value("id").toString().toStdString(), v.toMap().value("titulo").toString().toStdString());
        return out;
    };
    g.bancas = [this]() {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& i : p_.asamblea.cuerpo().cuerpo().integrantes())
            if (!i.value("banca", std::string()).empty())
                out.emplace_back(i.value("banca", std::string()), i.value("nombre", std::string()));
        return out;
    };
    g.recinto = [this]() { return p_.asamblea.cuerpo().cuerpo().recinto(); };
    // El estante de la línea de tiempo (plantilla «Línea de tiempo»: un botón por ítem).
    g.estanteLinea = [this]() { return acciones_ && acciones_->ganchos().estante ? acciones_->ganchos().estante() : nlohmann::json::array(); };
    // La cara de un botón: la foto del integrante (subida o capturada con la
    // cámara de su banca) o la foto de una posición PTZ, en 96 px.
    g.imagen = [this](const std::string& ref) -> std::string {
        QString ruta;
        const QString r = QString::fromStdString(ref);
        if (r.startsWith("medio:")) {
            ruta = QString::fromStdString(carasBotonera_->rutaPoster(ref.substr(6)));
        } else if (r.startsWith("cuerpo:")) {
            ruta = p_.asamblea.cuerpo().rutaFoto(r.mid(7));
        } else if (r.startsWith("ptz:") && p_.camaras) {
            const QString resto = r.mid(4);
            const int barra = resto.lastIndexOf('/');
            if (barra > 0)
                for (const QVariant& v : p_.camaras->posiciones(resto.left(barra))) {
                    const QVariantMap m = v.toMap();
                    if (m.value("id").toString() == resto.mid(barra + 1) ||
                        m.value("nombre").toString().compare(resto.mid(barra + 1), Qt::CaseInsensitive) == 0)
                        ruta = m.value("miniatura_ruta").toString();
                }
        }
        if (ruta.isEmpty()) return {};
        const QFileInfo fi(ruta);
        const QString clave = ruta + "|" + QString::number(fi.lastModified().toMSecsSinceEpoch());
        auto it = imagenesBotonera_.find(clave);
        if (it != imagenesBotonera_.end()) return it->second;
        QImage img(ruta);
        if (img.isNull()) return {};
        const int lado = std::min(img.width(), img.height());
        img = img.copy((img.width() - lado) / 2, (img.height() - lado) / 2, lado, lado)
                  .scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray jpg;
        QBuffer buf(&jpg);
        buf.open(QIODevice::WriteOnly);
        img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPG", 80);
        const std::string url = "data:image/jpeg;base64," + jpg.toBase64().toStdString();
        if (imagenesBotonera_.size() > 400) imagenesBotonera_.clear();
        imagenesBotonera_[clave] = url;
        return url;
    };
    g.cara = [this](const std::string& ref) -> nlohmann::json {
        if (ref.rfind("medio:", 0) != 0) return nullptr;
        return carasBotonera_->cara(ref.substr(6));
    };
    g.medioDeCara = [this](const std::string& id) -> nlohmann::json {
        const QVariantMap i = carasBotonera_->info(qs(id));
        return {{"medio", i.value("medio").toString().toStdString()}, {"video", i.value("video").toBool()}};
    };
    g.presidentes = [this]() {
        std::set<std::string> out;
        for (const auto& u : p_.asamblea.dcn().estado().value("unidades", nlohmann::json::array()))
            if (u.value("tipo", std::string()) == "presidente" && !u.value("banca", std::string()).empty())
                out.insert(u.value("banca", std::string()));
        return out;
    };
    // Un cuadro de una entrada (la foto o el video corto de una cara): lo saca el dueño del motor.
    carasBotonera_ = std::make_unique<control::CarasBotonera>(
        [&rt = p_.rt, &v = p_.ventanilla](const std::string& entrada, int ancho, control::CarasBotonera::Captura listo) {
            v.pedir("cara_capturar", [&rt, entrada, ancho, listo = std::move(listo)]() mutable {
                rt.engine().capturarAparte(entrada, ancho, std::move(listo));
            });
        },
        p_.rt.dataRoot());
    botonera_ = std::make_unique<control::Botonera>(p_.rt.dataRoot(), p_.bus, std::move(g));
}

QObject* ControlViewModel::botoneraQml() const { return deCpp(botonera_.get()); }
QObject* ControlViewModel::carasBotoneraQml() const { return deCpp(carasBotonera_.get()); }
QObject* ControlViewModel::controlWebQml() const { return deCpp(controlWeb_.get()); }
QObject* ControlViewModel::satelliteQml() const { return deCpp(satellite_.get()); }
QObject* ControlViewModel::dskQml() const { return deCpp(dsk_.get()); }
QObject* ControlViewModel::accionesQml() const { return deCpp(acciones_.get()); }

QString ControlViewModel::temaWeb() const { return qs(temaWeb_); }

QVariantList ControlViewModel::temasWeb() const {
    QVariantList out;
    for (const auto& t : control::temasWeb())
        out << QVariantMap{{"id", t.id},       {"nombre", QCoreApplication::translate("control-web", t.nombre)},
                           {"descripcion", QCoreApplication::translate("control-web", t.descripcion)},
                           {"fondo", t.fondo}, {"superficie", t.superficie}, {"texto", t.texto},
                           {"acento", t.acento}};
    return out;
}

std::string ControlViewModel::guardarTemaWeb(AppRuntime& rt, const std::string& id) {
    if (!control::temaWebValido(id)) return tr("No hay un tema «%1».").arg(qs(id)).toStdString();
    if (id == rt.config().temaWeb) return {};
    rt.config().temaWeb = id;
    rt.save();
    rt.ajustesAplicados(); // Configuración se entera; la foto lo trae (la parte Superficies)
    return {};
}

void ControlViewModel::setTemaWeb(const QString& id) {
    const std::string t = id.toStdString();
    if (!control::temaWebValido(t) || t == temaWeb_) return;
    p_.ventanilla.pedir(this, "tema_web", [&rt = p_.rt, t]() { return guardarTemaWeb(rt, t); },
                        [this](const std::string& e) {
                            if (!e.empty()) emit notify(qs(e), "error");
                        });
}

void ControlViewModel::mandarTemaWeb() {
    const std::string t = temaWeb_;
    temaWebMandado_ = t;
    QPointer<ControlViewModel> yo(this);
    sala::pedir(p_.sala.client(), "POST", "/api/v1/tema", {{"tema", t}}, [yo](const std::string& err, const nlohmann::json&) {
        if (yo && !err.empty()) yo->temaWebMandado_.clear(); // se reintenta cuando la sala vuelva
    });
}

void ControlViewModel::emparejarControlWebUi() {
    QPointer<ControlViewModel> yo(this);
    controlWebGw_->emparejar(nullptr, "", [yo](const nlohmann::json& r) {
        if (!yo) return;
        if (!r.contains("url")) {
            emit yo->notify(QString::fromStdString(r.value("error", tr("No se pudo emparejar.").toStdString())), "error");
            return;
        }
        const std::string aviso = r.value("aviso", std::string());
        emit yo->mostrarQr(QString::fromStdString(r.value("url", std::string())), tr("Control web (/control/)"),
                           tr("Abrilo en el navegador del aparato (de la red local; el código vale unos "
                              "minutos y sirve una vez). Después aprobalo acá, en Servicios → Control web.") +
                               (aviso.empty() ? QString() : "\n" + QString::fromStdString(aviso)),
                           "verde");
    });
}

void ControlViewModel::emparejarTabletUi(const QString& perfil, const QString& nombre, int venceDias) {
    QPointer<ControlViewModel> yo(this);
    botoneraGw_->emparejar(perfil.toStdString(), nombre.toStdString(), venceDias, [yo](const nlohmann::json& r) {
        if (!yo) return;
        const QJsonDocument d = QJsonDocument::fromJson(
            QByteArray::fromStdString(r.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
        emit yo->emparejamientoListo(d.object().toVariantMap());
    });
}

void ControlViewModel::aprobarTabletUi(const QString& solicitud, bool aprobar, const QString& perfil) {
    const std::string e = botoneraGw_->resolver(solicitud.toStdString(), aprobar, perfil.toStdString());
    if (!e.empty()) {
        emit notify(qs(e), "error");
        return;
    }
    emit notify(aprobar ? tr("Tablet emparejada.") : tr("Emparejamiento rechazado."), aprobar ? "ok" : "info");
}

QString ControlViewModel::urlApiRemota() const {
    // La dirección la decide la sala (sala/Direcciones): la puerta HTTP en la red local.
    return qs(p_.direcciones.origen("companion"));
}

QVariantMap ControlViewModel::apiRemotaUi() const {
    QVariantList accesos;
    for (const auto& a : botonera_->accesosHttp())
        accesos.append(QVariantMap{{"id", qs(a.value("id", std::string()))},
                                   {"nombre", qs(a.value("nombre", std::string()))},
                                   {"perfil", qs(a.value("perfil", std::string()))}});
    QVariantMap out{{"activo", botonera_->apiRemotaActiva()}, {"accesos", accesos}, {"url", urlApiRemota()}};
    if (urlApiRemota().isEmpty()) out["aviso"] = qs(p_.direcciones.motivo("companion"));
    return out;
}

void ControlViewModel::setApiRemotaUi(bool activo) { botonera_->setApiRemotaActiva(activo); }

QVariantMap ControlViewModel::crearAccesoHttpUi(const QString& perfil, const QString& nombre) {
    const nlohmann::json r = botonera_->crearAccesoHttp(perfil.toStdString(), nombre.toStdString());
    if (r.contains("error")) return {{"error", qs(r.value("error", std::string()))}};
    return {{"dispositivo", qs(r.value("dispositivo", std::string()))},
            {"token", qs(r.value("token", std::string()))},
            {"url_ejemplo", urlApiRemota() + qs(r.value("ejemplo", std::string()))}};
}

void ControlViewModel::alimentarEnlaces(EnlacesViewModel::Fuentes& f) {
    f.apiRemota = [this]() { return apiRemotaUi(); };
    f.activarApiRemota = [this](bool on) { setApiRemotaUi(on); };
    f.satellite = [this]() -> QVariantMap {
        if (!satellite_) return {};
        return {{"activo", satellite_->activo()}, {"puerto", satellite_->puerto()}, {"error", satellite_->error()}};
    };
    f.activarSatellite = [this](bool on) -> QString {
        return satellite_ ? satellite_->configurar(on, satellite_->puerto()) : tr("No disponible.");
    };
    f.controlWeb = [this]() -> QVariantMap {
        return {{"conectados", controlWeb_->conectados()},
                {"aparatos", controlWeb_->aparatosUi().size()},
                {"tunel", controlWeb_->tunel()}};
    };
    f.emparejarControlWeb = [this]() { emparejarControlWebUi(); };
    // Los pedidos de aprobación por aparato: se aprueban en la misma fila de Servicios.
    // Lo hace la persona que está en la consola (el asistente va por sus herramientas).
    {
        EnlacesViewModel::Aprobaciones a;
        a.pendientes = [this]() {
            QVariantList out;
            for (const QVariant& v : botonera_->solicitudesUi()) {
                QVariantMap m = v.toMap();
                m["perfil"] = m.value("perfilId");
                m["detalle"] = tr("pide emparejarse");
                out << m;
            }
            return out;
        };
        a.resolver = [this](const QString& sol, bool aprobar, const QString& perfil) -> QString {
            const std::string e = botoneraGw_->resolver(sol.toStdString(), aprobar, perfil.toStdString());
            if (e.empty()) emit notify(aprobar ? tr("Tablet emparejada.") : tr("Emparejamiento rechazado."), aprobar ? "ok" : "info");
            return qs(e);
        };
        a.perfilModo = "lista";
        a.perfiles = [this]() {
            QVariantList out;
            for (const QVariant& v : botonera_->perfilesUi()) {
                const QVariantMap pf = v.toMap();
                out << QVariantMap{{"id", pf.value("id")}, {"nombre", pf.value("nombre")}};
            }
            return out;
        };
        f.aprobaciones["botonera"] = std::move(a);
    }
    {
        EnlacesViewModel::Aprobaciones a;
        a.pendientes = [this]() {
            QVariantList out;
            for (const QVariant& v : controlWeb_->solicitudesUi()) {
                QVariantMap m = v.toMap();
                m["detalle"] = tr("pide entrar al control web");
                out << m;
            }
            return out;
        };
        a.resolver = [this](const QString& sol, bool aprobar, const QString& perfil) -> QString {
            const QString e = controlWeb_->aprobarUi(sol, aprobar, perfil);
            if (e.isEmpty()) emit notify(aprobar ? tr("Aparato emparejado.") : tr("Pedido rechazado."), aprobar ? "ok" : "info");
            return e;
        };
        a.perfilModo = "texto";
        a.perfilAyuda = tr("todo, o: escenas, audio");
        f.aprobaciones["control"] = std::move(a);
    }
    {
        EnlacesViewModel::Aprobaciones a;
        a.pendientes = [this]() {
            QVariantList out;
            for (const QVariant& v : satellite_->solicitudesUi()) {
                QVariantMap m = v.toMap();
                m["detalle"] = tr("quiere conectar un Satellite");
                out << m;
            }
            return out;
        };
        a.resolver = [this](const QString& ip, bool aprobar, const QString&) -> QString {
            if (!satellite_->pendientes().contains(ip)) return tr("Ese pedido ya no está.");
            if (aprobar)
                satellite_->permitir(ip, true);
            else
                satellite_->rechazar(ip);
            return {};
        };
        a.textoAprobar = tr("Permitir");
        a.unoEspera = tr("Una IP pide conectar: permitila acá abajo");
        a.variosEsperan = tr("%1 IP piden conectar: permitilas acá abajo");
        f.aprobaciones["satellite"] = std::move(a);
    }
}

void ControlViewModel::avisarA(EnlacesViewModel& e) {
    const auto avisar = [ep = &e]() { ep->avisarCambio(); };
    connect(satellite_.get(), &control::SatelliteServidor::cambio, &e, avisar);
    connect(controlWeb_.get(), &control::ControlWeb::aparatosCambio, &e, avisar);
    connect(controlWeb_.get(), &control::ControlWeb::solicitudesCambio, &e, avisar);
    connect(controlWeb_.get(), &control::ControlWeb::conexionesCambio, &e, avisar);
    connect(botonera_.get(), &control::Botonera::solicitudesCambio, &e, avisar);
    connect(botonera_.get(), &control::Botonera::cambio, &e, avisar); // los nombres de los perfiles
}

} // namespace ba::app
