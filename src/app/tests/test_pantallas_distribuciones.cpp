// test_pantallas_distribuciones.cpp
//
// Las distribuciones de pantallas (pantallas/Distribuciones), sin interfaz ni
// monitores de verdad: el catálogo con fuentes inventadas, los monitores de prueba
// y las ventanas sueltas en un VentanasModel sin archivo.
//   1. Las cuentas: relativa ↔ absoluta (ida y vuelta exacta), píxeles fijados que
//      entran y que no, a qué monitor va una ventana (el suyo, o el que haya).
//   2. Crear valida como lo haría el MCP (cosas del catálogo, lugar 0..1, monitor
//      conocido, nombre único) y guarda el monitor por su id estable.
//   3. Resolver arma UNA ventana madre por monitor, con las piezas relativas a ella.
//   4. Aplicar es idempotente: la segunda vez no avisa nada; y cierra las sueltas.
//   5. Un monitor que falta: avisa, va al que haya, y vuelve solo cuando aparece.
//   6. Capturar → aplicar deja lo mismo (lienzos y sueltas).
//   7. Se guarda y vuelve (con la aplicada); plantillas; mover en el esquema;
//      editar en vivo la aplicada; la salida de emergencia.

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cstdio>

#include "pantallas/Catalogo.hpp"
#include "pantallas/Distribuciones.hpp"
#include "pantallas/Ventanas.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;
using ba::app::CatalogoVisible;
using ba::app::Distribucion;
using ba::app::DistribucionesModel;
using ba::app::Lienzo;
using ba::app::LienzosModel;
using ba::app::MonitorVisto;
using ba::app::VentanaDistribucion;
using ba::app::VentanasModel;
namespace dist = ba::app::distribucion;

namespace {
int g_fallas = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_fallas;                                                                                      \
        }                                                                                                    \
    } while (0)

CatalogoVisible::Fuentes fuentes() {
    CatalogoVisible::Fuentes f;
    f.escenas = []() { return QStringList{"Remotos A", "Remotos B", "Atril"}; };
    f.multiviews = []() { return QVariantList{QVariantMap{{"id", "principal"}, {"nombre", "Control"}}}; };
    f.fondo = []() { return QStringList{"program"}; };
    f.fondoDistribucion = []() { return QString("grid"); };
    f.videos = []() { return QVariantList{QVariantMap{{"clave", "guest:1"}, {"titulo", "Ana"}}}; };
    f.paginas = []() { return QVariantList{QVariantMap{{"page", 10}, {"id", "guion"}, {"titulo", "Evento"}, {"icon", "≣"}}}; };
    f.contenidosRed = []() { return QVariantList{QVariantMap{{"id", "mensaje"}, {"titulo", "Un mensaje"}}}; };
    return f;
}

/// A a la izquierda (principal, 1920×1080); B a la derecha (2560×1440).
const MonitorVisto A{"Dell|U1|SA", QRect(0, 0, 1920, 1080), true, 1, "DELL U1"};
const MonitorVisto B{"LG|27|SB", QRect(1920, 0, 2560, 1440), false, 2, "LG 27"};

/// Lo que diría el MCP: el JSON tal cual, pasado a QVariant como lo pasa jsonToQv.
QVariantList ventanasJson(const char* json) { return QJsonDocument::fromJson(json).toVariant().toList(); }

