// src/app/ui/EscenasViewModel.cpp — ver el .hpp

#include "ui/EscenasViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "escenas/AjustesEscenas.hpp"
#include "escenas/Conmutador.hpp"
#include "ui/ListModels.hpp"
#include "ui/SalidasViewModel.hpp"

#include <QMap>
#include <QTimer>

#include <algorithm>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

} // namespace

EscenasViewModel::EscenasViewModel(Partes p, QObject* parent)
    : QObject(parent), p_(std::move(p)), ajustes_(std::make_unique<escenas::AjustesEscenas>(p_.rt.dataRoot())) {
    // El AFV es de la salida activa: cambia con su conmutador y cuando se elige otra.
    connect(&p_.salidas, &SalidasViewModel::afvActivaCambio, this, &EscenasViewModel::afvCambio);
    connect(&p_.salidas, &SalidasViewModel::activaElegida, this, &EscenasViewModel::afvCambio);
}

EscenasViewModel::~EscenasViewModel() = default;

QString EscenasViewModel::escenaAlAire() const { return qs(p_.rt.engine().currentScene(p_.salidas.activaId())); }

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
    for (const std::string& n : p_.rt.orderedSceneNames()) {
        const QString carpeta = qs(p_.rt.sceneFolder(n));
        if (carpeta.isEmpty()) {
            sueltas.append(qs(n));
            continue;
        }
        if (!donde.contains(carpeta)) {
            donde[carpeta] = static_cast<int>(carpetas.size());
            carpetas.append(QVariantMap{{"carpeta", carpeta}, {"escenas", QVariantList{}}});
        }
        QVariantMap g = carpetas[donde[carpeta]].toMap();
        QVariantList e = g.value("escenas").toList();
        e.append(qs(n));
        g["escenas"] = e;
        carpetas[donde[carpeta]] = g;
    }
    QVariantList out;
    if (!sueltas.isEmpty()) out.append(QVariantMap{{"carpeta", QString()}, {"escenas", sueltas}});
    out.append(carpetas);
    return out;
}

void EscenasViewModel::addScene(const QString& name) {
    if (!p_.rt.addScene(name.trimmed().toStdString())) {
        emit notify(qs(p_.rt.lastError()), "error");
        return;
    }
    p_.lista.reload();
    emit listaCambio();
    p_.rt.save();
}

QString EscenasViewModel::copyScene(const QString& from, const QString& to) {
    // Sin nombre, se propone uno libre: copiar tiene que ser un clic, no un
    // dialogo. Se renombra despues, que es cuando uno ya sabe como llamarla.
    QString destino = to;
    if (destino.isEmpty()) {
        const auto existe = [this](const QString& n) {
            for (const std::string& s : p_.rt.orderedSceneNames())
                if (qs(s) == n) return true;
            return false;
        };
        destino = tr("%1 (copia)").arg(from);
        for (int i = 2; existe(destino); ++i) destino = tr("%1 (copia %2)").arg(from).arg(i);
    }
    if (!p_.rt.copyScene(from.toStdString(), destino.toStdString())) {
        emit notify(qs(p_.rt.lastError()), "error");
        return {};
    }
    p_.lista.reload();
    emit listaCambio();
    emit escenaCambio();
    p_.rt.save();
    const std::string aviso = p_.rt.lastError();
    emit notify(aviso.empty() ? tr("Escena copiada: %1").arg(destino) : tr("Escena copiada, pero %1").arg(qs(aviso)),
                aviso.empty() ? "ok" : "warn");
    return destino;
}

void EscenasViewModel::setSceneProtected(const QString& name, bool protegida) {
    if (!p_.rt.setSceneProtected(name.toStdString(), protegida)) {
        emit notify(qs(p_.rt.lastError()), "error");
        return;
    }
    p_.lista.reload();
    emit listaCambio();
    p_.rt.save();
    emit notify(protegida ? tr("«%1» protegida: no se puede borrar ni renombrar").arg(name) : tr("«%1» ya no está protegida").arg(name),
                "ok");
}

bool EscenasViewModel::sceneProtected(const QString& name) const { return p_.rt.sceneProtected(name.toStdString()); }

void EscenasViewModel::removeScene(const QString& name) {
    if (!p_.rt.removeScene(name.toStdString())) {
        emit notify(qs(p_.rt.lastError()), "error");
        return;
    }
    p_.lista.reload();
    emit listaCambio();
    if (p_.fuentes.scene() == name) p_.fuentes.setScene(escenaAlAire());
    emit escenaCambio();
    p_.rt.save();
}

void EscenasViewModel::setSceneFolder(const QString& name, const QString& folder) {
    if (!p_.rt.setSceneFolder(name.toStdString(), folder.trimmed().toStdString())) {
        emit notify(qs(p_.rt.lastError()), "error");
        return;
    }
    p_.lista.reload();
    emit listaCambio();
    emit escenaCambio();
    p_.rt.save();
}

QStringList EscenasViewModel::sceneFolders() const {
    QStringList out;
    for (const auto& f : p_.rt.sceneFolders()) out << qs(f);
    return out;
}

void EscenasViewModel::moveScene(const QString& name, const QString& before) {
    if (!p_.rt.moveScene(name.toStdString(), before.toStdString())) {
        emit notify(qs(p_.rt.lastError()), "error");
        return;
    }
    p_.lista.reload();
    emit listaCambio();
    emit escenaCambio();
    p_.rt.save();
}

