// ============================================================================
// src/app/ui/VivosViewModel.cpp — ver VivosViewModel.hpp
// ============================================================================

#include "ui/VivosViewModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlEngine>

#include <utility>

#include "arranque/RuntimeAire.hpp"
#include "emision/VentanillaMotor.hpp"
#include "i18n/I18nTexto.hpp"
#include "net/Credentials.hpp"
#include "platform/Paths.hpp"
#include "ui/AireDeCuentas.hpp"
#include "ui/CapturaError.hpp"
#include "ui/EmisionViewModel.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/ListaObjetos.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"
#include "vivos/PlataformasVivo.hpp"

namespace ba::app {

namespace {

using json = nlohmann::json;

QString qs(const std::string& s) { return QString::fromStdString(s); }

QVariant aVariant(const json& j) {
    const QByteArray b = QByteArray::fromStdString(json{{"x", j}}.dump(-1, ' ', false, json::error_handler_t::replace));
    return QJsonDocument::fromJson(b).object().value("x").toVariant();
}

QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}


/// La salud que dijo la plataforma, para una persona.
std::string textoSalud(const json& s) {
    if (!s.is_object() || s.empty()) return {};
    std::string out;
    const auto sumar = [&out](const std::string& t) {
        if (!t.empty()) out += (out.empty() ? "" : " · ") + t;
    };
    const std::string es = s.value("estado_stream", std::string());
    if (es == "active") sumar(ba::i18n::traducir("vivos", "recibe la señal"));
    else if (es == "ready" || es == "inactive") sumar(ba::i18n::traducir("vivos", "todavía sin señal"));
    else if (es == "error") sumar(ba::i18n::traducir("vivos", "error en la ingesta"));
    const std::string sa = s.value("salud", std::string());
    if (sa == "good") sumar(ba::i18n::traducir("vivos", "salud excelente"));
    else if (sa == "ok") sumar(ba::i18n::traducir("vivos", "salud buena"));
    else if (sa == "bad") sumar(ba::i18n::traducir("vivos", "salud mala: revisá el bitrate y la red"));
    else if (sa == "noData") sumar(ba::i18n::traducir("vivos", "sin datos de salud"));
    const std::string ev = s.value("estado_vivo", std::string());
    if (ev == "live") sumar(ba::i18n::traducir("vivos", "al aire en la plataforma"));
    else if (ev == "testing") sumar(ba::i18n::traducir("vivos", "en prueba"));
    else if (ev == "offline") sumar(ba::i18n::traducir("vivos", "el canal no está en vivo"));
    if (s.contains("espectadores") && s["espectadores"].is_number())
        sumar(ba::i18n::llenar(ba::i18n::traducir("vivos", "%1 mirando"), {std::to_string(s["espectadores"].get<long long>())}));
    return out;
}

} // namespace

