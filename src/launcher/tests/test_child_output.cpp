// test_child_output.cpp
//
// Pruebas unitarias de la salida capturada de los hijos (Logs/app.log, ver
// ChildOutput.hpp). Sin framework externo: es un main() que devuelve 0 si todo
// pasa y 1 si algo falla (lo registra CTest). Compila y corre en Linux y
// Windows.
//
// Casos:
//   1. Un archivo que no existe o que todavia es chico no se rota.
//   2. Uno que supera el tope pasa a .1 y el activo desaparece (lo vuelve a
//      crear el lanzamiento siguiente).
//   3. Rotaciones sucesivas corren las generaciones (.1 -> .2) y la mas vieja
//      se pierde: nunca quedan mas de keepFiles.
//   4. prepareChildOutput crea Logs/, AGREGA un encabezado por lanzamiento sin
//      pisar lo que habia y, si el archivo crecio, lo rota antes.
//   5. Los argumentos del encabezado no llevan secretos.

#include "ChildOutput.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using ba::launcher::prepareChildOutput;
using ba::launcher::redactedArgs;
using ba::launcher::rotateChildOutput;

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

void spit(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool has(const std::string& all, const std::string& part) { return all.find(part) != std::string::npos; }

std::size_t occurrences(const std::string& all, const std::string& part) {
    std::size_t n = 0;
    for (std::size_t p = all.find(part); p != std::string::npos; p = all.find(part, p + 1)) {
        ++n;
    }
    return n;
}

void testRotation(const fs::path& dir) {
    const fs::path log = dir / "app.log";
    const fs::path log1 = dir / "app.log.1";
    const fs::path log2 = dir / "app.log.2";
    const fs::path log3 = dir / "app.log.3";

    // 1) No existe / todavia es chico: no se toca nada.
    CHECK(!rotateChildOutput(log, 100, 2));
    spit(log, std::string(100, 'a')); // justo en el tope: todavia no
    CHECK(!rotateChildOutput(log, 100, 2));
    CHECK(fs::exists(log));
    CHECK(!fs::exists(log1));

    // 2) Supera el tope: pasa a .1 y el activo desaparece.
    spit(log, std::string(101, 'a'));
    CHECK(rotateChildOutput(log, 100, 2));
    CHECK(!fs::exists(log));
    CHECK(slurp(log1) == std::string(101, 'a'));

    // 3) Otra corrida larga: .1 -> .2 y la nueva queda en .1.
    spit(log, std::string(150, 'b'));
    CHECK(rotateChildOutput(log, 100, 2));
    CHECK(slurp(log1) == std::string(150, 'b'));
    CHECK(slurp(log2) == std::string(101, 'a'));

    //    Y otra mas: la de las 'a' se pierde; nunca hay una .3.
    spit(log, std::string(200, 'c'));
    CHECK(rotateChildOutput(log, 100, 2));
    CHECK(slurp(log1) == std::string(200, 'c'));
    CHECK(slurp(log2) == std::string(150, 'b'));
    CHECK(!fs::exists(log3));
    CHECK(!fs::exists(dir / "app.log.0")); // el nombre de paso no queda tirado
}

void testPrepare(const fs::path& dir) {
    // 4) Logs/ todavia no existe: se crea, y cada lanzamiento AGREGA su encabezado.
    const fs::path log = dir / "Logs" / "app.log";
    const fs::path exe = fs::path("bin") / "64bit" / "ba-app";
    CHECK(prepareChildOutput(log, "A", exe, {}));
    CHECK(fs::exists(log));
    {
        std::ofstream child(log, std::ios::app | std::ios::binary);
        child << "ba-app: el motor no pudo arrancar\n"; // lo que escribiria el hijo
    }
    CHECK(prepareChildOutput(log, "A", exe, {"--restore", "--stream-server=rtmp://host/app/clave"}));
    const std::string all = slurp(log);
    CHECK(occurrences(all, "el launcher lanza A (ba-app)") == 2);
    CHECK(has(all, "args: (ninguno)"));
    CHECK(has(all, "ba-app: el motor no pudo arrancar"));
    CHECK(has(all, "args: --restore --stream-server=***"));
    CHECK(!has(all, "rtmp://"));
    CHECK(all.find("(ninguno)") < all.find("el motor no pudo arrancar"));
    CHECK(all.find("el motor no pudo arrancar") < all.find("--restore"));

    //    Con el archivo crecido, el lanzamiento lo rota y encabeza uno nuevo.
    CHECK(prepareChildOutput(log, "A", exe, {"--safe-mode"}, 64, 2));
    CHECK(has(slurp(dir / "Logs" / "app.log.1"), "el motor no pudo arrancar"));
    const std::string fresh = slurp(log);
    CHECK(occurrences(fresh, "el launcher lanza A") == 1);
    CHECK(has(fresh, "args: --safe-mode"));

    //    Un destino imposible (su "carpeta" es un archivo) no lanza: devuelve
    //    false y el hijo arranca sin captura.
    spit(dir / "no-es-carpeta", "x");
    CHECK(!prepareChildOutput(dir / "no-es-carpeta" / "app.log", "A", exe, {}));
    CHECK(!prepareChildOutput(fs::path(), "A", exe, {}));
}

void testRedactedArgs() {
    // 5) Las dos formas: --flag=valor (A) y --flag valor (B).
    CHECK(redactedArgs({}).empty());
    CHECK(redactedArgs({"--restore", "--safe-mode"}) == "--restore --safe-mode");
    CHECK(redactedArgs({"--guests=4", "--encoder=x264"}) == "--guests=4 --encoder=x264");
    CHECK(redactedArgs({"--pin", "123456", "--port", "8080"}) == "--pin *** --port 8080");
    CHECK(redactedArgs({"--tunnel-token", "eyJhIjoi", "--no-lobby"}) == "--tunnel-token *** --no-lobby");
    CHECK(redactedArgs({"--PIN=123456", "--api-key=abc", "--password=x"}) ==
          "--PIN=*** --api-key=*** --password=***");
    // Un flag sensible sin valor no se come al flag que sigue.
    CHECK(redactedArgs({"--pin", "--port", "8080"}) == "--pin --port 8080");
}

} // namespace

int main() {
    // Carpeta temporal propia para no pisar otros tests en paralelo.
    const fs::path dir = fs::temp_directory_path() / "ba_child_output_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    testRotation(dir / "rotacion");
    testPrepare(dir / "preparar");
    testRedactedArgs();

    fs::remove_all(dir);

    if (g_failures == 0) {
        std::printf("OK: test_child_output\n");
        return 0;
    }
    std::printf("FALLOS: %d\n", g_failures);
    return 1;
}
