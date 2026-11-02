// test_capacidad_vm.cpp — el modelo de la pestaña Capacidad (ui/CapacidadViewModel) sin la app
//
// Con una sala, un generador, una máquina y una red de mentira (todo por Partes) y un reloj
// que avanza a mano: lo que impide arrancar (gente de verdad, algo al aire), la rampa de este
// equipo hasta 40 (lotes 10 + 10 + 20, el cupo que sube y vuelve, la credencial que se cierra,
// el historial), la degradación por CPU, la guardia por memoria, cancelar a mitad, el remoto
// (código malo, el cupo del otro recorta la rampa), recibir (el código se lee, la medida va al
// gateway, al terminar se limpia) y la conexión a internet por pinggy.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <string>
#include <vector>

#include "sala/CapacidadReglas.hpp"
#include "sala/GeneradorCarga.hpp"
#include "ui/CapacidadViewModel.hpp"

using namespace ba::app;
namespace cap = ba::app::sala::capacidad;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

/// Un generador que solo anota.
class GeneradorFalso : public sala::GeneradorCarga {
   public:
    std::vector<Lote> lotes;
    int paradas = 0;
    bool anda = false;
    bool disponible() const override { return true; }
    QString lanzar(const Lote& l) override {
        lotes.push_back(l);
        anda = true;
        return {};
    }
    void parar() override {
        ++paradas;
        anda = false;
    }
    std::vector<std::uint32_t> pids() const override { return anda ? std::vector<std::uint32_t>{4242} : std::vector<std::uint32_t>{}; }
    bool andando() const override { return anda; }
    bool listo = true;
    int preparaciones = 0;
    bool preparado() const override { return listo; }
    QString preparar() override {
        ++preparaciones;
        return {};
    }
    int total() const {
        int n = 0;
        for (const Lote& l : lotes) n += l.cantidad;
        return n;
    }
};

/// Todo lo de afuera, de mentira.
struct Mundo {
    int64_t reloj = 1700000000000LL;
    bool online = true, alAire = false;
    int reales = 0;
    int cupo = 10;
    std::vector<int> cupos;   ///< lo que se pidió cambiar
    std::vector<std::string> posts;
    std::vector<QJsonObject> medidas; ///< lo que se dejó para el probador remoto
    bool credencialAbierta = false;
    std::function<double(int n)> cpu = [](int n) { return 20.0 + n * 0.8; };
    std::function<double(int n)> mem = [](int) { return 9000.0; };
    double cpuGen = 5;
    GeneradorFalso* gen = nullptr;
    QVariantMap pinggy{{"activo", true}, {"via", "pinggy"}, {"estado", "listo"}, {"url", "https://x.a.pinggy.link"}};
    json remota = json{{"ok", true}, {"medida", {{"cupo", 20}, {"cpuMaquina", 30}, {"memLibreMb", 9000}, {"entraron", 0}, {"conVideo", 0}}}};

    QJsonObject foto() const {
        QJsonArray ps;
        const int prueba = gen ? gen->total() : 0;
        for (int i = 0; i < prueba && gen && gen->anda; ++i)
            ps.append(QJsonObject{{"id", QString::number(i)}, {"prueba", true}, {"connected", true}, {"mediaUp", true}});
        for (int i = 0; i < reales; ++i) ps.append(QJsonObject{{"id", "r" + QString::number(i)}, {"connected", true}});
        return QJsonObject{{"participants", ps}, {"maxGuests", cupo}};
    }
};