struct Avisos {
    int altas = 0, bajas = 0, cambios = 0, reset = 0;
    void escuchar(LienzosModel& l) {
        QObject::connect(&l, &QAbstractItemModel::rowsInserted, [this] { ++altas; });
        QObject::connect(&l, &QAbstractItemModel::rowsRemoved, [this] { ++bajas; });
        QObject::connect(&l, &QAbstractItemModel::dataChanged, [this] { ++cambios; });
        QObject::connect(&l, &QAbstractItemModel::modelReset, [this] { ++reset; });
    }
    int total() const { return altas + bajas + cambios + reset; }
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ===================== 1. Las cuentas =====================================
    {
        const QRectF mitad(0, 0, 0.5, 1);
        CHECK(dist::aAbsoluta(mitad, B.geometria) == QRect(1920, 0, 1280, 1440));
        // Ida y vuelta exacta (lo que hace que capturar → aplicar deje lo mismo).
        const QRect g(2100, 37, 913, 611);
        CHECK(dist::aAbsoluta(dist::aRelativa(g, B.geometria), B.geometria) == g);
        // La misma mitad en otra resolución sigue siendo la mitad.
        CHECK(dist::aAbsoluta(mitad, A.geometria) == QRect(0, 0, 960, 1080));
        // Píxeles fijados: si entran se usan (corridos para quedar adentro)…
        VentanaDistribucion v;
        v.relativa = QRectF(0.5, 0.5, 0.5, 0.5);
        v.absoluta = QRect(1800, 1000, 640, 360);
        CHECK(dist::geometriaEn(v, B.geometria) == QRect(1920 + 1800, 1000, 640, 360));
        CHECK(dist::geometriaEn(v, A.geometria) == QRect(1920 - 640, 1080 - 360, 640, 360));
        // …y si no entran en el monitor, la relativa.
        v.absoluta = QRect(0, 0, 2000, 1200);
        CHECK(dist::geometriaEn(v, A.geometria) == QRect(960, 540, 960, 540));
        // A qué monitor va.
        bool falta = true;
        v.monitor = A.id;
        CHECK(dist::monitorPara(v, {A, B}, &falta) == 0 && !falta);
        v.monitor = "no|esta|x";
        CHECK(dist::monitorPara(v, {A, B}, &falta) == 1 && falta); // el primero que no es el principal
        CHECK(dist::monitorPara(v, {A}, &falta) == 0 && falta);    // si no, el principal
        v.monitor.clear();
        CHECK(dist::monitorPara(v, {A, B}, &falta) == 1 && !falta); // sin monitor: el que no es el principal
        CHECK(dist::monitorPara(v, {}, &falta) == -1);
        // El lugar va de 0 a 1.
        VentanaDistribucion w;
        const auto tal = [](const QString& q) { return q; };
        CHECK(!dist::ventanaDeMapa(QVariantMap{{"x", 0.6}, {"ancho", 0.5}}, w, tal).isEmpty());
        CHECK(dist::ventanaDeMapa(QVariantMap{{"x", 0.5}, {"ancho", 0.5}, {"disposicion", "pip"}}, w, tal).isEmpty());
        CHECK(w.relativa == QRectF(0.5, 0, 0.5, 1) && w.disposicion == "pip");
        CHECK(!dist::ventanaDeMapa(QVariantMap{{"disposicion", "mosaico"}}, w, tal).isEmpty());
        CHECK(!dist::ventanaDeMapa(QVariantMap{{"absoluta", QVariantMap{{"x", 0}, {"y", 0}, {"ancho", 0}, {"alto", 10}}}}, w, tal)
                   .isEmpty());
    }

    CatalogoVisible cat(fuentes());
    cat.refrescar();
    QTemporaryDir dir;
    const std::filesystem::path archivo = dir.filePath("Config/distribuciones-pantallas.json").toStdString();
    VentanasModel sueltas({}, &cat, nullptr);
    QString remotos;
    {
        DistribucionesModel d(archivo, &cat, nullptr, &sueltas);
        d.setMonitoresDePrueba({A, B});
        Avisos av;
        av.escuchar(d.lienzos());

        // ===================== 2. Crear (como el MCP) =========================
        const QVariantList lado = ventanasJson(R"([
            {"nombre": "Remotos A", "cosas": ["escena:Remotos A"], "monitor": "2", "x": 0, "y": 0, "ancho": 0.5, "alto": 1},
            {"nombre": "Remotos B", "cosas": "escena:Remotos B", "monitor": "LG 27", "x": 0.5, "y": 0, "ancho": 0.5, "alto": 1}])");
        QString err = errorDe([&] { remotos = d.crear("Remotos lado a lado", lado); });
        CHECK(err.isEmpty() && !remotos.isEmpty());
        const Distribucion* r = d.buscar("remotos LADO a lado"); // por nombre, sin mayúsculas
        CHECK(r != nullptr && r->ventanas.size() == 2);
        CHECK(r->ventanas[0].monitor == B.id && r->ventanas[1].monitor == B.id); // por orden y por nombre → id
        CHECK(r->ventanas[1].cosas == QStringList{"escena:Remotos B"});
        // Lo que no va: nombre repetido, una cosa que no existe, una que solo va a la TV,
        // un monitor que no se conoce, un lugar fuera de 0..1, sin nombre.
        QString id;
        CHECK(!errorDe([&] { id = d.crear("REMOTOS lado a lado", {}); }).isEmpty() && id.isEmpty());
        CHECK(!errorDe([&] { d.crear("X", ventanasJson(R"([{"cosas": ["no-existe"]}])")); }).isEmpty());
        CHECK(!errorDe([&] { d.crear("X", ventanasJson(R"([{"cosas": ["web:mensaje"]}])")); }).isEmpty());
        CHECK(!errorDe([&] { d.crear("X", ventanasJson(R"([{"cosas": ["programa"], "monitor": "9"}])")); }).isEmpty());
        CHECK(!errorDe([&] { d.crear("X", ventanasJson(R"([{"cosas": ["programa"], "x": 0.8, "ancho": 0.5}])")); }).isEmpty());
        CHECK(!errorDe([&] { d.crear("  ", {}); }).isEmpty());
        CHECK(d.rowCount() == 1);
        // Lo que ve el editor: dónde iría cada una, en el esquema de todos los monitores.
        const QVariantList vs = d.porId().value(remotos).toMap().value("ventanas").toList();
        CHECK(vs.size() == 2 && vs[0].toMap().value("enMonitor").toString() == B.id &&
              !vs[0].toMap().value("faltaMonitor").toBool());
        CHECK(vs[0].toMap().value("titulo").toString() == "Remotos A");

