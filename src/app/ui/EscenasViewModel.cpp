// src/app/ui/EscenasViewModel.cpp — ver el .hpp

#include "ui/EscenasViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/AjustesEscenas.hpp"
#include "escenas/Conmutador.hpp"
#include "i18n/I18n.hpp"
#include "ui/ListModels.hpp"
#include "ui/SalidasViewModel.hpp"

#include <QMap>
#include <QTimer>

#include <algorithm>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// El conmutador de `salida` ("" = la por omisión; si se fue, la por omisión). En el dueño.
escenas::Conmutador& conmutadorDe(escenas::Conmutadores& cs, const std::string& salida) {
    escenas::Conmutador* c = salida.empty() ? nullptr : cs.de(salida);
    return c != nullptr ? *c : cs.porOmision();
}

/// La repetición en la escena «Repetición» (la crea si falta) y armada en `salida`. En el
/// dueño, con el turno: "" o el error.
std::string armarRepeticionEn(AppRuntime& rt, escenas::Conmutadores& cs, const std::string& archivo,
                              const std::string& salida) {
    static const std::string kEscena = "Repetición", kFuente = "Repetición (clip)";
    BroadcastEngine& e = rt.engine();
    const auto escenas = e.sceneNames();
    if (std::find(escenas.begin(), escenas.end(), kEscena) == escenas.end() && !rt.addScene(kEscena)) return rt.lastError();
    const std::string ajustes =
        nlohmann::json{{"local_file", archivo}, {"looping", false}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const auto fuentes = e.sceneSources(kEscena);
    if (std::find(fuentes.begin(), fuentes.end(), kFuente) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = kFuente;
        spec.settingsJson = ajustes;
        if (!rt.addSource(kEscena, spec)) return rt.lastError();
    } else if (!rt.updateSourceSettings(kFuente, ajustes)) {
        return rt.lastError();
    }
    rt.save();
    escenas::Conmutador* c = salida.empty() ? &cs.porOmision() : cs.de(salida);
    if (c == nullptr)
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no hay una salida «%1»"), {salida});
    return c->armar(kEscena);
}

} // namespace

EscenasViewModel::EscenasViewModel(Partes p, QObject* parent)
    : QObject(parent), p_(std::move(p)), ajustes_(std::make_unique<escenas::AjustesEscenas>(p_.rt.dataRoot())) {
    // Lo que se muestra sale de la foto del motor: se avisa cuando cambia. El AFV es de la
    // salida activa: cambia también cuando se elige otra.
    connect(&p_.ventanilla, &VentanillaMotor::cambio, this, &EscenasViewModel::alCambiar);
    connect(&p_.salidas, &SalidasViewModel::activaElegida, this, &EscenasViewModel::afvCambio);
    listaAntes_ = firmaLista();
    afvAntes_ = afvFuentes();
    transicionesAntes_ = transicionesEntrada();
}

EscenasViewModel::~EscenasViewModel() = default;

void EscenasViewModel::alCambiar(unsigned partes) {
    if ((partes & FotoMotor::Escenas) != 0) {
        // La lista (nombres, carpetas, protecciones) y lo que muestra cada escena.
        if (const QStringList firma = firmaLista(); firma != listaAntes_) {
            listaAntes_ = firma;
            emit listaCambio();
        }
        emit escenaCambio();
        if (const QVariantMap t = transicionesEntrada(); t != transicionesAntes_) {
            transicionesAntes_ = t;
            emit transicionesCambio();
        }
    }
    if ((partes & FotoMotor::Salidas) != 0)
        if (const QStringList a = afvFuentes(); a != afvAntes_) {
            afvAntes_ = a;
            emit afvCambio();
        }
}

QStringList EscenasViewModel::firmaLista() const {
    QStringList firma;
    for (const auto& e : p_.ventanilla.foto().escenas)
        firma << qs(e.nombre) + QChar(0x1f) + qs(e.carpeta) + (e.protegida ? QStringLiteral("\x1f1") : QString());
    return firma;
}

