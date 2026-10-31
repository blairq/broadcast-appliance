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
#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "diag/VigiaBucle.hpp"
#include "i18n/I18n.hpp"

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
    medirServicio(m, inf);
    atenderTermica(m);
    // Lo de esta vuelta que el MCP lee desde el pool (fotoServicio).
    nlohmann::json termica = politica_.informe(m.t);
    termica["sin_gancho"] = ganchos_.sinGancho();
    fotoServicio_.poner({{"transporte", transporteVuelta_}, {"termica", std::move(termica)}});
    estado_ =QString::fromStdString(inf.value("estado", std::string("desconocido")));
    margen_ = mg.value("pct", nlohmann::json()).is_number() ? mg["pct"].get<double>() : -1.0;
    limita_ = mg.value("limita", nlohmann::json()).is_string() ? titulo(QString::fromStdString(mg["limita"].get<std::string>())) : QString();
    emit cambio();
}

// --- Métricas y SLI (F4-01) ------------------------------------------------------------

void RecursosViewModel::medirServicio(const diag::Recursos::Muestra& m, const nlohmann::json& inf) {
    auto& met = diag::Metricas::proceso();
    const auto medir = [](diag::Medidor& g, double v) {
        if (v < 0) g.desconocido();
        else g.poner(v);
    };
    // El motor y la máquina (lo que ya midió el muestreador: acá solo se copia).
    static auto& fps = met.medidor("ba_motor_fps", QT_TRANSLATE_NOOP("metricas", "Cuadros por segundo del programa"));
    static auto& perdidos = met.medidor("ba_motor_cuadros_perdidos_pct", QT_TRANSLATE_NOOP("metricas", "Cuadros perdidos en el último segundo, %"));
    static auto& perdidosTotal = met.contador("ba_motor_cuadros_perdidos_total", QT_TRANSLATE_NOOP("metricas", "Cuadros perdidos desde que arrancó el motor"));
    medir(fps, m.fps);
    medir(perdidos, m.perdidosPct);
    if (perdidosAntes_ >= 0) perdidosTotal.igualar(static_cast<std::uint64_t>(perdidosAntes_));
    for (const char* k : {"cpu", "gpu", "ram", "vram", "red"}) {
        const nlohmann::json v = inf.value(k, nlohmann::json::object()).value("pct", nlohmann::json());
        medir(met.medidor("ba_recurso_pct", QT_TRANSLATE_NOOP("metricas", "Uso de cada recurso de la máquina, %"), {{"recurso", k}}),
              v.is_number() ? v.get<double>() : -1.0);
    }
    // El bucle de la interfaz (diag::VigiaBucle): el último minuto.
    const nlohmann::json bucle = diag::VigiaBucle::interfaz().informe();
    const nlohmann::json um = bucle.value("ultimo_minuto", nlohmann::json::object());
    const bool hayBucle = um.value("muestras", 0) > 0;
    static auto& bucleP99 = met.medidor("ba_ui_bucle_p99_ms", QT_TRANSLATE_NOOP("metricas", "Demora del bucle de la interfaz, p99 del último minuto, ms"));
    static auto& bucleMax = met.medidor("ba_ui_bucle_max_ms", QT_TRANSLATE_NOOP("metricas", "Demora del bucle de la interfaz, máximo del último minuto, ms"));
    static auto& bucleTarde = met.contador("ba_ui_bucle_tarde_total", QT_TRANSLATE_NOOP("metricas", "Turnos del bucle de la interfaz que llegaron tarde"));
    const double p99 = hayBucle ? um.value("p99_ms", -1.0) : -1.0;
    medir(bucleP99, p99);
    medir(bucleMax, hayBucle ? um.value("max_ms", -1.0) : -1.0);
    bucleTarde.igualar(bucle.value("tarde", std::uint64_t{0}));
    // Las salidas: el colchón de la grabación y las reconexiones de la transmisión.
    const nlohmann::json sal = rt_.engine().salidasAhora();
    const nlohmann::json disco = sal.value("disco", nlohmann::json::object());
    const double atraso = disco.value("atraso_s", -1.0);
    static auto& colchon = met.medidor("ba_grabacion_colchon_s", QT_TRANSLATE_NOOP("metricas", "Lo que espera al disco en el colchón de la grabación más atrasada, s"));
    medir(colchon, atraso);
    static auto& reconexiones = met.contador("ba_transmision_reconexiones_total", QT_TRANSLATE_NOOP("metricas", "Veces que una transmisión conectada se cortó y empezó a reconectar"));
    int reconectando = 0;
    const auto mirar = [&](const std::string& id, const nlohmann::json& st) {
        if (!st.is_object() || !st.value("andando", false)) {
            conectadaAntes_.erase(id);
            return;
        }
        const bool con = st.value("conectada", false);
        if (st.value("reconectando", false)) ++reconectando;
        if (!con && conectadaAntes_[id]) reconexiones.sumar();
        conectadaAntes_[id] = con;
    };
    // Con nombre: recorrer .items() de un temporal (value(...) devuelve una copia) es UB.
    const nlohmann::json salidas = sal.value("salidas", nlohmann::json::object());
    for (const auto& [id, fs] : salidas.items()) {
        mirar(id, fs.value("principal", nlohmann::json::object()));
        const nlohmann::json extras = fs.value("extras", nlohmann::json::object());
        for (const auto& [eid, x] : extras.items()) mirar(eid, x);
    }
    static auto& reconectandoAhora = met.medidor("ba_transmision_reconectando", QT_TRANSLATE_NOOP("metricas", "Transmisiones reconectando ahora"));
    reconectandoAhora.poner(reconectando);

    // Los SLI de la página de diagnóstico: el número y si está bien, con umbrales fijos.
    const auto estadoDe = [](double v, double aviso, double critico) -> QString {
        if (v < 0) return QStringLiteral("desconocido");
        return v >= critico ? QStringLiteral("critico") : v >= aviso ? QStringLiteral("aviso") : QStringLiteral("normal");
    };
    const QString nose = tr("sin datos");
    QVariantList slis;
    const auto sli = [&](const QString& clave, const QString& titulo, double v, const QString& texto, double aviso, double critico) {
        slis.push_back(QVariantMap{{"clave", clave}, {"titulo", titulo}, {"texto", v < 0 ? nose : texto}, {"estado", estadoDe(v, aviso, critico)}});
    };
    // El transporte A→B: la peor edad p99 y lo que se perdió en la ventana (lo mide el gateway).
    double edad = -1, perdidosVentana = -1;
    transporteVuelta_ = transporte_ ? transporte_() : nlohmann::json::array();
    for (const auto& f : transporteVuelta_) {
        if (!f.is_object()) continue;
        edad = std::max(edad, f.value("edad_p99_ms", -1.0));
        perdidosVentana = std::max(0.0, perdidosVentana) + f.value("descartados", 0.0) + f.value("faltantes", 0.0);
    }
    sli("edad_ab", tr("Edad A→B (p99)"), edad, tr("%1 ms").arg(numero(edad)), 50, 150);
    sli("anillos", tr("Anillos A→B: perdidos en 10 s"), perdidosVentana, numero(perdidosVentana), 1, 10);
    sli("cuadros", tr("Cuadros perdidos"), m.perdidosPct, tr("%1 %").arg(numero(m.perdidosPct, 1)), 1, 5);
    sli("bucle_ui", tr("Bucle de la interfaz (p99)"), p99, tr("%1 ms").arg(numero(p99)), 50, 200);
    double mcp = -1;
    QString mcpCual;
    for (const auto& [e, v] : met.valores("ba_mcp_herramienta_ms:p99"))
        if (v > mcp) {
            mcp = v;
            mcpCual = e.empty() ? QString() : QString::fromStdString(e.front().second);
        }
    sli("mcp", tr("Herramienta MCP más lenta (p99)"), mcp, tr("%1 ms · %2").arg(numero(mcp), mcpCual), 1000, 5000);
    const double candado = met.valor("ba_motor_candado_espera_ms:p99").value_or(-1.0);
    sli("candado", tr("Espera del candado del motor (p99)"), candado, tr("%1 ms").arg(numero(candado, 1)), 20, 50);
    sli("reconexiones", tr("Transmisiones reconectando"), reconectando, QString::number(reconectando), 1, 2);
    sli("colchon", tr("Colchón de grabación"), atraso, tr("%1 s").arg(numero(atraso, 1)), 2, 5);
    slis_ = slis;
}

