// ============================================================================
// src/app/ui/RecursosViewModel.cpp — ver RecursosViewModel.hpp
// ============================================================================
#include "ui/RecursosViewModel.hpp"

#include <QLocale>
#include <QMetaObject>
#include <QVariantMap>

#include <algorithm>

#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"

namespace ba::app {
namespace {

QString numero(double v, int decimales = 0) { return QLocale().toString(v, 'f', decimales); }

/// Un tamaño en MB como lo lee una persona: «850 MB», «12,3 GB».
QString tamano(double mb) {
    if (mb < 0) return QStringLiteral("?");
    return mb >= 1024 ? RecursosViewModel::tr("%1 GB").arg(numero(mb / 1024.0, 1)) : RecursosViewModel::tr("%1 MB").arg(numero(mb));
}

QVariantList aLista(const std::vector<double>& v) {
    QVariantList out;
    out.reserve(static_cast<int>(v.size()));
    for (double x : v) out.push_back(x);
    return out;
}

} // namespace

RecursosViewModel::RecursosViewModel(AppRuntime& rt, QObject* parent)
    : QObject(parent), rt_(rt), recursos_(std::make_unique<diag::Recursos>(600)) {
    // Mide en su hilo; lo que ve la interfaz se arma en el de la interfaz.
    recursos_->arrancar([this]() { QMetaObject::invokeMethod(this, [this]() { publicar(); }, Qt::QueuedConnection); });
}

RecursosViewModel::~RecursosViewModel() { recursos_->parar(); }

QString RecursosViewModel::titulo(const QString& clave) const {
    if (clave == "cpu") return tr("CPU");
    if (clave == "gpu") return tr("GPU");
    if (clave == "ram") return tr("RAM");
    if (clave == "vram") return tr("VRAM");
    if (clave == "red") return tr("Red");
    if (clave == "motor") return tr("Cuadros perdidos");
    return clave;
}

void RecursosViewModel::publicar() {
    // Los umbrales, de los ajustes avanzados (se leen en cada vuelta: cambiarlos no pide reiniciar).
    diag::Recursos::Umbrales u;
    u.aviso = ba::ajustes::numero("recursos.avisoPct", u.aviso);
    u.critico = ba::ajustes::numero("recursos.criticoPct", u.critico);
    u.sostenidoS = ba::ajustes::entero("recursos.sostenidoS", u.sostenidoS);
    recursos_->umbrales(u);

    // Lo del motor: cuadros por segundo, perdidos y lo que sale (para la vuelta que viene).
    const EngineStatus st = rt_.engine().status();
    const auto ahora = std::chrono::steady_clock::now();
    diag::Recursos::Motor mo;
    mo.fps = st.fps > 0 ? st.fps : diag::Recursos::kDesconocido;
    if (perdidosAntes_ >= 0 && st.totalFrames >= totalAntes_ && st.droppedFrames >= perdidosAntes_) {
        const int cuadros = st.totalFrames - totalAntes_, perdidos = st.droppedFrames - perdidosAntes_;
        mo.perdidosPct = cuadros > 0 ? std::min(100.0, perdidos * 100.0 / cuadros) : (st.streaming || st.recording ? 0.0 : -1.0);
    }
    const double dt = std::chrono::duration<double>(ahora - tAntes_).count();
    if (st.streaming && bytesAntes_ > 0 && st.bytesSent >= bytesAntes_ && dt > 0.2)
        mo.salidaKbps = static_cast<double>(st.bytesSent - bytesAntes_) * 8.0 / 1000.0 / dt;
    perdidosAntes_ = st.droppedFrames;
    totalAntes_ = st.totalFrames;
    bytesAntes_ = st.streaming ? st.bytesSent : 0;
    tAntes_ = ahora;
    recursos_->motor(mo);
    // El gateway se relanza con otro pid: se sigue al que está.
    if (const std::uint32_t gw = rt_.gatewayPid(); gw != gatewayPid_) {
        gatewayPid_ = gw;
        recursos_->procesos({{"gateway", gw}});
    }

    const diag::Recursos::Muestra m = recursos_->ultima();
    const nlohmann::json inf = recursos_->informe();
    const QString desconocida = tr("desconocida");
    const auto fila = [&](const QString& clave, double pct, const QString& texto, const QString& pico,
                          const std::string& serie, double techo, const std::string& serie2 = {}) {
        QVariantMap f{{"clave", clave},
                      {"titulo", titulo(clave)},
                      {"pct", pct},
                      {"texto", texto},
                      {"conocido", texto != desconocida},
                      {"pico", pico},
                      {"estado", QString::fromStdString(inf.value(clave.toStdString(), nlohmann::json::object())
                                                            .value("estado", std::string("desconocido")))},
                      {"historia", aLista(recursos_->serie(serie, kHistoriaS))},
                      {"techo", techo}};
        if (!serie2.empty()) f["historia2"] = aLista(recursos_->serie(serie2, kHistoriaS));
        return f;
    };
    const auto pico = [&](const std::string& clave) {
        const double p = recursos_->pico(clave);
        return p >= 0 ? tr("pico %1 %").arg(numero(p)) : QString();
    };
    QVariantList filas;

    // CPU: la de la máquina y, al lado, la nuestra (la app, el gateway).
    {
        double nuestra = 0;
        bool alguna = false;
        for (const auto& p : m.procesos)
            if (p.cpu >= 0) {
                nuestra += p.cpu;
                alguna = true;
            }
        const QString texto = m.cpu < 0 ? desconocida
                              : alguna ? tr("%1 % · nuestra %2 %").arg(numero(m.cpu), numero(nuestra))
                                       : tr("%1 %").arg(numero(m.cpu));
        filas.push_back(fila("cpu", m.cpu, texto, pico("cpu"), "cpu", 100));
    }
    filas.push_back(fila("gpu", m.gpu, m.gpu < 0 ? desconocida : tr("%1 %").arg(numero(m.gpu)), pico("gpu"), "gpu", 100));
    filas.push_back(fila("ram", m.ramPct(),
                         m.ramTotalMb > 0 ? tr("%1 de %2").arg(tamano(m.ramUsadaMb), tamano(m.ramTotalMb)) : desconocida,
                         pico("ram"), "ram", 100));
    // La memoria de video: con total, como la RAM; en memoria unificada, solo lo que usa (sin barra).
    if (m.vramUsadaMb >= 0)
        filas.push_back(fila("vram", m.vramPct(),
                             m.vramTotalMb > 0 ? tr("%1 de %2").arg(tamano(m.vramUsadaMb), tamano(m.vramTotalMb))
                                               : tr("%1 en uso (compartida con la RAM)").arg(tamano(m.vramUsadaMb)),
                             m.vramTotalMb > 0 ? pico("vram") : QString(), m.vramTotalMb > 0 ? "vram" : "", 100));
    // La red: lo que sale (lo que más pesa con varias salidas) y lo que entra; la
    // barra, si se sabe la velocidad del enlace.
    {
        const auto ent = recursos_->serie("red_entrada", kHistoriaS), sal = recursos_->serie("red_salida", kHistoriaS);
        double techo = 1;
        for (double v : ent) techo = std::max(techo, v);
        for (double v : sal) techo = std::max(techo, v);
        const QString texto = m.redSalidaMbps < 0 ? desconocida
                                                  : tr("↑ %1 · ↓ %2 Mbps").arg(numero(m.redSalidaMbps, 1), numero(m.redEntradaMbps, 1));
        const double ps = recursos_->pico("red_salida");
        filas.push_back(fila("red", m.redPct, texto, ps >= 0 ? tr("pico ↑ %1").arg(numero(ps, 1)) : QString(), "red_salida",
                             techo * 1.1, "red_entrada"));
    }

    QVariantList procs;
    for (const auto& p : m.procesos) {
        const QString nom = p.nombre == "app" ? tr("app") : p.nombre == "gateway" ? tr("gateway") : p.nombre == "launcher" ? tr("launcher")
                                                                                                    : QString::fromStdString(p.nombre);
        procs.push_back(QVariantMap{
            {"nombre", nom},
            {"texto", p.cpu < 0 && p.rssMb < 0 ? tr("%1: no responde").arg(nom)
                                               : tr("%1 %2 % · %3").arg(nom, numero(std::max(0.0, p.cpu), 1), tamano(p.rssMb))}});
    }

    const nlohmann::json mg = inf.value("margen", nlohmann::json::object());
    filas_ = filas;
    procesos_ = procs;
    estado_ = QString::fromStdString(inf.value("estado", std::string("desconocido")));
    margen_ = mg.value("pct", nlohmann::json()).is_number() ? mg["pct"].get<double>() : -1.0;
    limita_ = mg.value("limita", nlohmann::json()).is_string() ? titulo(QString::fromStdString(mg["limita"].get<std::string>())) : QString();
    emit cambio();
}

} // namespace ba::app