CapacidadViewModel::Partes partes(Mundo& w, const QString& dir) {
    CapacidadViewModel::Partes p;
    p.online = [&w]() { return w.online; };
    p.foto = [&w]() { return w.foto(); };
    p.post = [&w](const QString& ruta, const QJsonObject& c, CapacidadViewModel::Respuesta r) {
        w.posts.push_back(ruta.toStdString());
        QJsonObject resp{{"ok", true}};
        if (ruta == "/api/v1/prueba-sala" && c.contains("duracionS")) {
            w.credencialAbierta = true;
            resp["credencial"] = "0123456789abcdef0123456789abcdef";
        } else if (ruta == "/api/v1/prueba-sala" && c.contains("medida")) {
            w.medidas.push_back(c.value("medida").toObject());
        } else if (ruta == "/api/v1/prueba-sala/cerrar") {
            w.credencialAbierta = false;
        } else if (ruta == "/api/v1/capacidad/red") {
            resp["secreto"] = "s3cr3t0s3cr3t0s3cr3t0s3cr3t0abcd";
        }
        if (r) r(true, resp);
    };
    p.get = [](const QString& ruta, CapacidadViewModel::Respuesta r) {
        if (ruta == "/api/v1/network")
            r(true, QJsonObject{{"https", QJsonObject{{"enabled", true}, {"port", 8443}, {"fingerprint", "AB:CD"}}},
                                {"bestUrl", "http://192.168.1.20:8787"}});
        else
            r(false, {});
    };
    p.del = [&w](const QString& ruta) { w.posts.push_back("DELETE " + ruta.toStdString()); };
    p.cambiarCupo = [&w](int n) {
        w.cupos.push_back(n);
        w.cupo = n;
    };
    p.medirMaquina = [&w](const std::vector<std::uint32_t>& pids) {
        cap::Muestra m;
        const int n = w.gen && w.gen->anda ? w.gen->total() : 0;
        m.cpuGenerador = pids.empty() ? -1 : w.cpuGen;
        m.cpuMaquina = w.cpu(n) + (pids.empty() ? 0 : w.cpuGen);
        m.memLibreMb = w.mem(n);
        m.memTotalMb = 16000;
        return m;
    };
    p.medidaGuardia = []() {
        ba::app::sala::prueba::Medida m;
        m.memLibreMb = 12000;
        m.carga = 1;
        return m;
    };
    p.medirGrilla = [](int, std::function<void(double)> listo) { listo(29.5); };
    p.medirAudio = [](std::function<void(double)> listo) { listo(14); };
    p.alAire = [&w]() { return w.alAire; };
    p.otraPrueba = []() { return QString(); };
    p.hayTunel = []() { return false; };
    p.pinggy = [&w]() { return w.pinggy; };
    p.prenderPinggy = []() { return QString(); };
    p.apagarPinggy = []() {};
    p.juzgarEnlace = [](const ba::app::sala::enlace::Medicion& m, int) {
        return json{{"entranConMargen", static_cast<int>(m.subidaKbps / 2000)}};
    };
    p.pedirRemota = [&w](QObject*, const QString&, const QString&, const QString&, std::function<void(const json&)> listo) {
        listo(w.remota);
    };
    p.medirIdaYVuelta = [](QObject*, const QString&, const QString&, std::shared_ptr<std::atomic<bool>>,
                           std::function<void(const json&)> listo) {
        listo(json{{"ok", true}, {"kbps", 50000}, {"rttMs", 40}, {"rttCargaMs", 90}, {"perdida", 0.0}});
    };
    auto g = std::make_unique<GeneradorFalso>();
    w.gen = g.get();
    p.generador = std::move(g);
    p.dataRoot = dir.toStdString();
    p.maquina = "mac-de-prueba";
    p.reloj = [&w]() { return w.reloj; };
    p.tiempos.asentarS = 2;
    p.tiempos.regimenS = 4;
    return p;
}