VivosViewModel::VivosViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    const QString dir = QString::fromStdString(ba::platform::credentialsDir().string());
    // La lista de cuentas: del aparato, cifrada junto a los tokens.
    registro_ = std::make_unique<vivos::RegistroCuentas>(vivos::RegistroCuentas::Almacen{
        [dir]() -> json {
            QByteArray raw;
            if (!cred::read(dir, vivos::kCredencialLista, raw)) return json();
            return json::parse(raw.toStdString(), nullptr, false);
        },
        [dir](const json& j) {
            return cred::write(dir, vivos::kCredencialLista,
                               QByteArray::fromStdString(j.dump(1, ' ', false, json::error_handler_t::replace)));
        }});
    registro_->cargar();
    youtube_ = std::make_unique<vivos::YouTubeVivo>(p_.youtube.cliente(), dir);
    twitch_ = std::make_unique<vivos::TwitchVivo>(p_.twitch.cliente(), dir);
    facebook_ = std::make_unique<vivos::FacebookVivo>(p_.facebook.cliente(), dir);

    servicio_ = std::make_unique<vivos::ServicioVivos>(vivos::ServicioVivos::Partes{
        *registro_,
        [this](const std::string& id) { return plataforma(id); },
        [this]() { return p_.ventanilla.foto().destinos; },
        [this](const std::string& id) { return huellaDe(id); },
        [this](const json& d, std::function<void(const std::string&, const std::string&)> listo) {
            // Por la ventanilla, como los demás destinos (Config/destinos.json es del evento;
            // la clave va cifrada como «stream-<id>»).
            p_.ventanilla.pedir(
                this, "vivo_destino_guardar",
                [&rt = p_.rt, d]() {
                    std::string id;
                    std::string e = rt.guardarDestino(d, &id);
                    return std::make_pair(e, id);
                },
                [this, listo](std::pair<std::string, std::string> r) {
                    huellas_.clear();
                    listo(r.first, r.second);
                });
        },
        [this](const std::string& salida) -> std::string {
            const std::string id = salida.empty() ? std::string("principal") : salida;
            for (const auto& s : p_.ventanilla.foto().salidas)
                if (s.id == id) return s.nombre.empty() ? s.id : s.nombre;
            return {};
        }});

    const QStringList rolesCuenta{"plataforma", "cuenta", "nombre", "canal", "estado", "estadoTexto", "principal", "conectada", "conectando"};
    cuentas_ = std::make_unique<ListaObjetos>(QStringLiteral("uid"), rolesCuenta);
    for (const std::string& pl : vivos::plataformas())
        cuentasPor_[pl] = std::make_unique<ListaObjetos>(QStringLiteral("uid"), rolesCuenta);
    vivos_ = std::make_unique<ListaObjetos>(QStringLiteral("uid"), QStringList{"titulo", "estado", "privacidad", "inicio", "url"});
    destinos_ = std::make_unique<ListaObjetos>(
        QStringLiteral("uid"),
        QStringList{"nombre", "salida", "plataforma", "plataformaNombre", "cuenta", "cuentaNombre", "vivo", "vivoTitulo", "estado",
                    "estadoTexto", "kbps", "activo", "reconectando", "respaldo", "salud", "saludTexto", "choque"});

    for (vivos::PlataformaVivo* pl : {youtube_.get(), twitch_.get(), facebook_.get()})
        connect(pl, &vivos::PlataformaVivo::cambio, this, [this]() {
            rehacerCuentas();
            rehacerDestinos();
        });
    // Los destinos: la foto del motor (la lista y lo que sale) y el bitrate de cada uno.
    p_.ventanilla.escuchar(this, [this](unsigned partes) {
        if ((partes & FotoMotor::Emision) != 0 || (partes & FotoMotor::Salidas) != 0) rehacerDestinos();
    });
    if (EmisionViewModel* e = p_.emision ? p_.emision() : nullptr)
        connect(e, &EmisionViewModel::saludChanged, this, &VivosViewModel::rehacerDestinos);
    sondeo_.setInterval(30000);
    connect(&sondeo_, &QTimer::timeout, this, [this]() {
        servicio_->sondearSalud(p_.ventanilla.foto().destinos, [this]() { rehacerDestinos(); });
    });
    rehacerCuentas();
    rehacerDestinos();
}

VivosViewModel::~VivosViewModel() = default;

QObject* VivosViewModel::cuentasQml() const { return deCpp(cuentas_.get()); }
QObject* VivosViewModel::vivosQml() const { return deCpp(vivos_.get()); }
QObject* VivosViewModel::destinosQml() const { return deCpp(destinos_.get()); }

QVariantMap VivosViewModel::cuentasPor() const {
    QVariantMap m;
    for (const auto& [pl, l] : cuentasPor_) m.insert(qs(pl), QVariant::fromValue(deCpp(l.get())));
    return m;
}

vivos::Plataforma* VivosViewModel::plataforma(const std::string& id) const {
    if (id == "youtube") return youtube_.get();
    if (id == "twitch") return twitch_.get();
    if (id == "facebook") return facebook_.get();
    return nullptr;
}

PlataformaViewModel* VivosViewModel::paginaDe(const std::string& id) const {
    if (id == "youtube") return &p_.youtube;
    if (id == "twitch") return &p_.twitch;
    if (id == "facebook") return &p_.facebook;
    return nullptr;
}

