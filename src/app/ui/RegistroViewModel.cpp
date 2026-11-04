// ============================================================================
// src/app/ui/RegistroViewModel.cpp — ver RegistroViewModel.hpp
// ============================================================================
#include "ui/RegistroViewModel.hpp"
#include "persist/ColaEscritura.hpp"
#include "i18n/I18nTexto.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <atomic>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QThreadPool>
#include <QTimer>

#include "arranque/RuntimeMotor.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/LectorDeLogs.hpp"
#include "diag/Registro.hpp"

namespace ba::app {
namespace {

/// Cada cuánto se miran los archivos mientras se sigue la cola (lo de los otros procesos;
/// lo de este llega por aviso). Dos segundos: un registro no es un vúmetro. La lectura es
/// en el fondo.
constexpr int kSeguirMs = 2000;
/// Lo que llega junto del registro de este proceso se junta en una sola relectura.
constexpr int kJuntarMs = 250;

/// Cuántas líneas se muestran como mucho. Más que esto no lo lee nadie y la
/// lista se pone pesada; para más, están los filtros.
inline int kTope() { return ba::ajustes::entero("limites.registroLineas", 800); }

QString horaLocal(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("dd/MM HH:mm:ss.zzz");
}

QVariantList aLineas(const std::vector<ba::diag::Registrado>& rs) {
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
    return out;
}

/// Los módulos para elegir: los de los archivos más los que aparecen en lo que se ve.
QStringList aModulos(const std::vector<std::string>& enLogs, const QVariantList& lineas) {
    QStringList mods;
    for (const std::string& m : enLogs) mods << QString::fromStdString(m);
    for (const QVariant& v : lineas) {
        const QString m = v.toMap().value("modulo").toString();
        if (!m.isEmpty() && !mods.contains(m)) mods << m;
    }
    mods.sort();
    return mods;
}

} // namespace

struct RegistroViewModel::Impl {
    AppRuntime& rt;
    QVariantList lineas;
    QStringList modulos;
    ba::diag::Filtro filtro;
    bool siguiendo = true;
    bool enMemoria = false;
    QTimer reloj;   ///< los archivos, mientras se sigue (lo de los otros procesos)
    QTimer juntar;  ///< lo que avisó el registro de este proceso, junto
    int oyente = -1;
    std::atomic<bool> avisado{false};
    // La lectura de los archivos, en el fondo (una por vez; la última pedida manda).
    QThreadPool fondo;
    std::uint64_t gen = 0;
    bool leyendo = false, otraVez = false;
    QStringList modulosEnLogs; ///< de la última lectura de los archivos

    explicit Impl(AppRuntime& r) : rt(r) {
        filtro.cuantas = kTope();
        fondo.setMaxThreadCount(1);
    }

    QString carpeta() const { return QString::fromStdString((ba::platform::dataRoot() / "Logs").string()); }
};

RegistroViewModel::RegistroViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(rt)) {
    d_->reloj.setInterval(kSeguirMs);
    connect(&d_->reloj, &QTimer::timeout, this, [this]() {
        if (!d_->enMemoria) refrescar();
    });
    d_->juntar.setSingleShot(true);
    d_->juntar.setInterval(kJuntarMs);
    connect(&d_->juntar, &QTimer::timeout, this, [this]() {
        d_->avisado = false;
        refrescar();
    });
    // Se sigue cuando alguien mira (usar).
}

RegistroViewModel::~RegistroViewModel() {
    seguir(false);
    d_->fondo.waitForDone();
}

void RegistroViewModel::seguir(bool si) {
    if (si == (d_->oyente >= 0)) return;
    if (si) {
        // Desde el hilo escritor del registro: solo marca y pasa a este hilo una vez.
        d_->oyente = ba::diag::registro().agregarOyente([this](const ba::diag::Registrado&) {
            if (!d_->avisado.exchange(true)) QMetaObject::invokeMethod(&d_->juntar, qOverload<>(&QTimer::start), Qt::QueuedConnection);
        });
        d_->reloj.start();
    } else {
        ba::diag::registro().quitarOyente(d_->oyente);
        d_->oyente = -1;
        d_->reloj.stop();
        d_->juntar.stop();
        d_->avisado = false;
    }
}

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
    seguir(s && vistas_.alguien());
    emit filtroCambio();
}

void RegistroViewModel::alCambiarVistas() {
    const bool antes = d_->oyente >= 0;
    seguir(vistas_.alguien() && d_->siguiendo);
    if (vistas_.alguien() && !antes) refrescar();
}

bool RegistroViewModel::enMemoria() const { return d_->enMemoria; }

void RegistroViewModel::setEnMemoria(bool m) {
    if (m == d_->enMemoria) return;
    d_->enMemoria = m;
    emit filtroCambio();
    refrescar();
}

int RegistroViewModel::nivelDelProceso() const { return static_cast<int>(ba::diag::registro().nivel()); }

void RegistroViewModel::setNivelDelProceso(int n) {
    const auto v = static_cast<ba::diag::Nivel>(std::clamp(n, 0, 5));
    if (v == ba::diag::registro().nivel()) return;
    ba::diag::registro().setNivel(v);
    // Queda anotado en el propio registro: si mañana aparece un log lleno de
    // trazas, tiene que decir quién lo puso así y cuándo.
    ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                               ba::i18n::llenar(ba::i18n::traducir("registro", "nivel de registro cambiado a %1 desde el visor"),
                                                {ba::diag::nombreDeNivel(v)}));
    emit cambio();
    refrescar();
}