// --- La térmica (F4-05) ------------------------------------------------------------------

void RecursosViewModel::atenderTermica(const diag::Recursos::Muestra& m) {
    platform::NivelTermico nivel = platform::NivelTermico::Desconocido;
    platform::nivelDeNombre(m.termica, nivel);
    diag::PoliticaTermica::Config c;
    c.activa = ba::ajustes::siNo("termica.priorizarPrograma", true);
    politica_.configurar(c);
    const int ahora = politica_.actualizar(nivel, m.t);
    // Lo que cambió, de a un paso (lo atiende el gancho de cada uno).
    for (const auto& [paso, apagar] : ganchos_.aplicar(ahora))
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "termica", 0,
            ba::i18n::llenar(apagar ? ba::i18n::traducir("termica", "la máquina está %1: se apaga «%2» para priorizar el programa")
                                    : ba::i18n::traducir("termica", "la máquina se alivió (%1): vuelve «%2»"),
                             {m.termica, paso}));
    auto& met = diag::Metricas::proceso();
    static auto& nivelMet = met.medidor("ba_termica_nivel", QT_TRANSLATE_NOOP("metricas", "Nivel térmico: 0 desconocido, 1 normal, 2 templado, 3 serio, 4 crítico"));
    static auto& temp = met.medidor("ba_termica_temperatura_c", QT_TRANSLATE_NOOP("metricas", "La temperatura más alta que se pudo leer, °C"));
    static auto& apagados = met.medidor("ba_termica_pasos_apagados", QT_TRANSLATE_NOOP("metricas", "Pasos de «priorizar programa» apagados por calor"));
    nivelMet.poner(static_cast<int>(nivel));
    if (m.temperaturaC < 0) temp.desconocido();
    else temp.poner(m.temperaturaC);
    apagados.poner(ahora);

    QStringList nombres;
    for (int i = 0; i < ahora; ++i) {
        const std::string& p = diag::PoliticaTermica::pasos()[static_cast<std::size_t>(i)];
        nombres << (p == "superres" ? tr("superresolución")
                    : p == "modelos" ? tr("modelos locales")
                    : p == "vistas"  ? tr("vistas sin mirar")
                                     : tr("resolución de invitados"));
    }
    const QString nivelTexto = nivel == platform::NivelTermico::Normal     ? tr("normal")
                               : nivel == platform::NivelTermico::Templado ? tr("templada")
                               : nivel == platform::NivelTermico::Serio    ? tr("caliente: el sistema frena")
                               : nivel == platform::NivelTermico::Critico  ? tr("muy caliente")
                                                                           : tr("sin datos");
    QString texto = m.temperaturaC >= 0 ? tr("%1 · %2 °C").arg(nivelTexto, numero(m.temperaturaC)) : nivelTexto;
    if (!nombres.isEmpty()) texto += " · " + tr("apagado: %1").arg(nombres.join(", "));
    const QString estado = nivel == platform::NivelTermico::Critico                       ? QStringLiteral("critico")
                           : nivel == platform::NivelTermico::Serio || nivel == platform::NivelTermico::Templado ? QStringLiteral("aviso")
                           : nivel == platform::NivelTermico::Normal                    ? QStringLiteral("normal")
                                                                                        : QStringLiteral("desconocido");
    termica_ = QVariantMap{{"nivel", QString::fromStdString(m.termica)}, {"texto", texto}, {"estado", estado}, {"apagados", nombres}};
    slis_.push_back(QVariantMap{{"clave", "termica"}, {"titulo", tr("Térmica")}, {"texto", texto}, {"estado", estado}});
}

} // namespace ba::app
