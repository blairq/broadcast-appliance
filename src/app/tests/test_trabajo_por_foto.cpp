// src/app/tests/test_trabajo_por_foto.cpp — la interfaz trabaja según lo que cambió, no según N
//
// El 2026-11-03, con 200 invitados en la PC de pruebas, la interfaz se trababa 2 a 6,5 s
// (mediciones-carga/2026-11-03-n2/trabas): con cada foto del motor o de la sala, oyentes que
// recorrían TODAS las entradas, y por cada una algo que también era O(N):
//   - VideoViewModel/SuperRes: el tamaño de cada entrada copiando el mapa entero de estados
//     por cada una;
//   - la Matriz: una lista de filas armada de nuevo en cada lectura de QML, y el canal de
//     cada fila buscado recorriendo la lista de canales;
//   - StudioViewModel: los nombres de los invitados a los rótulos de los cuadros con cada
//     roomChanged, avisando todas las claves.
//
// Acá, con 200 entradas/canales/invitados, una foto nueva que cambia UNA cosa hace trabajo
// de UNA (contadores de las piezas que usan esos modelos):
//   1. video/TamanosOrigen (VideoViewModel::tamanosOrigen, lo que lee SuperRes): recalcula
//      el estado de la entrada que cambió, no los 200; avisa solo si cambió la lista de
//      tamaños distintos.
//   2. emision/IndiceCanales (la Matriz y las tiras del Mezclador): el índice se arma una
//      vez por foto con otros canales (no una búsqueda lineal por fila) y
//      `canalesCambiados` da solo el canal que cambió (con los mismos canales, ninguno sin
//      comparar).
//   3. ui/ListaObjetos::ponerEn (el modelo de filas de la Matriz): cambiar los cruces de una
//      fila avisa esa fila y sus roles, sin reset ni las otras 199.
//   4. ui/LugaresCambiados (FrameService::setGuestNames): un nombre que cambia toca su lugar.
// Más una cota de tiempo holgada para 1000 fotos con un cambio cada una.

#include "emision/FotoMotor.hpp"
#include "emision/IndiceCanales.hpp"
#include "ui/ListaObjetos.hpp"
#include "ui/LugaresCambiados.hpp"
#include "video/TamanosOrigen.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <string>
#include <vector>

using namespace ba::app;
using json = nlohmann::json;