/// Avanza el reloj de a un segundo y da el latido hasta que el modelo quede libre (o `tope` latidos).
int correr(CapacidadViewModel& vm, Mundo& w, int tope = 2000) {
    int i = 0;
    for (; i < tope && vm.ocupado(); ++i) {
        w.reloj += 1000;
        vm.tic();
    }
    return i;
}
bool hay(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    // --- Lo que impide arrancar -----------------------------------------------------------
    {
        Mundo w;
        w.reales = 2;
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(!vm.riesgo().isEmpty(), "con gente de verdad conectada hay riesgo");
        CHECK(!vm.probarEsteEquipo(40).isEmpty() && w.gen->lotes.empty(), "y no arranca: nadie entra");
        w.reales = 0;
        w.alAire = true;
        vm.revisar();
        CHECK(!vm.probarEsteEquipo(40).isEmpty(), "con algo al aire tampoco");
        w.alAire = false;
        vm.revisar();
        CHECK(vm.riesgo().isEmpty(), "sin nada de eso, no hay riesgo");
        CHECK(!vm.empezarARecibir(0, 10).isEmpty(), "recibir con cupo 0: no");
    }
    // --- Este equipo hasta 40, todo sano --------------------------------------------------
    {
        Mundo w;
        CapacidadViewModel vm(partes(w, tmp.path()));
        int avisosPasos = 0;
        QObject::connect(&vm, &CapacidadViewModel::pasosChanged, [&]() { ++avisosPasos; });
        CHECK(vm.probarEsteEquipo(40).isEmpty(), "arranca");
        CHECK(vm.estado() == "local", "estado local");
        CHECK(w.credencialAbierta, "abrió la credencial de prueba");
        CHECK((w.cupos == std::vector<int>{40}), "el cupo (10) sube a 40");
        CHECK(w.gen->lotes.size() == 1 && w.gen->lotes[0].cantidad == 10, "primer lote: 10");
        CHECK(w.gen->lotes[0].url == "https://127.0.0.1:8443", "contra el propio gateway por HTTPS");
        CHECK(w.gen->lotes[0].credencial.size() == 32, "con la credencial (al entorno, no a la línea de comandos)");
        const int tics = correr(vm, w);
        CHECK(!vm.ocupado(), "terminó sola");
        CHECK(tics < 3 * 120, "cada escalón dentro de sus 2 minutos");
        CHECK(w.gen->lotes.size() == 3 && w.gen->lotes[1].cantidad == 10 && w.gen->lotes[2].cantidad == 20, "lotes 10 + 10 + 20");
        CHECK(w.gen->paradas >= 1 && !w.gen->anda, "hinchada cortada");
        CHECK(!w.credencialAbierta, "credencial revocada");
        CHECK((w.cupos == std::vector<int>{40, 10}), "el cupo vuelve a 10");
        const QVariantMap r = vm.resultado();
        const QVariantMap res = r.value("resultado").toMap();
        CHECK(res.value("maximo").toInt() == 40 && res.value("alMenos").toBool(), "aguanta al menos 40");
        CHECK(!r.value("frase").toString().isEmpty(), "con la frase para el operador");
        CHECK(vm.pasos().size() == 3 && avisosPasos >= 3, "tres escalones, avisados");
        CHECK(vm.pasos()[0].toMap().value("cps").toDouble() == 29.5, "la grilla medida en el escalón");
        CHECK(vm.pasos()[0].toMap().value("cpuGenerador").toDouble() == 5, "la CPU de hinchada, aparte");
        CHECK(vm.historial().size() >= 1 && vm.historial()[0].toMap().value("tipo") == "local", "va al historial");
        CHECK(!vm.chequeoPrevio(80).isEmpty(), "el chequeo previo avisa si el evento tiene más lugar que lo que aguanta");
        CHECK(vm.ultimoDeEstaMaquina().is_object(), "el último de esta máquina");
    }
    // --- La primera vez: hinchada graba su video antes de la rampa ---------------------------
    {
        Mundo w;
        CapacidadViewModel vm(partes(w, tmp.path()));
        w.gen->listo = false;
        CHECK(vm.probarEsteEquipo(40).isEmpty(), "arranca aunque falte el video de prueba");
        CHECK(w.gen->preparaciones == 1 && w.gen->lotes.empty() && !w.credencialAbierta, "primero lo prepara: nadie entra todavía");
        CHECK(vm.progreso().value("fase").toString() == "preparando", "y lo dice");
        for (int i = 0; i < 30; ++i) {
            w.reloj += 1000;
            vm.tic();
        }
        CHECK(vm.pasos().isEmpty() && w.gen->lotes.empty(), "mientras prepara no se juzga nada");
        w.gen->listo = true;
        emit w.gen->preparacionTerminada(true, {});
        CHECK(w.gen->lotes.size() == 1 && w.credencialAbierta, "preparado: abre la prueba y entra el primer lote");
        vm.cancelar();
        CHECK(!vm.ocupado() && !w.credencialAbierta, "y se puede cancelar");
    }
    // --- Se degrada por CPU ---------------------------------------------------------------
    {
        Mundo w;
        w.cpu = [](int n) { return 10.0 + 2.0 * n; }; // 40 → 90 %
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(vm.probarEsteEquipo(80).isEmpty(), "arranca hasta 80");
        correr(vm, w);
        const QVariantMap res = vm.resultado().value("resultado").toMap();
        CHECK(res.value("maximo").toInt() == 20 && res.value("degradoEn").toInt() == 40, "con 40 se degrada: máximo 20");
        CHECK(res.value("limita").toString() == "cpu", "lo limita la CPU");
        CHECK(w.gen->lotes.size() == 3, "no siguió después de degradarse");
    }
    // --- La guardia (memoria) ----------------------------------------------------------------
    {
        Mundo w;
        w.mem = [](int n) { return n >= 20 ? 2500.0 : 9000.0; };
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(vm.probarEsteEquipo(80).isEmpty(), "arranca");
        correr(vm, w);
        const QVariantList pasos = vm.pasos();
        CHECK(pasos.size() == 2 && pasos[1].toMap().value("guardia").toBool(), "la guardia cortó el segundo escalón");
        CHECK(!w.credencialAbierta && !w.gen->anda, "y limpió todo");
    }
    // --- Cancelar a mitad ---------------------------------------------------------------------
    {
        Mundo w;
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(vm.probarEsteEquipo(80).isEmpty(), "arranca");
        for (int i = 0; i < 5; ++i) {
            w.reloj += 1000;
            vm.tic();
        }
        vm.cancelar();
        CHECK(!vm.ocupado() && !w.gen->anda && !w.credencialAbierta, "cancelar corta y limpia");
        CHECK((w.cupos == std::vector<int>{80, 10}), "y devuelve el cupo");
        CHECK(vm.resultado().value("resultado").toMap().value("cortada").toBool(), "el resultado dice que se cortó");
    }
    // --- Remoto -------------------------------------------------------------------------------
    {
        Mundo w;
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(!vm.probarRemoto("cualquier cosa", 40).isEmpty(), "un código que no es: no");
        cap::Codigo c;
        c.url = "https://192.168.1.20:8443";
        c.credencial = "0123456789abcdef0123456789abcdef";
        c.huella = "AB:CD";
        c.maquina = "pc-sala";
        CHECK(vm.probarRemoto(QString::fromStdString(cap::armarCodigo(c)), 80).isEmpty(), "con el código del otro, arranca");
        CHECK(vm.progreso().value("escalones").toList().size() == 2, "el otro abrió 20: la rampa es 10, 20");
        CHECK(w.gen->lotes.size() == 1 && w.gen->lotes[0].url == "https://192.168.1.20:8443", "los invitados van al otro");
        // El otro informa a los de prueba con video.
        w.remota["medida"]["conVideo"] = 10;
        w.remota["medida"]["entraron"] = 10;
        for (int i = 0; i < 30 && vm.ocupado(); ++i) {
            w.reloj += 1000;
            if (w.gen->lotes.size() == 2) {
                w.remota["medida"]["conVideo"] = 20;
                w.remota["medida"]["entraron"] = 20;
            }
            vm.tic();
        }
        correr(vm, w);
        CHECK(!w.credencialAbierta, "el remoto no toca la credencial de esta máquina");
        CHECK(vm.resultado().value("tipo").toString() == "remoto" && vm.resultado().value("maquina").toString() == "pc-sala",
              "el resultado es del otro equipo");
    }
    // --- Recibir -------------------------------------------------------------------------------
    {
        Mundo w;
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(vm.empezarARecibir(60, 10).isEmpty(), "empieza a recibir");
        CHECK(vm.estado() == "recibiendo" && w.credencialAbierta, "con la credencial abierta");
        CHECK((w.cupos == std::vector<int>{60}), "el cupo sube a 60");
        cap::Codigo c;
        CHECK(cap::leerCodigo(vm.recibir().value("codigo").toString().toStdString(), c), "el código se lee");
        CHECK(c.url == "https://192.168.1.20:8443" && c.huella == "AB:CD" && c.credencial.size() == 32, "con la dirección, la huella y la credencial");
        CHECK(!vm.recibir().value("codigoInternet").toString().isEmpty(), "y otro por pinggy");
        CHECK(!vm.estadoJson(false).value("recibir", json::object()).contains("codigo"), "el estado sin código no lo trae");
        for (int i = 0; i < 3; ++i) {
            w.reloj += 1000;
            vm.tic();
        }
        CHECK(w.medidas.size() == 3 && w.medidas.back().value("cupo").toInt() == 60, "cada segundo deja la medida para el otro");
        vm.dejarDeRecibir();
        CHECK(!vm.ocupado() && !w.credencialAbierta && w.cupo == 10, "al terminar cierra y devuelve el cupo");
    }
    // --- Internet --------------------------------------------------------------------------------
    {
        Mundo w;
        CapacidadViewModel vm(partes(w, tmp.path()));
        CHECK(vm.medirInternet(false).isEmpty(), "mide la conexión");
        CHECK(!vm.ocupado(), "con todo de mentira termina enseguida");
        CHECK(vm.internet().value("invitados").toInt() == 25, "50 Mbps ida y vuelta → 25 (la cuenta del enlace)");
        CHECK(hay(w.posts, "DELETE /api/v1/capacidad/red"), "el secreto de medir se olvida al terminar");
        w.pinggy = QVariantMap{{"activo", false}, {"estado", "apagado"}};
        CHECK(vm.medirInternet(false).isEmpty(), "con pinggy apagado y sin pedirlo…");
        CHECK(!vm.internet().value("pinggy").toMap().value("nota").toString().isEmpty(), "…dice cómo medirlo");
    }
    std::printf(g_fallas == 0 ? "\nTODO OK\n" : "\n%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