void EscenasViewModel::pedir(const char* que, std::function<std::string(AppRuntime&)> orden, const QString& nivelError,
                             std::function<void()> alVolver) {
    p_.ventanilla.pedir(
        this, que, [&rt = p_.rt, orden = std::move(orden)]() { return orden(rt); },
        [this, nivelError, alVolver = std::move(alVolver)](const std::string& e) {
            if (!e.empty()) return emit notify(qs(e), nivelError);
            if (alVolver) alVolver();
        });
}

void EscenasViewModel::pedirAlConmutador(const char* que, std::function<std::string(escenas::Conmutador&)> orden,
                                         const QString& nivelError) {
    // El de la activa, resuelto en el dueño por su id (la activa puede irse en el medio).
    pedir(
        que,
        [&cs = p_.salidas.conmutadores(), salida = p_.salidas.activaId(), orden = std::move(orden)](AppRuntime&) {
            return orden(conmutadorDe(cs, salida));
        },
        nivelError);
}

QString EscenasViewModel::escenaAlAire() const {
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(p_.salidas.activaId());
    return s != nullptr ? qs(s->alAire) : QString();
}

bool EscenasViewModel::corteDirectoEditor() const { return ajustes_->corteDirectoEditor(); }

void EscenasViewModel::setCorteDirectoEditor(bool si) {
    if (ajustes_->setCorteDirectoEditor(si)) emit ajustesCambio();
}

QVariantList EscenasViewModel::escenasPorCarpeta() const {
    // MISMO ORDEN QUE LA PESTAÑA ESCENAS: primero las sueltas, después las
    // carpetas. Agrupando en el orden crudo del modelo, las sueltas quedaban
    // EN EL MEDIO de dos carpetas y sin título propio, o sea que parecían
    // parte de la de arriba. Dos vistas de lo mismo con dos ordenes distintos
    // obligan a leer en vez de reconocer.
    //
    // Adentro de cada grupo se respeta el orden que puso el operador:
    // ordenarlas alfabéticamente sería desordenarlas para el que las acomodó.
    QVariantList sueltas;
    QVariantList carpetas;    // [{carpeta, escenas}]
    QMap<QString, int> donde; // carpeta -> posicion en `carpetas`
    for (const FotoMotor::Escena& e : p_.ventanilla.foto().escenas) {
        const QString carpeta = qs(e.carpeta);
        if (carpeta.isEmpty()) {
            sueltas.append(qs(e.nombre));
            continue;
        }
        if (!donde.contains(carpeta)) {
            donde[carpeta] = static_cast<int>(carpetas.size());
            carpetas.append(QVariantMap{{"carpeta", carpeta}, {"escenas", QVariantList{}}});
        }
        QVariantMap g = carpetas[donde[carpeta]].toMap();
        QVariantList l = g.value("escenas").toList();
        l.append(qs(e.nombre));
        g["escenas"] = l;
        carpetas[donde[carpeta]] = g;
    }
    QVariantList out;
    if (!sueltas.isEmpty()) out.append(QVariantMap{{"carpeta", QString()}, {"escenas", sueltas}});
    out.append(carpetas);
    return out;
}

void EscenasViewModel::addScene(const QString& name) {
    pedir("escena_crear", [n = name.trimmed().toStdString()](AppRuntime& rt) {
        if (!rt.addScene(n)) return rt.lastError();
        rt.save();
        return std::string();
    });
}

QString EscenasViewModel::copyScene(const QString& from, const QString& to) {
    // Sin nombre, se propone uno libre: copiar tiene que ser un clic, no un
    // dialogo. Se renombra despues, que es cuando uno ya sabe como llamarla.
    QString destino = to;
    if (destino.isEmpty()) {
        const auto existe = [this](const QString& n) { return p_.ventanilla.foto().escena(n.toStdString()) != nullptr; };
        destino = tr("%1 (copia)").arg(from);
        for (int i = 2; existe(destino); ++i) destino = tr("%1 (copia %2)").arg(from).arg(i);
    }
    // El aviso de la copia (con lo que no se pudo recrear) vuelve con la respuesta.
    p_.ventanilla.pedir(
        this, "escena_copiar",
        [&rt = p_.rt, f = from.toStdString(), d = destino.toStdString()]() -> std::pair<bool, std::string> {
            if (!rt.copyScene(f, d)) return {false, rt.lastError()};
            const std::string aviso = rt.lastError(); // con la copia hecha: alguna fuente no se recreó
            rt.save();
            return {true, aviso};
        },
        [this, destino](const std::pair<bool, std::string>& r) {
            if (!r.first) return emit notify(qs(r.second), "error");
            emit notify(r.second.empty() ? tr("Escena copiada: %1").arg(destino) : tr("Escena copiada, pero %1").arg(qs(r.second)),
                        r.second.empty() ? "ok" : "warn");
        });
    return destino;
}

