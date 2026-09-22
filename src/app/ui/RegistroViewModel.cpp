// ============================================================================
// src/app/ui/RegistroViewModel.cpp — ver RegistroViewModel.hpp
// ============================================================================
#include "ui/RegistroViewModel.hpp"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>

#include "AppRuntime.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/LectorDeLogs.hpp"
#include "diag/Registro.hpp"

namespace ba::app {
namespace {

/// Cada cuánto se vuelve a leer mientras se sigue la cola. Dos segundos: un
/// registro no es un vúmetro, y leer la cola de cuatro archivos cada 200 ms
/// sería gastar disco para mirar lo mismo.
constexpr int kSeguirMs = 2000;

/// Cuántas líneas se muestran como mucho. Más que esto no lo lee nadie y la
/// lista se pone pesada; para más, están los filtros.
constexpr int kTope = 800;

QString horaLocal(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("dd/MM HH:mm:ss.zzz");
}

} // namespace

struct RegistroViewModel::Impl {
    AppRuntime& rt;
    QVariantList lineas;
    QStringList modulos;
    ba::diag::Filtro filtro;
    bool siguiendo = true;
    bool enMemoria = false;
    QTimer reloj;

    explicit Impl(AppRuntime& r) : rt(r) { filtro.cuantas = kTope; }

    QString carpeta() const { return QString::fromStdString((rt.dataRoot() / "Logs").string()); }
};

RegistroViewModel::RegistroViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt)) {
    d_->reloj.setInterval(kSeguirMs);
    connect(&d_->reloj, &QTimer::timeout, this, &RegistroViewModel::refrescar);
    d_->reloj.start();
    refrescar();
}

RegistroViewModel::~RegistroViewModel() = default;

QVariantList RegistroViewModel::lineas() const { return d_->lineas; }

QStringList RegistroViewModel::niveles() const {
    return {"traza", "depuración", "info", "aviso", "error", "fatal"};
}

QStringList RegistroViewModel::modulos() const { return d_->modulos; }

int RegistroViewModel::minimo() const { return static_cast<int>(d_->filtro.minimo); }

void RegistroViewModel::setMinimo(int n) {
    const auto v = static_cast<ba::diag::Nivel>(std::clamp(n, 0, 5));
    if (v == d_->filtro.minimo) return;
    d_->filtro.minimo = v;
    emit filtroCambio();
    refrescar();
}

QString RegistroViewModel::modulo() const {
    return d_->filtro.componentes.empty() ? QString()
                                          : QString::fromStdString(d_->filtro.componentes.front());
}

void RegistroViewModel::setModulo(const QString& m) {
    if (m == modulo()) return;
    d_->filtro.componentes.clear();
    if (!m.isEmpty()) d_->filtro.componentes.push_back(m.toStdString());
    emit filtroCambio();
    refrescar();
}

QString RegistroViewModel::busca() const { return QString::fromStdString(d_->filtro.busca); }

void RegistroViewModel::setBusca(const QString& b) {
    if (b.toStdString() == d_->filtro.busca) return;
    d_->filtro.busca = b.toStdString();
    emit filtroCambio();
    refrescar();
}

bool RegistroViewModel::siguiendo() const { return d_->siguiendo; }

void RegistroViewModel::setSiguiendo(bool s) {
    if (s == d_->siguiendo) return;
    d_->siguiendo = s;
    // Se deja de leer del todo al dejar de seguir: mirar una línea de hace
    // media hora no tiene por qué costar una lectura de disco cada dos
    // segundos, y además la lista se movería sola mientras se lee.
    if (s) d_->reloj.start();
    else d_->reloj.stop();
    emit filtroCambio();
}

bool RegistroViewModel::enMemoria() const { return d_->enMemoria; }

void RegistroViewModel::setEnMemoria(bool m) {
    if (m == d_->enMemoria) return;
    d_->enMemoria = m;
    emit filtroCambio();
    refrescar();
}

int RegistroViewModel::nivelDelProceso() const {
    return static_cast<int>(ba::diag::registro().nivel());
}

void RegistroViewModel::setNivelDelProceso(int n) {
    const auto v = static_cast<ba::diag::Nivel>(std::clamp(n, 0, 5));
    if (v == ba::diag::registro().nivel()) return;
    ba::diag::registro().setNivel(v);
    // Queda anotado en el propio registro: si mañana aparece un log lleno de
    // trazas, tiene que decir quién lo puso así y cuándo.
    ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                               std::string("nivel de registro cambiado a ") +
                                   ba::diag::nombreDeNivel(v) + " desde el visor");
    emit cambio();
    refrescar();
}

qulonglong RegistroViewModel::descartadas() const { return ba::diag::registro().descartadas(); }

QString RegistroViewModel::carpeta() const { return d_->carpeta(); }