qulonglong RegistroViewModel::descartadas() const { return ba::diag::registro().descartadas(); }

QString RegistroViewModel::carpeta() const { return d_->carpeta(); }

QStringList RegistroViewModel::apagados() const {
    QStringList out;
    for (const std::string& s : ba::diag::diario().apagados())
        out << QString::fromStdString(s);
    return out;
}

QString RegistroViewModel::culpable() const { return QString::fromStdString(ba::diag::diario().culpable()); }

void RegistroViewModel::prender(const QString& id) {
    ba::diag::diario().setApagado(id.toStdString(), false);
    ba::diag::registro().linea(ba::diag::Nivel::Info, "arranque", 0,
                               ba::i18n::llenar(ba::i18n::traducir("arranque", "«%1» vuelve a cargarse en el próximo arranque"),
                                                {id.toStdString()}));
    emit cambio();
    emit notify(tr("«%1» se va a cargar en el próximo arranque").arg(id), "ok");
}

QString RegistroViewModel::exportar() {
    const QString nombre = "registro-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt";
    const QString ruta = QDir(d_->carpeta()).filePath(nombre);
    // Con el filtro adelante: quien lo reciba tiene que saber que esta mirando
    // un recorte y cual.
    QString cab = "# " + tr("Registro exportado %1").arg(QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm:ss")) + "\n";
    cab += "# " + tr("desde nivel: %1").arg(niveles().value(minimo(), "traza"));
    if (!modulo().isEmpty()) cab += "  " + tr("modulo: %1").arg(modulo());
    if (!busca().isEmpty()) cab += "  " + tr("busca: %1").arg(busca());
    cab += "  " + tr("(%1 lineas)").arg(d_->lineas.size()) + "\n\n";
    // El escritor único, por su cola (entero o nada, fuera de la interfaz).
    ba::persist::cola().escribir(ruta.toStdWString(), (cab + comoTexto()).toUtf8().toStdString());
    ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                               ba::i18n::llenar(ba::i18n::traducir("registro", "registro exportado a %1"), {nombre.toStdString()}));
    emit notify(tr("Registro guardado en Logs/%1").arg(nombre), "ok");
    return ruta;
}

void RegistroViewModel::refrescar() {
    if (d_->enMemoria) {
        // El anillo no está filtrado: se filtra acá con el mismo criterio que
        // el lector, para que cambiar de fuente no cambie lo que se ve. Es memoria: ya.
        std::vector<ba::diag::Registrado> rs;
        const std::string busca = d_->filtro.busca;
        for (const auto& r : ba::diag::registro().ultimas(kTope())) {
            if (static_cast<int>(r.nivel) < static_cast<int>(d_->filtro.minimo)) continue;
            if (!d_->filtro.componentes.empty() &&
                std::find(d_->filtro.componentes.begin(), d_->filtro.componentes.end(), r.componente) ==
                    d_->filtro.componentes.end())
                continue;
            if (!busca.empty() &&
                QString::fromStdString(r.texto).contains(QString::fromStdString(busca),
                                                         Qt::CaseInsensitive) == false &&
                QString::fromStdString(r.componente)
                        .contains(QString::fromStdString(busca), Qt::CaseInsensitive) == false)
                continue;
            rs.push_back(r);
        }
        const QVariantList out = aLineas(rs);
        llego(++d_->gen, out, aModulos({}, out));
        return;
    }
    // Los archivos pesan megas: se leen en el fondo, uno por vez (si se pide otra vez
    // mientras lee, se lee de nuevo al terminar, con el filtro de ese momento).
    if (d_->leyendo) {
        d_->otraVez = true;
        return;
    }
    d_->leyendo = true;
    const std::uint64_t gen = ++d_->gen;
    d_->fondo.start([this, gen, filtro = d_->filtro, carpeta = ba::platform::dataRoot() / "Logs"]() {
        const QVariantList out = aLineas(ba::diag::leerLogs(carpeta, filtro));
        const QStringList mods = aModulos(ba::diag::componentesEnLogs(carpeta), out);
        QMetaObject::invokeMethod(
            this,
            [this, gen, out, mods]() {
                d_->leyendo = false;
                if (d_->otraVez) {
                    d_->otraVez = false;
                    refrescar();
                    return;
                }
                llego(gen, out, mods);
            },
            Qt::QueuedConnection);
    });
}

void RegistroViewModel::llego(std::uint64_t gen, QVariantList lineas, QStringList modulos) {
    if (gen != d_->gen) return; // se pidió otra cosa después (otro filtro, otra fuente)
    // Se avisa solo si cambió: si no, la lista se redibuja y se pierde la posición del que
    // está leyendo.
    if (lineas == d_->lineas && modulos == d_->modulos) return;
    d_->lineas = std::move(lineas);
    d_->modulos = std::move(modulos);
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
                               texto.isEmpty() ? ba::i18n::traducir("marca", "marca del operador") : texto.toStdString());
    refrescar();
    emit notify(tr("Marca puesta en el registro"), "ok");
}

} // namespace ba::app

#include "moc_RegistroViewModel.cpp"