        // ===================== 3. Una ventana madre por monitor ===============
        const std::vector<Lienzo> ls = d.resolver(*r);
        CHECK(ls.size() == 1);
        CHECK(ls[0].id == B.id && ls[0].geometria == B.geometria && ls[0].pantallaCompleta);
        CHECK(ls[0].piezas.size() == 2 && ls[0].piezas[0].rect == QRectF(0, 0, 0.5, 1) &&
              ls[0].piezas[1].rect == QRectF(0.5, 0, 0.5, 1));

        // ===================== 4. Aplicar, idempotente ========================
        CHECK(sueltas.abrirCon({"padron"}, {}) > 0);
        CHECK(errorDe([&] { d.aplicar(remotos); }).isEmpty());
        CHECK(d.aplicada() == remotos && d.aviso().isEmpty());
        CHECK(d.lienzos().rowCount() == 1);
        CHECK(sueltas.rowCount() == 0); // EXACTAMENTE lo suyo
        const int antes = av.total();
        CHECK(errorDe([&] { d.aplicar(remotos); }).isEmpty());
        CHECK(av.total() == antes); // la segunda vez no avisa nada (no se recrea ninguna ventana)
        CHECK(d.data(d.index(0), DistribucionesModel::AplicadaRole).toBool());

        // Editar la aplicada se ve en el acto, y la ventana madre del monitor es la misma.
        const int altas = av.altas;
        CHECK(d.setVentana(remotos, 1, {{"x", 0.5}, {"y", 0.5}, {"ancho", 0.5}, {"alto", 0.5}}));
        CHECK(av.altas == altas && d.lienzos().rowCount() == 1);
        CHECK(d.lienzos().lienzos()[0].piezas[1].rect == QRectF(0.5, 0.5, 0.5, 0.5));

        // ===================== 5. Un monitor que falta ========================
        // Se desenchufa B: lo aplicado se vuelve a ubicar solo (sin que nadie lo pida).
        d.setMonitoresDePrueba({A});
        CHECK(d.lienzos().rowCount() == 1 && d.lienzos().lienzos()[0].id == A.id);
        CHECK(!d.aviso().isEmpty());
        CHECK(d.porId().value(remotos).toMap().value("ventanas").toList()[0].toMap().value("faltaMonitor").toBool());
        // Vuelve B: sus ventanas vuelven a B.
        d.setMonitoresDePrueba({A, B});
        CHECK(d.lienzos().rowCount() == 1 && d.lienzos().lienzos()[0].id == B.id && d.aviso().isEmpty());

        // ===================== 6. Capturar → aplicar = igual ==================
        CHECK(sueltas.abrirEn({"multiview:principal"}, "grid", A.id, QRect(100, 120, 800, 450), false) > 0);
        const std::vector<Lienzo> puestos = d.lienzos().lienzos();
        QString copia;
        CHECK(errorDe([&] { copia = d.capturar("Copia"); }).isEmpty() && !copia.isEmpty());
        CHECK(d.buscar(copia)->ventanas.size() == 3);
        CHECK(errorDe([&] { d.aplicar(copia); }).isEmpty());
        CHECK(sueltas.rowCount() == 0);
        const std::vector<Lienzo>& ahora = d.lienzos().lienzos();
        CHECK(ahora.size() == 2);
        bool igualB = false, sueltaEnA = false;
        for (const Lienzo& l : ahora) {
            if (l.id == B.id) igualB = l == puestos[0];
            if (l.id == A.id) sueltaEnA = l.geometria == QRect(100, 120, 800, 450) && !l.pantallaCompleta &&
                                          l.piezas.size() == 1 && l.piezas[0].rect == QRectF(0, 0, 1, 1);
        }
        CHECK(igualB);
        CHECK(sueltaEnA);
        // Y capturar lo que dejó la copia y volver a aplicarla no cambia nada.
        const int antes2 = av.total();
        CHECK(errorDe([&] { d.capturar("", copia); }).isEmpty());
        CHECK(errorDe([&] { d.aplicar(copia); }).isEmpty());
        CHECK(av.total() == antes2);

