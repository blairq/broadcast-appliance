// test_pantallas_catalogo.cpp
//
// El catálogo de cosas que se pueden mostrar (pantallas/Catalogo) y los
// monitores de la máquina (pantallas/Monitores), sin interfaz ni monitores de
// verdad: las fuentes del catálogo son funciones y las pantallas se inventan.
//   1. El catálogo trae lo fijo (programa, paneles, tarjetas) y lo que le dan
//      las fuentes (escenas, multivistas, cámaras, páginas, contenidos de la TV),
//      cada cosa con su id estable, cómo se dibuja y si viaja a una TV.
//   2. Al refrescar, las filas se actualizan EN SU LUGAR (nunca un reset).
//   3. Los monitores salen en su orden FÍSICO (filas de arriba abajo, cada fila
//      de izquierda a derecha) y con un id que no es el índice.
//   4. Asignar valida contra el catálogo; lo asignado se guarda y vuelve.
//   5. Un monitor que no está se ESPERA: su contenido no pasa a otro, y al
//      volver aparece solo.
//   6. Las ventanas sueltas (pantallas/Ventanas): las mismas cosas del catálogo
//      y los mismos ids de monitor; se recuerdan; lo que había en ui.json (las
//      ventanas de video viejas) se migra sin perderse.

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <deque>

#include "pantallas/Catalogo.hpp"
#include "pantallas/Monitores.hpp"
#include "pantallas/Ventanas.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using ba::app::CatalogoVisible;
using ba::app::VentanasModel;
using ba::app::MonitoresModel;
using ba::app::PantallaFisica;

namespace {
int g_fallas = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_fallas;                                                                                      \
        }                                                                                                    \
    } while (0)

/// Lo que «hay» en el aparato (lo cambian las pruebas).
QStringList g_escenas{"Plano general", "Atril"};
QStringList g_camaras;
QStringList g_fondo{"program"};
QString g_fondoDist{"grid"};

CatalogoVisible::Fuentes fuentes() {
    CatalogoVisible::Fuentes f;
    f.escenas = []() { return g_escenas; };
    f.multiviews = []() { return QVariantList{QVariantMap{{"id", "principal"}, {"nombre", "Control"}}}; };
    f.fondo = []() { return g_fondo; };
    f.fondoDistribucion = []() { return g_fondoDist; };
    f.videos = []() {
        return QVariantList{QVariantMap{{"clave", "program"}, {"titulo", "Programa"}},
                            QVariantMap{{"clave", "guest:1"}, {"titulo", "Ana"}},
                            QVariantMap{{"clave", "entrada:Cámara 1"}, {"titulo", "Cámara 1"}}};
    };
    f.camarasPtz = []() { return g_camaras; };
    f.paginas = []() {
        return QVariantList{QVariantMap{{"page", 10}, {"id", "guion"}, {"titulo", "Evento"}, {"icon", "≣"}},
                            QVariantMap{{"page", 15}, {"id", "prompter"}, {"titulo", "Teleprompter"}, {"icon", "▤"}}};
    };
    f.contenidosRed = []() {
        return QVariantList{QVariantMap{{"id", "mensaje"}, {"titulo", "Un mensaje"}},
                            QVariantMap{{"id", "qr"}, {"titulo", "Un código QR"}},
                            QVariantMap{{"id", "programa"}, {"titulo", "Lo que sale al aire"}},
                            QVariantMap{{"id", "escena"}, {"titulo", "Una escena"}},
                            QVariantMap{{"id", "prompter"}, {"titulo", "El teleprompter"}},
                            QVariantMap{{"id", "votacion"}, {"titulo", "La votación"}}};
    };
    return f;
}

/// Cuenta cuántas veces sale una señal (sin QtTest, que no viaja en el bundle).
struct Contador {
    int n = 0;
    int count() const { return n; }
};
std::deque<Contador> g_contadores; // viven hasta el final (las conexiones los usan)
template <typename Obj, typename Senal>
Contador* contar(Obj* o, Senal s) {
    Contador* c = &g_contadores.emplace_back();
    QObject::connect(o, s, o, [c]() { ++c->n; });
    return c;
}