void EscenasViewModel::setSceneProtected(const QString& name, bool protegida) {
    pedir(
        "escena_proteger",
        [n = name.toStdString(), protegida](AppRuntime& rt) {
            if (!rt.setSceneProtected(n, protegida)) return rt.lastError();
            rt.save();
            return std::string();
        },
        "error",
        [this, name, protegida]() {
            emit notify(protegida ? tr("«%1» protegida: no se puede borrar ni renombrar").arg(name)
                                  : tr("«%1» ya no está protegida").arg(name),
                        "ok");
        });
}

bool EscenasViewModel::sceneProtected(const QString& name) const {
    const FotoMotor::Escena* e = p_.ventanilla.foto().escena(name.toStdString());
    return e != nullptr && e->protegida;
}

QStringList EscenasViewModel::protegidas() const {
    QStringList out;
    for (const FotoMotor::Escena& e : p_.ventanilla.foto().escenas)
        if (e.protegida) out << qs(e.nombre);
    return out;
}

void EscenasViewModel::removeScene(const QString& name) {
    pedir(
        "escena_borrar",
        [n = name.toStdString()](AppRuntime& rt) {
            if (!rt.removeScene(n)) return rt.lastError();
            rt.save();
            return std::string();
        },
        "error",
        [this, name]() {
            // El editor estaba en la que se borró: sigue a la del aire.
            if (p_.fuentes.scene() == name) p_.fuentes.setScene(escenaAlAire());
        });
}

void EscenasViewModel::setSceneFolder(const QString& name, const QString& folder) {
    pedir("escena_carpeta", [n = name.toStdString(), f = folder.trimmed().toStdString()](AppRuntime& rt) {
        if (!rt.setSceneFolder(n, f)) return rt.lastError();
        rt.save();
        return std::string();
    });
}

QStringList EscenasViewModel::sceneFolders() const {
    QStringList out;
    for (const FotoMotor::Escena& e : p_.ventanilla.foto().escenas)
        if (!e.carpeta.empty() && !out.contains(qs(e.carpeta))) out << qs(e.carpeta);
    return out;
}

void EscenasViewModel::moveScene(const QString& name, const QString& before) {
    pedir("escena_mover", [n = name.toStdString(), b = before.toStdString()](AppRuntime& rt) {
        if (!rt.moveScene(n, b)) return rt.lastError();
        rt.save();
        return std::string();
    });
}

void EscenasViewModel::renameScene(const QString& from, const QString& to) {
    const QString nuevo = to.trimmed();
    pedir(
        "escena_renombrar",
        [f = from.toStdString(), t = nuevo.toStdString()](AppRuntime& rt) {
            if (!rt.renameScene(f, t)) return rt.lastError();
            rt.save();
            return std::string();
        },
        "error",
        [this, from, nuevo]() {
            if (p_.fuentes.scene() == from) p_.fuentes.setScene(nuevo);
        });
}

void EscenasViewModel::setScenePreview(const QString& name) {
    // 960 px de ancho alcanza para el lienzo del editor y no castiga la GPU.
    p_.ventanilla.pedir("escena_vista_editor", [&rt = p_.rt, n = name.toStdString()]() { rt.engine().enableScenePreview(n, 960); });
}

void EscenasViewModel::takeScene(const QString& name) {
    pedirAlConmutador("escena_al_aire", [n = name.toStdString()](escenas::Conmutador& c) { return c.poner(n); });
}

void EscenasViewModel::armarEscena(const QString& name) {
    pedirAlConmutador("escena_armar", [n = name.toStdString()](escenas::Conmutador& c) { return c.armar(n); }, "aviso");
}

void EscenasViewModel::take() { takeCon(QString(), -1); }