        // ===================== 7. Plantillas, esquema, alAbrir, emergencia ====
        QString mosaico;
        CHECK(errorDe([&] { mosaico = d.crearDePlantilla("mosaico-2x2", ""); }).isEmpty());
        const Distribucion* m = d.buscar(mosaico);
        CHECK(m != nullptr && m->nombre == "Mosaico 2×2" && m->ventanas.size() == 4);
        CHECK(m->ventanas[0].cosas == QStringList{"programa"} && m->ventanas[3].relativa == QRectF(0.5, 0.5, 0.5, 0.5));
        CHECK(m->ventanas[0].monitor == B.id); // el que no es el principal
        QString una;
        CHECK(errorDe([&] { una = d.crearDePlantilla("una-por-monitor", "", {}, {"programa"}); }).isEmpty());
        CHECK(d.buscar(una)->ventanas.size() == 1 && d.buscar(una)->ventanas[0].pantallaCompleta);
        CHECK(!errorDe([&] { d.crearDePlantilla("no-existe", "Z"); }).isEmpty());
        // Arrastrar en el esquema (0..1 del rectángulo de todos los monitores) a la izquierda: pasa a A.
        CHECK(d.moverEnDibujo(mosaico, 0, 0.0, 0.0, 0.2, 0.3));
        CHECK(d.buscar(mosaico)->ventanas[0].monitor == A.id);
        // Una sola se abre al cargar el evento.
        CHECK(d.setAlAbrir(mosaico, true));
        CHECK(d.setAlAbrir(remotos, true));
        CHECK(!d.buscar(mosaico)->alAbrir && d.buscar(remotos)->alAbrir);
        // Duplicar, renombrar.
        QString dup;
        CHECK(errorDe([&] { dup = d.duplicar(remotos); }).isEmpty() && !d.buscar(dup)->alAbrir);
        CHECK(d.renombrar(dup, "Otra"));
        CHECK(!d.renombrar(dup, "Copia"));
        CHECK(d.borrar(dup) && d.buscar(dup) == nullptr);
        // La salida de emergencia: se van las ventanas madre, la distribución queda.
        CHECK(d.cerrarVentanas() == 2);
        CHECK(d.lienzos().rowCount() == 0 && d.aplicada().isEmpty() && d.buscar(copia) != nullptr);
        CHECK(errorDe([&] { d.aplicar(copia); }).isEmpty());
    }
    {
        // Se guarda y vuelve: las distribuciones y la aplicada (al cargar el evento vuelve
        // la marcada alAbrir; si no hubiera, la que estaba).
        VentanasModel s2({}, &cat, nullptr);
        DistribucionesModel d(archivo, &cat, nullptr, &s2);
        d.setMonitoresDePrueba({A, B});
        CHECK(d.rowCount() == 4);
        CHECK(d.buscar(remotos) != nullptr && d.buscar(remotos)->alAbrir);
        CHECK(d.buscar(remotos)->ventanas[1].relativa == QRectF(0.5, 0.5, 0.5, 0.5));
        CHECK(d.aplicada() == d.buscar("Copia")->id);
        d.aplicarAlAbrir();
        CHECK(d.aplicada() == remotos && d.lienzos().rowCount() == 1);
        CHECK(d.setAlAbrir(remotos, false));
        DistribucionesModel d3(archivo, &cat, nullptr, &s2);
        d3.setMonitoresDePrueba({A, B});
        d3.aplicarAlAbrir();
        CHECK(d3.aplicada() == remotos && d3.lienzos().rowCount() == 1);
    }

    if (g_fallas == 0) std::printf("test_pantallas_distribuciones: OK\n");
    return g_fallas == 0 ? 0 : 1;
}