QStringList RegistroViewModel::apagados() const {
    QStringList out;
    for (const std::string& s : ba::diag::diario().apagados()) out << QString::fromStdString(s);
    return out;
}

QString RegistroViewModel::culpable() const {
    return QString::fromStdString(ba::diag::diario().culpable());
}

void RegistroViewModel::prender(const QString& id) {
    ba::diag::diario().setApagado(id.toStdString(), false);
    ba::diag::registro().linea(ba::diag::Nivel::Info, "arranque", 0,
                               "«" + id.toStdString() + "» vuelve a cargarse en el próximo arranque");
    emit cambio();
    emit notify("«" + id + "» se va a cargar en el próximo arranque", "ok");
}

QString RegistroViewModel::exportar() {
    const QString nombre =
        "registro-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt";
    const QString ruta = QDir(d_->carpeta()).filePath(nombre);
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit notify("No se pudo escribir " + ruta, "error");
        return {};
    }
    // Con el filtro adelante: quien lo reciba tiene que saber que esta mirando
    // un recorte y cual.
    QString cab = "# Registro exportado " +
                  QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss") + "\n";
    cab += "# desde nivel: " + niveles().value(minimo(), "traza");
    if (!modulo().isEmpty()) cab += "  modulo: " + modulo();
    if (!busca().isEmpty()) cab += "  busca: " + busca();
    cab += "  (" + QString::number(d_->lineas.size()) + " lineas)\n\n";
    f.write(cab.toUtf8());
    f.write(comoTexto().toUtf8());
    f.close();
    ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                               "registro exportado a " + nombre.toStdString());
    emit notify("Registro guardado en Logs/" + nombre, "ok");
    return ruta;
}

void RegistroViewModel::refrescar() {
    std::vector<ba::diag::Registrado> rs;
    if (d_->enMemoria) {
        // El anillo no está filtrado: se filtra acá con el mismo criterio que
        // el lector, para que cambiar de fuente no cambie lo que se ve.
        const std::string busca = d_->filtro.busca;
        for (const auto& r : ba::diag::registro().ultimas(kTope)) {
            if (static_cast<int>(r.nivel) < static_cast<int>(d_->filtro.minimo)) continue;
            if (!d_->filtro.componentes.empty() &&
                std::find(d_->filtro.componentes.begin(), d_->filtro.componentes.end(),
                          r.componente) == d_->filtro.componentes.end())
                continue;
            if (!busca.empty() &&
                QString::fromStdString(r.texto).contains(QString::fromStdString(busca),
                                                         Qt::CaseInsensitive) == false &&
                QString::fromStdString(r.componente).contains(QString::fromStdString(busca),
                                                              Qt::CaseInsensitive) == false)
                continue;
            rs.push_back(r);
        }
    } else {
        rs = ba::diag::leerLogs(d_->rt.dataRoot() / "Logs", d_->filtro);
    }

    QVariantList out;
    out.reserve(static_cast<int>(rs.size()));
    for (const auto& r : rs)
        out.append(QVariantMap{{"hora", horaLocal(r.ms)},
                               {"ms", static_cast<qlonglong>(r.ms)},
                               {"nivel", QString::fromUtf8(ba::diag::nombreDeNivel(r.nivel))},
                               {"nivelN", static_cast<int>(r.nivel)},
                               {"modulo", QString::fromStdString(r.componente)},
                               {"codigo", r.codigo},
                               {"texto", QString::fromStdString(r.texto)}});

    // Se avisa solo si cambió: si no, la lista se redibuja cada dos segundos y
    // se pierde la posición del que está leyendo.
    if (out == d_->lineas) return;
    d_->lineas = out;

    QStringList mods;
    for (const std::string& m : ba::diag::componentesEnLogs(d_->rt.dataRoot() / "Logs"))
        mods << QString::fromStdString(m);
    for (const QVariant& v : out) {
        const QString m = v.toMap().value("modulo").toString();
        if (!m.isEmpty() && !mods.contains(m)) mods << m;
    }
    mods.sort();
    d_->modulos = mods;
    emit cambio();
}

QString RegistroViewModel::comoTexto() const {
    QString out;
    for (const QVariant& v : d_->lineas) {
        const QVariantMap m = v.toMap();
        out += m.value("hora").toString() + "  " + m.value("nivel").toString().leftJustified(10) +
               m.value("modulo").toString().leftJustified(12) + m.value("texto").toString() + "\n";
    }
    return out;
}

void RegistroViewModel::marcar(const QString& texto) {
    ba::diag::registro().linea(ba::diag::Nivel::Info, "marca", 0,
                               texto.isEmpty() ? "marca del operador" : texto.toStdString());
    refrescar();
    emit notify("Marca puesta en el registro", "ok");
}

} // namespace ba::app