void VivosViewModel::avisarError(const std::string& e) {
    if (!e.empty() && !CapturaError::tomar(qs(e))) emit notify(qs(e), "error");
}

std::string VivosViewModel::huellaDe(const std::string& destino) const {
    const auto it = huellas_.find(destino);
    if (it != huellas_.end()) return it->second;
    std::string clave;
    ba::security::CredentialStore::bundle().read("stream-" + destino, clave);
    return huellas_[destino] = vivos::huella(clave);
}

json VivosViewModel::kbps() const {
    json k = json::object();
    if (EmisionViewModel* e = p_.emision ? p_.emision() : nullptr)
        for (const json& s : e->saludActual()) k[s.value("id", std::string())] = s.value("kbps", 0);
    return k;
}

// --- Las cuentas ---------------------------------------------------------------------------

void VivosViewModel::rehacerCuentas() {
    // Lo que dice cada plataforma de sus cuentas conectadas queda anotado (el canal y que
    // estuvo conectada: si después deja de estarlo, se venció).
    for (const vivos::Cuenta& c : registro_->de()) {
        const vivos::Plataforma* pl = plataforma(c.plataforma);
        if (pl != nullptr && pl->conectada(c.id)) registro_->anotarConexion(c.plataforma, c.id, pl->canal(c.id));
    }
    const json todas = servicio_->cuentas();
    QVariantList filas;
    std::map<std::string, QVariantList> por;
    for (const json& c : todas) {
        const std::string pl = c.value("plataforma", std::string());
        const std::string e = c.value("estado", std::string());
        const QVariantMap f{{"uid", qs(pl + "/" + c.value("id", std::string()))},
                            {"plataforma", qs(pl)},
                            {"cuenta", qs(c.value("id", std::string()))},
                            {"nombre", qs(c.value("nombre", std::string()))},
                            {"canal", qs(c.value("canal", std::string()))},
                            {"estado", qs(e)},
                            {"estadoTexto", qs(c.value("estado_texto", std::string()))},
                            {"principal", c.value("principal", false)},
                            {"conectada", e == "conectada" || e == "sin_permiso"},
                            {"conectando", e == "conectando"}};
        filas.append(f);
        por[pl].append(f);
    }
    cuentas_->poner(filas);
    for (auto& [pl, l] : cuentasPor_) l->poner(por[pl]);
}

std::string VivosViewModel::agregar(const std::string& plataforma, const std::string& nombre, std::string* id) {
    const std::string e = registro_->agregar(plataforma, nombre, id);
    if (e.empty()) rehacerCuentas();
    return e;
}

std::string VivosViewModel::renombrar(const std::string& plataforma, const std::string& cuenta, const std::string& nombre) {
    const std::string e = registro_->renombrar(plataforma, cuenta, nombre);
    if (e.empty()) {
        rehacerCuentas();
        rehacerDestinos();
    }
    return e;
}

std::string VivosViewModel::quitar(const std::string& plataforma, const std::string& cuenta) {
    vivos::Plataforma* pl = this->plataforma(plataforma);
    if (pl == nullptr) return ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una plataforma «%1» (youtube, twitch o facebook)."), {plataforma});
    // Los destinos que la usan (en este evento) quedan sin cuenta: se avisa antes.
    for (const json& d : p_.ventanilla.foto().destinos)
        if (d.value("plataforma", std::string()) == plataforma && d.value("cuenta", std::string()) == cuenta)
            return ba::i18n::llenar(ba::i18n::traducir("vivos", "La usa el destino «%1»: quitalo antes (o asignale otra cuenta)."),
                                    {d.value("nombre", d.value("id", std::string()))});
    const std::string e = registro_->quitar(plataforma, cuenta);
    if (!e.empty()) return e;
    pl->desconectar(cuenta); // revoca y borra su token
    pl->olvidar(cuenta);
    rehacerCuentas();
    return {};
}

bool VivosViewModel::agregarCuenta(const QString& plataforma, const QString& nombre) {
    const std::string e = agregar(plataforma.toStdString(), nombre.trimmed().toStdString(), nullptr);
    avisarError(e);
    return e.empty();
}