void EscenasViewModel::takeCon(const QString& tipo, int ms) {
    // Sin nada armado es un aviso; con algo armado que no salió, un error.
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(p_.salidas.activaId());
    const bool habiaArmada = s != nullptr && !s->armada.empty();
    pedirAlConmutador("take", [t = tipo.toStdString(), ms](escenas::Conmutador& c) { return c.take(t, ms); },
                      habiaArmada ? "error" : "aviso");
}

void EscenasViewModel::tbar(double t, bool soltar) {
    const FotoMotor::Salida* s = p_.ventanilla.foto().salida(p_.salidas.activaId());
    pedirAlConmutador("tbar", [t, soltar](escenas::Conmutador& c) { return c.tbar(t, soltar); },
                      s == nullptr || s->armada.empty() ? "aviso" : "error");
}

QVariantMap EscenasViewModel::transicionesEntrada() const {
    QVariantMap out;
    for (const auto& [escena, e] : p_.ventanilla.foto().transicionesEntrada.items())
        out[qs(escena)] = QVariantMap{{"tipo", qs(e.value("tipo", std::string()))}, {"ms", e.value("ms", -1)}};
    return out;
}

QVariantMap EscenasViewModel::transicionEntrada(const QString& escena) const {
    return transicionesEntrada().value(escena).toMap();
}

QString EscenasViewModel::setTransicionEntrada(const QString& escena, const QString& tipo, int ms) {
    pedirAlConmutador("escena_transicion", [e = escena.toStdString(), t = tipo.toStdString(), ms](escenas::Conmutador& c) {
        return c.setTransicionEntrada(e, t, ms);
    });
    return {};
}

bool EscenasViewModel::afv(const QString& fuente) const { return afvFuentes().contains(fuente); }

void EscenasViewModel::setAfv(const QString& fuente, bool sigue) {
    pedirAlConmutador("audio_afv", [f = fuente.toStdString(), sigue](escenas::Conmutador& c) {
        c.setAfv(f, sigue);
        return std::string();
    });
}

QStringList EscenasViewModel::afvFuentes() const {
    QStringList out;
    if (const FotoMotor::Salida* s = p_.ventanilla.foto().salida(p_.salidas.activaId()))
        for (const std::string& f : s->afv) out << qs(f);
    return out;
}

void EscenasViewModel::esperarReplay(const std::string& antes, int ms, std::function<void(const QString&)> listo,
                                     const std::string& salida) {
    p_.ventanilla.pedir(
        this, "replay_ultimo", [&rt = p_.rt, salida]() { return rt.engine().ultimoReplay(salida); },
        [this, antes, ms, listo = std::move(listo), salida](const std::string& ahora) mutable {
            if (!ahora.empty() && ahora != antes) return listo(qs(ahora));
            if (ms <= 0) return listo(QString());
            QTimer::singleShot(200, this, [this, antes, ms, listo = std::move(listo), salida]() mutable {
                esperarReplay(antes, ms - 200, std::move(listo), salida);
            });
        });
}

QString EscenasViewModel::repetirUi() {
    p_.ventanilla.pedir(
        this, "replay_guardar",
        [&rt = p_.rt]() -> std::pair<std::string, std::string> {
            const std::string antes = rt.engine().ultimoReplay();
            if (!rt.engine().guardarReplay()) return {antes, rt.engine().lastError()};
            return {antes, {}};
        },
        [this](const std::pair<std::string, std::string>& r) {
            if (!r.second.empty()) return emit notify(qs(r.second), "error");
            esperarReplay(r.first, 8000, [this](const QString& f) {
                if (f.isEmpty()) return emit notify(tr("La repetición no llegó a escribirse."), "error");
                armarRepeticion(f, {}, [this](const QString& e) {
                    emit notify(e.isEmpty() ? tr("Repetición armada: dale TAKE.") : e, e.isEmpty() ? "ok" : "error");
                });
            });
        });
    return {};
}

void EscenasViewModel::armarRepeticion(const QString& archivo, const std::string& salida,
                                       std::function<void(const QString&)> listo) {
    p_.ventanilla.pedir(
        this, "replay_armar",
        [&rt = p_.rt, &cs = p_.salidas.conmutadores(), a = archivo.toStdString(), salida]() {
            return armarRepeticionEn(rt, cs, a, salida);
        },
        [listo = std::move(listo)](const std::string& e) { listo(qs(e)); });
}

} // namespace ba::app