namespace {

int fallas = 0;

void comprobar(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++fallas;
}

constexpr int kN = 200;

std::string nombre(int i) { return "Invitado " + std::to_string(i); }

/// El estado de una entrada como lo arma VideoGrafos (lo que importa: gstreamer.origen),
/// con algo que cambia solo (los cuadros).
json estado(int w, int h, int cuadros) {
    return {{"activa", true},
            {"gstreamer", {{"origen", {{"ancho", w}, {"alto", h}}}, {"cuadros", cuadros}, {"sin_datos_s", 0.0}}},
            {"ajustes", {{"uri", "rtsp://x"}}}};
}

json estados200() {
    json e = json::object();
    for (int i = 0; i < kN; ++i) e[nombre(i)] = i % 50 == 0 ? estado(1920, 1080, 0) : estado(1280, 720, 0);
    return e;
}

std::vector<FotoMotor::CanalAudio> canales200() {
    std::vector<FotoMotor::CanalAudio> v;
    for (int i = 0; i < kN; ++i) {
        FotoMotor::CanalAudio c;
        c.nombre = nombre(i);
        c.tipo = "ba_gst_fuente";
        c.volumen["principal"] = 1.0f;
        c.mudo["principal"] = false;
        c.pistas = 1;
        v.push_back(std::move(c));
    }
    return v;
}

QVariantMap filaMatriz(int i, bool pista2) {
    QVariantMap cruces{{"pista1", QVariantMap{{"activo", true}}}};
    if (pista2) cruces["pista2"] = QVariantMap{{"activo", true}};
    return {{"nombre", QString::fromStdString(nombre(i))}, {"esVirtual", false}, {"cruces", cruces}};
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // --- 1. Los tamaños que llegan de las entradas (VideoViewModel → SuperRes) -----------
    {
        video::TamanosOrigen t;
        const json a = estados200();
        comprobar(t.releer(a, json::object(), true), "1a. la primera foto: hay tamaños");
        comprobar(t.recalculados() == kN, "1a. la primera foto mira las 200");
        comprobar(t.distintos().size() == 2, "1a. dos tamaños distintos (1280x720 y 1920x1080)");

        const json b = a; // otra foto, igual (lo común: cambió otra parte de la vista)
        comprobar(!t.releer(b, a, false), "1b. una foto igual no avisa");
        comprobar(t.recalculados() == 0, "1b. una foto igual no recalcula ninguna");

        json c = b;
        c[nombre(7)] = estado(1280, 720, 30); // solo se movieron sus cuadros
        comprobar(!t.releer(c, b, false), "1c. los cuadros de una no cambian los tamaños: no avisa");
        comprobar(t.recalculados() == 1, "1c. una entrada que cambia: se recalcula UNA");

        json d = c;
        d[nombre(8)] = estado(640, 360, 0); // cambió su modo
        comprobar(t.releer(d, c, false), "1d. un tamaño nuevo avisa");
        comprobar(t.recalculados() == 1 && t.distintos().size() == 3, "1d. una recalculada y tres tamaños");

        json e = d;
        e.erase(nombre(8)); // se fue
        comprobar(t.releer(e, d, true), "1e. se fue la única de 640x360: avisa");
        comprobar(t.distintos().size() == 2, "1e. vuelven a ser dos tamaños");

        // 1000 fotos con un cambio cada una: proporcional a lo que cambió (cota holgada:
        // compara cada estado con el suyo, sin copiar ni recalcular las demás).
        json ant = e;
        int recalculadas = 0;
        QElapsedTimer reloj;
        reloj.start();
        for (int k = 0; k < 1000; ++k) {
            json nueva = ant;
            nueva[nombre(k % kN)] = estado(1280, 720, k + 100);
            t.releer(nueva, ant, false);
            recalculadas += t.recalculados();
            ant = std::move(nueva);
        }
        comprobar(recalculadas == 1000, "1f. 1000 fotos con un cambio: una recalculada por foto (no 200)");
        std::printf("     (1000 fotos de 200 entradas: %lld ms, con la copia de cada foto del test)\n",
                    static_cast<long long>(reloj.elapsed()));
    }

    // --- 2. Los canales de la mezcla por nombre (Matriz, Mezclador) ----------------------
    {
        FotoMotor::MezclaAudio m1;
        m1.porOmision = "principal";
        m1.canales = canales200();
        IndiceCanales idx;
        int hallados = 0;
        for (int i = 0; i < kN; ++i) hallados += idx.canal(m1, nombre(i)) != nullptr ? 1 : 0;
        comprobar(hallados == kN, "2a. los 200 canales se encuentran por nombre");
        comprobar(idx.rearmados() == 1, "2a. el índice se arma UNA vez para 200 búsquedas");

        FotoMotor::MezclaAudio m2 = m1; // otra foto con los mismos canales (el mismo puntero)
        comprobar(canalesCambiados(m1.canales, m2.canales).empty(), "2b. los mismos canales: ninguno cambió");
        comprobar(!otrosNombres(m1.canales, m2.canales), "2b. los mismos canales: los mismos nombres");
        for (int i = 0; i < kN; ++i) idx.canal(m2, nombre(i));
        comprobar(idx.rearmados() == 1, "2b. con los mismos canales el índice no se rearma");

        FotoMotor::MezclaAudio m3 = m1;
        std::vector<FotoMotor::CanalAudio> v = m1.canales.get();
        v[57].volumen["principal"] = 0.5f;
        m3.canales = std::move(v); // otra foto: un fader
        const std::vector<std::size_t> cambiados = canalesCambiados(m1.canales, m3.canales);
        comprobar(cambiados.size() == 1 && cambiados[0] == 57, "2c. un fader que se movió: cambió UN canal (el suyo)");
        comprobar(!otrosNombres(m1.canales, m3.canales), "2c. un fader no cambia los nombres (sin rearmar las filas)");
        comprobar(idx.volumen(m3, nombre(57)) == 0.5f, "2c. el índice ve el valor nuevo");
        for (int i = 0; i < kN; ++i) idx.mudo(m3, nombre(i));
        comprobar(idx.rearmados() == 2, "2c. una foto con otros canales: el índice se rearma una vez (no por fila)");

        FotoMotor::MezclaAudio m4 = m3;
        std::vector<FotoMotor::CanalAudio> w = m3.canales.get();
        FotoMotor::CanalAudio nuevo;
        nuevo.nombre = nombre(kN);
        w.push_back(nuevo);
        m4.canales = std::move(w);
        comprobar(otrosNombres(m3.canales, m4.canales), "2d. entró uno: cambiaron los nombres");
        const std::vector<std::size_t> entro = canalesCambiados(m3.canales, m4.canales);
        comprobar(entro.size() == 1 && entro[0] == static_cast<std::size_t>(kN), "2d. entró uno: solo ese es nuevo");
    }

    // --- 3. El modelo de filas de la Matriz: una fila, sin tocar las demás --------------
    {
        ListaObjetos filas(QStringLiteral("nombre"), {QStringLiteral("esVirtual"), QStringLiteral("cruces")});
        QVariantList l;
        for (int i = 0; i < kN; ++i) l << filaMatriz(i, false);
        filas.poner(l);
        comprobar(filas.rowCount() == kN, "3a. 200 filas");

        int resets = 0, altas = 0, cambios = 0, filaCambiada = -1;
        QList<int> rolesAvisados;
        QObject::connect(&filas, &QAbstractItemModel::modelReset, [&]() { ++resets; });
        QObject::connect(&filas, &QAbstractItemModel::rowsInserted, [&]() { ++altas; });
        QObject::connect(&filas, &QAbstractItemModel::dataChanged,
                         [&](const QModelIndex& a, const QModelIndex& b, const QList<int>& roles) {
                             ++cambios;
                             filaCambiada = a.row() == b.row() ? a.row() : -2;
                             rolesAvisados = roles;
                         });
        comprobar(filas.ponerEn(57, filaMatriz(57, true)), "3b. ponerEn en su fila");
        comprobar(cambios == 1 && filaCambiada == 57, "3b. avisa UNA fila: la 57");
        const QHash<int, QByteArray> nombres = filas.roleNames();
        int rolCruces = -1, rolVirtual = -1;
        for (auto it = nombres.cbegin(); it != nombres.cend(); ++it) {
            if (it.value() == "cruces") rolCruces = it.key();
            if (it.value() == "esVirtual") rolVirtual = it.key();
        }
        comprobar(rolesAvisados.contains(rolCruces) && !rolesAvisados.contains(rolVirtual),
                  "3b. avisa el rol que cambió (cruces), no los demás");
        comprobar(resets == 0 && altas == 0, "3b. sin reset ni filas nuevas");
        comprobar(filas.ponerEn(57, filaMatriz(57, true)) && cambios == 1, "3c. lo mismo otra vez: no avisa nada");
        comprobar(!filas.ponerEn(58, filaMatriz(57, true)), "3d. otra clave en ese lugar: no la pisa (el modelo rearma)");

        // poner() con la lista entera y un solo cambio también avisa una fila (el camino
        // de `cambio`: rearmarFilas).
        l[57] = filaMatriz(57, true); // (lo que ya tiene el modelo)
        l[99] = filaMatriz(99, true);
        cambios = 0;
        filas.poner(l);
        comprobar(cambios == 1 && filaCambiada == 99 && resets == 0, "3e. la lista entera con un cambio: avisa esa fila");
    }

    // --- 4. Los nombres de los invitados a los rótulos (FrameService) ------------------
    {
        QStringList a;
        for (int i = 0; i < kN; ++i) a << QString::fromStdString(nombre(i));
        comprobar(lugaresCambiados(a, a).empty(), "4a. los mismos nombres: ningún lugar");
        QStringList b = a;
        b[41] = QStringLiteral("Ana");
        const std::vector<int> c = lugaresCambiados(a, b);
        comprobar(c.size() == 1 && c[0] == 42, "4b. un nombre que cambia: su lugar (42), no los 200");
        QStringList d = b;
        d << QStringLiteral("Beto");
        const std::vector<int> e = lugaresCambiados(b, d);
        comprobar(e.size() == 1 && e[0] == kN + 1, "4c. entra uno: su lugar");
    }

    if (fallas == 0)
        std::printf("\nTODO BIEN\n");
    else
        std::printf("\n%d FALLAS\n", fallas);
    return fallas == 0 ? 0 : 1;
}