bool VivosViewModel::renombrarCuenta(const QString& plataforma, const QString& cuenta, const QString& nombre) {
    const std::string e = renombrar(plataforma.toStdString(), cuenta.toStdString(), nombre.trimmed().toStdString());
    avisarError(e);
    return e.empty();
}

bool VivosViewModel::quitarCuenta(const QString& plataforma, const QString& cuenta) {
    const std::string e = quitar(plataforma.toStdString(), cuenta.toStdString());
    avisarError(e);
    return e.empty();
}

void VivosViewModel::conectar(const std::string& plataforma, const std::string& cuenta, vivos::Listo listo) {
    vivos::Plataforma* pl = this->plataforma(plataforma);
    const vivos::Cuenta* c = registro_->buscar(plataforma, cuenta);
    if (pl == nullptr || c == nullptr)
        return listo(vivos::Resultado::mal(ba::i18n::llenar(ba::i18n::traducir("vivos", "No hay una cuenta «%1» de %2 (cuentas_listar las lista)."),
                                                             {cuenta.empty() ? std::string(vivos::kPrincipal) : cuenta,
                                                              vivos::nombrePlataforma(plataforma)})));
    if (!pl->configurada())
        return listo(vivos::Resultado::mal(
            ba::i18n::llenar(ba::i18n::traducir("vivos", "Falta la app de %1: seguí account_setup_guide."), {vivos::nombrePlataforma(plataforma)})));
    const std::string id = c->id;
    QPointer<VivosViewModel> self(this);
    const auto alVolver = [self, plataforma, id, listo](const vivos::Resultado& r) {
        if (self && r.ok) {
            self->registro_->anotarConexion(plataforma, id, r.datos.value("canal", std::string()));
            self->rehacerCuentas();
        }
        listo(r);
    };
    // La principal, por su página (que se pone al día igual que si se conectara desde ahí).
    if (id == vivos::kPrincipal) {
        if (PlataformaViewModel* pagina = paginaDe(plataforma)) {
            pagina->login([alVolver, pagina](bool ok, const QJsonObject&, const QString& e) {
                if (!ok) return alVolver(vivos::Resultado::mal(e.toStdString()));
                alVolver(vivos::Resultado::bien({{"canal", pagina->cuentaNombre().toStdString()}}));
            });
            rehacerCuentas();
            return;
        }
    }
    pl->conectar(id, alVolver);
    rehacerCuentas(); // «conectando»
}

void VivosViewModel::conectarCuenta(const QString& plataforma, const QString& cuenta) {
    QPointer<VivosViewModel> self(this);
    conectar(plataforma.toStdString(), cuenta.toStdString(), [self](const vivos::Resultado& r) {
        if (!self) return;
        if (!r.ok) return self->avisarError(r.error);
        emit self->notify(qs(ba::i18n::llenar(ba::i18n::traducir("vivos", "Cuenta conectada: %1"), {r.datos.value("canal", std::string())})), "ok");
    });
}

void VivosViewModel::cancelarConexion(const QString& plataforma, const QString& cuenta) {
    if (vivos::Plataforma* pl = this->plataforma(plataforma.toStdString())) pl->cancelarConexion(cuenta.toStdString());
    rehacerCuentas();
}

void VivosViewModel::desconectarCuenta(const QString& plataforma, const QString& cuenta) {
    const std::string p = plataforma.toStdString();
    const std::string c = cuenta.isEmpty() ? std::string(vivos::kPrincipal) : cuenta.toStdString();
    if (c == vivos::kPrincipal) {
        if (PlataformaViewModel* pagina = paginaDe(p)) pagina->logout(); // la página se pone al día
    } else if (vivos::Plataforma* pl = this->plataforma(p)) {
        pl->desconectar(c);
    }
    registro_->anotarDesconexion(p, c);
    rehacerCuentas();
}

// --- Los vivos de una cuenta ---------------------------------------------------------------

void VivosViewModel::listarVivos(const std::string& plataforma, const std::string& cuenta, vivos::Listo listo) {
    if (std::string e = servicio_->cuentaLista(plataforma, cuenta); !e.empty()) return listo(vivos::Resultado::mal(e));
    this->plataforma(plataforma)->vivos(cuenta.empty() ? std::string(vivos::kPrincipal) : cuenta, std::move(listo));
}