int filaDe(const QAbstractItemModel& m, const QString& id, int rolId) {
    for (int r = 0; r < m.rowCount(); ++r)
        if (m.data(m.index(r, 0), rolId).toString() == id) return r;
    return -1;
}

PantallaFisica pantalla(const QString& nombre, const QString& serie, int x, int y, int w = 1920, int h = 1080,
                        bool principal = false) {
    return PantallaFisica{nombre, "Dell Inc.", "DELL U2720Q", serie, QRect(x, y, w, h), principal};
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ===================== 1. El catálogo ===================================
    CatalogoVisible cat(fuentes());
    {
        const auto* prog = cat.buscar("programa");
        CHECK(prog != nullptr);
        CHECK(prog && prog->dibujo.value("modo").toString() == "video");
        CHECK(prog && prog->redTipo == "programa");
        CHECK(cat.existe("preview"));
        CHECK(cat.existe("fondo"));
        // Escenas: una por escena, se dibujan y viajan a la TV con su nombre.
        const auto* esc = cat.buscar("escena:Atril");
        CHECK(esc != nullptr);
        CHECK(esc && esc->dibujo.value("claves").toStringList() == QStringList{"escena:Atril"});
        CHECK(esc && esc->redTipo == "escena" && esc->redDatos.value("escena").toString() == "Atril");
        CHECK(cat.existe("multiview:principal"));
        // Paneles y tarjetas: el Mezclador es UN panel (no se repite como tarjeta).
        CHECK(cat.existe("padron") && cat.existe("preguntas") && cat.existe("votacion"));
        CHECK(cat.existe("mezclador") && !cat.existe("tarjeta:mixer"));
        CHECK(cat.existe("tarjeta:aire") && cat.existe("tarjeta:direccion"));
        CHECK(cat.ventanaDeTarjeta("mixer") == "mezclador");
        CHECK(cat.ventanaDeTarjeta("aire") == "tarjeta:aire");
        CHECK(cat.ventanaDeTarjeta("ptz").isEmpty()); // sin cámaras no hay PTZ para abrir
        // Páginas: el teleprompter también viaja a la TV.
        const auto* pr = cat.buscar("pagina:prompter");
        CHECK(pr && pr->dibujo.value("pagina").toInt() == 15 && pr->redTipo == "prompter");
        CHECK(cat.buscar("pagina:guion") && cat.buscar("pagina:guion")->redTipo.isEmpty());
        // Lo que solo existe en la TV aparece como «web:»; lo ya cubierto, no.
        CHECK(cat.existe("web:mensaje") && cat.existe("web:qr"));
        CHECK(!cat.existe("web:programa") && !cat.existe("web:escena") && !cat.existe("web:prompter") &&
              !cat.existe("web:votacion"));
        CHECK(cat.buscar("web:mensaje")->dibujo.isEmpty()); // no se dibuja en un monitor
        // Las dos vistas filtradas.
        auto* locales = qobject_cast<QAbstractItemModel*>(cat.locales());
        auto* red = qobject_cast<QAbstractItemModel*>(cat.deRed());
        CHECK(filaDe(*locales, "tarjeta:aire", CatalogoVisible::IdRole) >= 0);
        CHECK(filaDe(*locales, "web:mensaje", CatalogoVisible::IdRole) < 0);
        CHECK(filaDe(*red, "web:mensaje", CatalogoVisible::IdRole) >= 0);
        CHECK(filaDe(*red, "escena:Atril", CatalogoVisible::IdRole) >= 0);
        CHECK(filaDe(*red, "tarjeta:aire", CatalogoVisible::IdRole) < 0);
        // Para enlazarse desde QML.
        CHECK(cat.porId().contains("escena:Plano general"));
        CHECK(cat.porId().value("programa").toMap().value("local").toBool());
        CHECK(cat.tituloDe("escena:Atril") == "Escena — Atril");
        CHECK(cat.tituloDe("escena:Borrada").contains("ya no existe"));
    }

    // ===================== 2. Refrescar en su lugar ==========================
    {
        Contador& reset = *contar(&cat, &QAbstractItemModel::modelReset);
        Contador& altas = *contar(&cat, &QAbstractItemModel::rowsInserted);
        Contador& bajas = *contar(&cat, &QAbstractItemModel::rowsRemoved);
        Contador& cambios = *contar(&cat, &QAbstractItemModel::dataChanged);
        Contador& aviso = *contar(&cat, &CatalogoVisible::cambio);
        const int antes = cat.rowCount();
        cat.refrescar(); // nada cambió: nada avisa
        CHECK(aviso.count() == 0 && altas.count() == 0 && bajas.count() == 0 && cambios.count() == 0);

        g_escenas = {"Plano general", "Recinto"}; // se borró «Atril», apareció «Recinto»
        g_camaras = {"Cam 1"};
        g_fondo = {"program", "guest:1"};
        cat.refrescar();
        CHECK(reset.count() == 0);
        CHECK(!cat.existe("escena:Atril") && cat.existe("escena:Recinto"));
        CHECK(cat.existe("ptz:Cam 1") && cat.ventanaDeTarjeta("ptz") == "ptz:Cam 1");
        CHECK(cat.buscar("fondo")->dibujo.value("claves").toStringList().size() == 2);
        CHECK(bajas.count() == 1);  // Atril
        CHECK(altas.count() == 2);  // Recinto y la PTZ
        CHECK(cambios.count() >= 1); // el fondo
        CHECK(aviso.count() == 1);
        CHECK(cat.rowCount() == antes + 1);
        // El orden es el que dicen las fuentes, después de refrescar también.
        CHECK(filaDe(cat, "escena:Plano general", CatalogoVisible::IdRole) <
              filaDe(cat, "escena:Recinto", CatalogoVisible::IdRole));
        g_escenas = {"Recinto", "Plano general"}; // el operador las reordenó
        cat.refrescar();
        CHECK(reset.count() == 0);
        CHECK(filaDe(cat, "escena:Recinto", CatalogoVisible::IdRole) <
              filaDe(cat, "escena:Plano general", CatalogoVisible::IdRole));
    }

    // ===================== 3. Monitores: orden físico e ids ===================
    QTemporaryDir dir;
    const std::filesystem::path archivo = dir.filePath("Config/monitores.json").toStdString();
    // Llegan desordenados (como los da el sistema): B a la izquierda, A en el
    // medio, C a la derecha un poco más arriba (misma fila), D abajo de B.
    const PantallaFisica A = pantalla("DP-2", "SERIE-A", 1920, 0, 1920, 1080, true);
    const PantallaFisica B = pantalla("DP-1", "SERIE-B", 0, 0);
    const PantallaFisica C = pantalla("HDMI-1", "SERIE-C", 3840, -200, 1920, 1200);
    const PantallaFisica D = pantalla("DP-3", "SERIE-D", 0, 1080);
    const QString idA = MonitoresModel::idDe(A), idB = MonitoresModel::idDe(B), idC = MonitoresModel::idDe(C),
                  idD = MonitoresModel::idDe(D);
    {
        CHECK(idA == "Dell Inc.|DELL U2720Q|SERIE-A");
        // Sin serie, el nombre del sistema desempata.
        CHECK(MonitoresModel::idDe(pantalla("DP-9", "", 0, 0)) == "Dell Inc.|DELL U2720Q|@DP-9");
        CHECK(MonitoresModel::idDe(PantallaFisica{"eDP-1", "", "", "", QRect(0, 0, 10, 10), false}) == "@eDP-1");

        MonitoresModel m(archivo, &cat);
        m.actualizarPantallas({A, D, C, B});
        CHECK(m.rowCount() == 4);
        CHECK(m.conectados() == 4);
        const auto orden = [&](const QString& id) {
            return m.data(m.index(filaDe(m, id, MonitoresModel::IdRole)), MonitoresModel::OrdenRole).toInt();
        };
        CHECK(orden(idB) == 1 && orden(idA) == 2 && orden(idC) == 3 && orden(idD) == 4);
        CHECK(m.resolver("2") == idA);
        CHECK(m.resolver("DP-1") == idB);
        CHECK(m.idDePantalla("DP-2", "Dell Inc.", "DELL U2720Q", "SERIE-A") == idA);
        // El dibujo: B arriba a la izquierda, dentro del rectángulo de todos.
        const QRectF dB = m.data(m.index(filaDe(m, idB, MonitoresModel::IdRole)), MonitoresModel::DibujoRole).toRectF();
        CHECK(dB.x() == 0.0 && dB.width() > 0.0 && dB.width() < 0.5);
        CHECK(m.proporcion() > 1.0);
        // Dos iguales sin serie y sin nombre: el segundo (en el orden físico) es «#2».
        MonitoresModel gemelos({}, nullptr);
        const PantallaFisica g1{"", "Acme", "X1", "", QRect(0, 0, 100, 100), false};
        const PantallaFisica g2{"", "Acme", "X1", "", QRect(100, 0, 100, 100), false};
        gemelos.actualizarPantallas({g2, g1});
        CHECK(gemelos.rowCount() == 2);
        CHECK(gemelos.data(gemelos.index(0), MonitoresModel::IdRole).toString() == "Acme|X1|@");
        CHECK(gemelos.data(gemelos.index(1), MonitoresModel::IdRole).toString() == "Acme|X1|@#2");

        // ===================== 4. Asignar y validar ===========================
        auto* activos = qobject_cast<QAbstractItemModel*>(m.activos());
        CHECK(activos->rowCount() == 0);
        CHECK(m.asignar(idC, {"programa"}, true).isEmpty());
        CHECK(m.asignar(idD, {"multiview:principal", "preguntas", "preguntas"}, false).isEmpty());
        CHECK(m.monitor(idD).value("cosas").toStringList() == (QStringList{"multiview:principal", "preguntas"}));
        CHECK(!m.asignar(idA, {"no-existe"}, true).isEmpty());
        CHECK(!m.asignar(idA, {"web:mensaje"}, true).isEmpty()); // solo TV
        CHECK(!m.asignar(idA, {}, true).isEmpty());
        CHECK(!m.asignar("monitor-inventado", {"programa"}, true).isEmpty());
        QStringList muchas;
        for (const char* c : {"programa", "preview", "fondo", "padron", "preguntas", "votacion", "mezclador",
                              "tarjeta:aire", "tarjeta:health", "pagina:guion"})
            muchas << c;
        CHECK(!m.asignar(idA, muchas, true).isEmpty()); // más de 9 no se leen
        CHECK(activos->rowCount() == 2);
        CHECK(m.monitor(idC).value("titulo").toString() == "Programa (lo que sale al aire)");
        CHECK(m.monitor(idD).value("titulo").toString().contains(" + "));
        CHECK(m.setPantallaCompleta(idD, true).isEmpty());
        CHECK(m.quitar(idD).isEmpty());
        CHECK(activos->rowCount() == 1);
        CHECK(m.asignar(idD, {"escena:Recinto"}, false).isEmpty());
    }

    // ===================== 5. Persistencia y espera ==========================
    {
        MonitoresModel m(archivo, &cat);
        // Antes de que el sistema diga qué hay: lo recordado, esperando.
        CHECK(m.rowCount() == 2);
        CHECK(m.esperando() == 2);
        CHECK(qobject_cast<QAbstractItemModel*>(m.activos())->rowCount() == 0);

        // Arranca con C apagado (como después de un corte de luz).
        Contador& reset = *contar(&m, &QAbstractItemModel::modelReset);
        m.actualizarPantallas({A, B, D});
        auto* activos = qobject_cast<QAbstractItemModel*>(m.activos());
        CHECK(m.conectados() == 3);
        CHECK(m.esperando() == 1);
        const QVariantMap c = m.monitor(idC);
        CHECK(!c.value("conectado").toBool() && c.value("esperando").toBool());
        CHECK(c.value("cosas").toStringList() == QStringList{"programa"});
        CHECK(!c.value("activa").toBool());
        // Su contenido no se mudó a otro: A y B siguen sin nada.
        CHECK(m.monitor(idA).value("cosas").toStringList().isEmpty());
        CHECK(m.monitor(idB).value("cosas").toStringList().isEmpty());
        // D volvió con lo suyo (y su pantalla completa).
        CHECK(m.monitor(idD).value("cosas").toStringList() == QStringList{"escena:Recinto"});
        CHECK(!m.monitor(idD).value("pantallaCompleta").toBool());
        CHECK(activos->rowCount() == 1);

        // Prende C: aparece solo, con lo suyo.
        m.actualizarPantallas({A, B, C, D});
        CHECK(m.monitor(idC).value("activa").toBool());
        CHECK(activos->rowCount() == 2);
        CHECK(m.esperando() == 0);
        CHECK(m.monitor(idC).value("orden").toInt() == 3);

        // Se desenchufa D: queda esperando; se lo olvida y ya no se espera.
        m.actualizarPantallas({A, B, C});
        CHECK(m.monitor(idD).value("esperando").toBool());
        CHECK(m.quitar(idD).isEmpty()); // sin estar y sin nada: se olvida
        CHECK(m.monitor(idD).isEmpty());
        CHECK(reset.count() == 0);

        // Una escena del monitor que se borra: el monitor la sigue recordando y lo dice.
        g_escenas = {"Plano general"};
        cat.refrescar();
        CHECK(m.asignar(idA, {"escena:Plano general"}, true).isEmpty());
        g_escenas = {};
        cat.refrescar();
        CHECK(m.monitor(idA).value("titulo").toString().contains("ya no existe"));
        CHECK(m.monitor(idA).value("cosas").toStringList() == QStringList{"escena:Plano general"});
    }
    {
        // Lo último quedó guardado.
        MonitoresModel m(archivo, &cat);
        CHECK(m.rowCount() == 2); // A y C (D se olvidó)
        CHECK(m.monitor(idD).isEmpty());
    }

    // ===================== 6. Las ventanas sueltas =========================
    {
        g_escenas = {"Plano general", "Atril"};
        cat.refrescar();
        // Las fuentes de video sueltas son cosas del catálogo (el programa no se repite).
        const auto* ana = cat.buscar("video:guest:1");
        CHECK(ana != nullptr && ana->titulo == "Ana" && ana->grupo == "video");
        CHECK(ana && ana->dibujo.value("claves").toStringList() == QStringList{"guest:1"});
        CHECK(cat.buscar("video:program") == nullptr);
        // El fondo que es una multivista se dibuja como multivista.
        g_fondoDist = "multiview:principal";
        cat.refrescar();
        CHECK(cat.buscar("fondo")->dibujo.value("modo").toString() == "multiview");
        g_fondoDist = "grid";
        cat.refrescar();

        // La única lista de monitores: los conectados en orden físico, con su id estable.
        MonitoresModel mon({}, &cat);
        mon.actualizarPantallas({A, B, C});
        const QVariantList lista = mon.conectadosEnOrden();
        CHECK(lista.size() == 3);
        CHECK(lista.size() == 3 && lista[0].toMap().value("id").toString() == idB &&
              lista[1].toMap().value("id").toString() == idA && lista[1].toMap().value("principal").toBool());
        CHECK(lista.size() == 3 && lista[2].toMap().value("orden").toInt() == 3 &&
              lista[2].toMap().value("sistema").toString() == "HDMI-1");
        CHECK(qobject_cast<QAbstractItemModel*>(mon.presentes())->rowCount() == 3);

        // Lo que había en ui.json: dos ventanas de video viejas.
        const QString ui = dir.filePath("Config/ui.json");
        {
            QJsonArray windows;
            windows.append(QJsonObject{{"keys", QJsonArray{"program", "guest:1"}}, {"layout", "pip"}, {"screen", "HDMI-1"},
                                       {"fullscreen", true}, {"x", 3900}, {"y", 10}, {"w", 800}, {"h", 450}});
            windows.append(QJsonObject{{"keys", QJsonArray{}}, {"layout", "multiview"}, {"screen", "DP-9"}});
            QFile f(ui);
            CHECK(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(QJsonObject{{"windows", windows}, {"cards", QJsonArray{}}}).toJson());
        }
        const std::filesystem::path archivoV = dir.filePath("Config/ventanas.json").toStdString();
        {
            VentanasModel v(archivoV, &cat, &mon);
            CHECK(v.migrarDesde(ui.toStdString(), "principal"));
            CHECK(v.rowCount() == 2);
            const auto& f0 = v.ventanas()[0];
            CHECK(f0.cosas == (QStringList{"programa", "video:guest:1"}));
            CHECK(f0.distribucion == "pip" && f0.pantallaCompleta && f0.geometria == QRect(3900, 10, 800, 450));
            // El monitor por nombre del sistema pasó a su id estable (está conectado).
            CHECK(f0.monitor == idC && f0.sistema.isEmpty());
            // «multiview» solo seguía a la elegida; su monitor no está: se lo traduce cuando aparezca.
            const auto& f1 = v.ventanas()[1];
            CHECK(f1.cosas == QStringList{"multiview:principal"});
            CHECK(f1.monitor.isEmpty() && f1.sistema == "DP-9");
            CHECK(v.ventanaDe("multiview:principal") == f1.id);
            CHECK(v.afuera() == QStringList{"multiview:principal"});
            // Ya hay ventanas.json: no se migra dos veces.
            CHECK(!v.migrarDesde(ui.toStdString(), "principal"));

            // Abrir lo mismo dos veces no abre otra: la trae al frente.
            Contador& frente = *contar(&v, &VentanasModel::cambio);
            QString err;
            const int p = v.abrirCon({"padron"}, {}, &err);
            CHECK(p > 0 && err.isEmpty());
            CHECK(v.abrirCon({"padron"}, {}) == p);
            CHECK(v.rowCount() == 3);
            CHECK(frente.count() >= 1);
            // Valida contra el catálogo, como un monitor.
            CHECK(v.abrirCon({"no-existe"}, {}, &err) == 0 && !err.isEmpty());
            CHECK(v.abrirCon({"web:mensaje"}, {}, &err) == 0);
            CHECK(v.abrirVideo({"armada"}) > 0);
            CHECK(v.ventanaDe("preview") > 0);
            v.setMonitor(p, idA);
            v.setPantallaCompleta(p, true);
            v.setGeometria(p, 10, 20, 640, 480);
            v.cerrarCosa("preview");
            CHECK(v.ventanaDe("preview") == 0);
            CHECK(v.setCosas(f1.id, {"multiview:principal", "preguntas"}).isEmpty());
            CHECK(v.ventana(f1.id).value("titulo").toString().contains(" + "));
            CHECK(!v.afuera().contains("multiview:principal"));
        }
        {
            // Se recuerda todo al volver (y el monitor que faltaba se traduce al aparecer).
            VentanasModel v(archivoV, &cat, &mon);
            CHECK(v.rowCount() == 3);
            const int p = v.ventanaDe("padron");
            CHECK(p > 0);
            const QVariantMap m = v.ventana(p);
            CHECK(m.value("monitor").toString() == idA && m.value("pantallaCompleta").toBool());
            CHECK(m.value("geometria").toRect() == QRect(10, 20, 640, 480));
            const PantallaFisica nuevo = pantalla("DP-9", "SERIE-9", 5760, 0);
            mon.actualizarPantallas({A, B, C, nuevo});
            bool traducida = false;
            for (const auto& f : v.ventanas())
                if (f.cosas.contains("preguntas")) traducida = f.monitor == MonitoresModel::idDe(nuevo) && f.sistema.isEmpty();
            CHECK(traducida);
            v.cerrar(p);
            CHECK(v.rowCount() == 2);
        }
        {
            VentanasModel v(archivoV, &cat, &mon);
            CHECK(v.rowCount() == 2);
            CHECK(v.ventanaDe("padron") == 0);
        }
        // Las claves viejas, a cosas del catálogo.
        CHECK(VentanasModel::cosaDeClave("program") == "programa");
        CHECK(VentanasModel::cosaDeClave("armada") == "preview");
        CHECK(VentanasModel::cosaDeClave("escena:Atril") == "escena:Atril");
        CHECK(VentanasModel::cosaDeClave("share:2") == "video:share:2");
    }

    if (g_fallas == 0) std::printf("test_pantallas_catalogo: OK\n");
    return g_fallas == 0 ? 0 : 1;
}