void EscenasViewModel::renameScene(const QString& from, const QString& to) {
    if (!p_.rt.renameScene(from.toStdString(), to.trimmed().toStdString())) {
        emit notify(qs(p_.rt.lastError()), "error");
        return;
    }
    p_.lista.reload();
    emit listaCambio();
    if (p_.fuentes.scene() == from) p_.fuentes.setScene(to.trimmed());
    emit escenaCambio();
    p_.rt.save();
}

void EscenasViewModel::setScenePreview(const QString& name) {
    // 960 px de ancho alcanza para el lienzo del editor y no castiga la GPU.
    p_.rt.engine().enableScenePreview(name.toStdString(), 960);
}

void EscenasViewModel::takeScene(const QString& name) {
    if (const std::string e = p_.salidas.conmutadorActivo().poner(name.toStdString()); !e.empty()) emit notify(qs(e), "error");
}

void EscenasViewModel::armarEscena(const QString& name) {
    if (const std::string e = p_.salidas.conmutadorActivo().armar(name.toStdString()); !e.empty()) emit notify(qs(e), "aviso");
}

void EscenasViewModel::take() { takeCon(QString(), -1); }

void EscenasViewModel::takeCon(const QString& tipo, int ms) {
    escenas::Conmutador& c = p_.salidas.conmutadorActivo();
    const bool habiaArmada = !c.armada().empty();
    if (const std::string e = c.take(tipo.toStdString(), ms); !e.empty()) emit notify(qs(e), habiaArmada ? "error" : "aviso");
}

void EscenasViewModel::tbar(double t, bool soltar) {
    escenas::Conmutador& c = p_.salidas.conmutadorActivo();
    if (const std::string e = c.tbar(t, soltar); !e.empty()) emit notify(qs(e), c.armada().empty() ? "aviso" : "error");
}

QVariantMap EscenasViewModel::transicionEntrada(const QString& escena) const {
    const nlohmann::json e = p_.salidas.conmutador().transicionEntrada(escena.toStdString());
    if (e.empty()) return {};
    return {{"tipo", QString::fromStdString(e.value("tipo", std::string()))}, {"ms", e.value("ms", -1)}};
}

QString EscenasViewModel::setTransicionEntrada(const QString& escena, const QString& tipo, int ms) {
    return qs(p_.salidas.conmutador().setTransicionEntrada(escena.toStdString(), tipo.toStdString(), ms));
}

bool EscenasViewModel::afv(const QString& fuente) const { return p_.salidas.conmutadorActivo().afv(fuente.toStdString()); }

void EscenasViewModel::setAfv(const QString& fuente, bool sigue) { p_.salidas.conmutadorActivo().setAfv(fuente.toStdString(), sigue); }

QStringList EscenasViewModel::afvFuentes() const {
    QStringList out;
    for (const std::string& f : p_.salidas.conmutadorActivo().afvFuentes()) out << qs(f);
    return out;
}

void EscenasViewModel::esperarReplay(const std::string& antes, int ms, std::function<void(const QString&)> listo,
                                     const std::string& salida) {
    const std::string ahora = p_.rt.engine().ultimoReplay(salida);
    if (!ahora.empty() && ahora != antes) return listo(qs(ahora));
    if (ms <= 0) return listo(QString());
    QTimer::singleShot(200, this, [this, antes, ms, listo = std::move(listo), salida]() mutable {
        esperarReplay(antes, ms - 200, std::move(listo), salida);
    });
}

QString EscenasViewModel::repetirUi() {
    const std::string antes = p_.rt.engine().ultimoReplay();
    if (!p_.rt.engine().guardarReplay()) return qs(p_.rt.engine().lastError());
    esperarReplay(antes, 8000, [this](const QString& f) {
        if (f.isEmpty()) return emit notify(tr("La repetición no llegó a escribirse."), "error");
        const QString e = armarRepeticion(f);
        emit notify(e.isEmpty() ? tr("Repetición armada: dale TAKE.") : e, e.isEmpty() ? "ok" : "error");
    });
    return {};
}

QString EscenasViewModel::armarRepeticion(const QString& archivo, const std::string& salida) {
    static const std::string kEscena = "Repetición", kFuente = "Repetición (clip)";
    AppRuntime& rt = p_.rt;
    BroadcastEngine& e = rt.engine();
    const auto escenas = e.sceneNames();
    if (std::find(escenas.begin(), escenas.end(), kEscena) == escenas.end() && !rt.addScene(kEscena)) return qs(rt.lastError());
    const std::string ajustes =
        nlohmann::json{{"local_file", archivo.toStdString()}, {"looping", false}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const auto fuentes = e.sceneSources(kEscena);
    if (std::find(fuentes.begin(), fuentes.end(), kFuente) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = kFuente;
        spec.settingsJson = ajustes;
        if (!rt.addSource(kEscena, spec)) return qs(rt.lastError());
    } else if (!rt.updateSourceSettings(kFuente, ajustes)) {
        return qs(rt.lastError());
    }
    p_.lista.reload();
    emit listaCambio();
    rt.save();
    if (salida.empty()) {
        armarEscena(qs(kEscena));
        return {};
    }
    escenas::Conmutador* c = p_.salidas.conmutadores().de(salida);
    if (c == nullptr) return tr("No hay una salida «%1».").arg(qs(salida));
    return qs(c->armar(kEscena));
}

} // namespace ba::app