void VivosViewModel::cargarVivos(const QString& plataforma, const QString& cuenta) {
    const std::string p = plataforma.toStdString();
    const vivos::Plataforma* pl = this->plataforma(p);
    vivosDe_ = QVariantMap{{"plataforma", plataforma},
                           {"cuenta", cuenta},
                           {"cargando", true},
                           {"error", QString()},
                           {"aparte", pl == nullptr || pl->vivosAparte()}};
    vivos_->poner({});
    emit vivosDeCambio();
    QPointer<VivosViewModel> self(this);
    listarVivos(p, cuenta.toStdString(), [self, plataforma, cuenta](const vivos::Resultado& r) {
        // Llegó tarde (ya se mira otra cuenta): no pisa.
        if (!self || self->vivosDe_.value("plataforma").toString() != plataforma || self->vivosDe_.value("cuenta").toString() != cuenta)
            return;
        self->vivosDe_.insert("cargando", false);
        self->vivosDe_.insert("error", qs(r.error));
        QVariantList filas;
        if (r.ok)
            for (const json& v : r.datos.value("items", json::array())) {
                QVariantMap f = aVariant(v).toMap();
                f.insert("uid", qs(v.value("id", std::string())));
                filas.append(f);
            }
        self->vivos_->poner(filas);
        emit self->vivosDeCambio();
    });
}

void VivosViewModel::crearVivo(const std::string& plataforma, const std::string& cuenta, const json& datos, vivos::Listo listo) {
    if (std::string e = servicio_->cuentaLista(plataforma, cuenta); !e.empty()) return listo(vivos::Resultado::mal(e));
    this->plataforma(plataforma)->crearVivo(cuenta.empty() ? std::string(vivos::kPrincipal) : cuenta, datos, std::move(listo));
}

void VivosViewModel::crearVivoUi(const QString& salida, const QString& plataforma, const QString& cuenta, const QString& titulo,
                                 const QString& privacidad, const QString& inicio) {
    json datos{{"titulo", titulo.trimmed().toStdString()}, {"privacidad", privacidad.isEmpty() ? std::string("private") : privacidad.toStdString()}};
    if (!inicio.trimmed().isEmpty()) datos["inicio"] = inicio.trimmed().toStdString();
    QPointer<VivosViewModel> self(this);
    crearVivo(plataforma.toStdString(), cuenta.toStdString(), datos, [self, salida, plataforma, cuenta](const vivos::Resultado& r) {
        if (!self) return;
        if (!r.ok) {
            self->avisarError(r.error);
            emit self->asignado(qs(r.error));
            return;
        }
        const json v = r.datos.value("vivo", json::object());
        if (self->vivosDe_.value("plataforma").toString() == plataforma && self->vivosDe_.value("cuenta").toString() == cuenta)
            self->cargarVivos(plataforma, cuenta);
        if (salida.isEmpty()) {
            emit self->notify(qs(ba::i18n::llenar(ba::i18n::traducir("vivos", "Vivo creado: %1"), {v.value("titulo", std::string())})), "ok");
            emit self->asignado({});
            return;
        }
        self->asignarVivoUi(salida, plataforma, cuenta, qs(v.value("id", std::string())), qs(v.value("titulo", std::string())));
    });
}

// --- Asignar -------------------------------------------------------------------------------

void VivosViewModel::asignar(vivos::Asignacion a, AlAsignar listo) {
    if (a.salida.empty()) a.salida = "principal";
    // El vivo al que ya transmite la página de YouTube con la transmisión principal: el
    // mismo choque que entre dos destinos.
    if (a.plataforma == "youtube" && (a.cuenta.empty() || a.cuenta == vivos::kPrincipal) && !a.vivo.empty() &&
        qs(a.vivo) == p_.youtube.liveBroadcastId() && cuentas::transmitiendo(p_.ventanilla))
        return listo(ba::i18n::traducir("vivos", "A ese vivo ya le transmite la página de YouTube (la transmisión principal): cortala antes o elegí "
                        "otro vivo."),
                     {});
    QPointer<VivosViewModel> self(this);
    servicio_->asignar(std::move(a), [self, listo](const std::string& e, const std::string& id) {
        if (self) self->rehacerDestinos();
        listo(e, id);
    });
}

void VivosViewModel::asignarVivoUi(const QString& salida, const QString& plataforma, const QString& cuenta, const QString& vivo,
                                   const QString& titulo) {
    vivos::Asignacion a;
    a.salida = salida.toStdString();
    a.plataforma = plataforma.toStdString();
    a.cuenta = cuenta.toStdString();
    a.vivo = vivo.toStdString();
    a.vivoTitulo = titulo.toStdString();
    QPointer<VivosViewModel> self(this);
    asignar(std::move(a), [self](const std::string& e, const std::string&) {
        if (!self) return;
        self->avisarError(e);
        if (e.empty()) emit self->notify(qs(ba::i18n::traducir("vivos", "Destino asignado: arrancalo desde la tarjeta de la salida.")), "ok");
        emit self->asignado(qs(e));
    });
}

// --- El estado de los destinos -------------------------------------------------------------

json VivosViewModel::estado(const std::string& salida) const {
    json e = servicio_->estado(p_.ventanilla.foto().destinos, kbps(), salida);
    for (json& f : e) f["salud_texto"] = textoSalud(f.value("salud", json::object()));
    return e;
}

void VivosViewModel::rehacerDestinos() {
    const json& guardados = p_.ventanilla.foto().destinos;
    // Cambió la lista (o sus claves): las huellas se vuelven a calcular.
    json lista = json::array();
    for (const json& d : guardados) lista.push_back({{"id", d.value("id", std::string())}, {"url", d.value("url", std::string())}});
    if (lista != listaAntes_) {
        listaAntes_ = lista;
        huellas_.clear();
    }
    const json filas = estado();
    QVariantList todas;
    QVariantMap por;
    bool algunoSale = false;
    for (const json& f : filas) {
        const QVariantMap m{{"uid", qs(f.value("id", std::string()))},
                            {"nombre", qs(f.value("nombre", std::string()))},
                            {"salida", qs(f.value("salida", std::string()))},
                            {"plataforma", qs(f.value("plataforma", std::string()))},
                            {"plataformaNombre", qs(f.value("plataforma_nombre", std::string()))},
                            {"cuenta", qs(f.value("cuenta", std::string()))},
                            {"cuentaNombre", qs(f.value("cuenta_nombre", std::string()))},
                            {"vivo", qs(f.value("vivo", std::string()))},
                            {"vivoTitulo", qs(f.value("vivo_titulo", std::string()))},
                            {"estado", qs(f.value("estado", std::string()))},
                            {"estadoTexto", qs(f.value("estado_texto", std::string()))},
                            {"kbps", f.value("kbps", 0)},
                            {"activo", f.value("activo", false)},
                            {"reconectando", f.value("reconectando", false)},
                            {"respaldo", f.value("respaldo", false)},
                            {"salud", aVariant(f.value("salud", json::object()))},
                            {"saludTexto", qs(f.value("salud_texto", std::string()))},
                            {"choque", qs(f.value("choque", std::string()))}};
        todas.append(m);
        const QString s = m.value("salida").toString();
        QVariantList l = por.value(s).toList();
        l.append(m);
        por.insert(s, l);
        algunoSale = algunoSale || (f.value("activo", false) && vivos::esPlataforma(f.value("plataforma", std::string())));
    }
    destinos_->poner(todas);
    if (por != porSalida_) {
        porSalida_ = por;
        emit porSalidaCambio();
    }
    // La salud se pide solo mientras algo sale a una plataforma (cuota de las APIs).
    if (algunoSale && !sondeo_.isActive()) {
        sondeo_.start();
        QTimer::singleShot(3000, this, [this]() {
            servicio_->sondearSalud(p_.ventanilla.foto().destinos, [this]() { rehacerDestinos(); });
        });
    } else if (!algunoSale && sondeo_.isActive()) {
        sondeo_.stop();
        servicio_->sondearSalud(json::array(), {}); // olvida la de los que ya no salen
    }
}

} // namespace ba::app
