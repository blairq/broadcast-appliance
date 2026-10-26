// src/app/dispositivos/SondaDrivers.cpp — ver SondaDrivers.hpp

#include "dispositivos/SondaDrivers.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::app::dispositivos::sonda {

using json = nlohmann::json;

namespace {

/// Una línea por evento, y enseguida: si lo que sigue cuelga o tumba el proceso,
/// lo anterior ya llegó.
void decir(const json& j) {
    const std::string s = j.dump(-1, ' ', false, json::error_handler_t::replace);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

std::vector<std::string> separar(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string p;
    while (std::getline(ss, p, ','))
        if (!p.empty()) out.push_back(p);
    return out;
}

} // namespace

Fuente fuenteDePrueba() {
    Fuente f;
    f.listar = []() {
        std::vector<json> out;
        const char* l = std::getenv("BA_SONDA_FALSOS");
        if (l == nullptr || *l == '\0') return out;
        const char* firma = std::getenv("BA_SONDA_FALSOS_FIRMA");
        for (const std::string& como : separar(l))
            out.push_back({{"id", "falso:" + como},
                           {"nombre", "Driver de prueba (" + como + ")"},
                           {"tipo", "prueba"},
                           {"firma", firma != nullptr && *firma != '\0' ? firma : "1"},
                           {"rango", 0}});
        return out;
    };
    f.esMia = [](const std::string& id) { return id.rfind("falso:", 0) == 0; };
    f.probar = [](const std::string& id) -> json {
        const std::string como = id.substr(6);
        // Cada vez que se lo carga, una línea: los tests cuentan si se volvió a probar.
        if (const char* diario = std::getenv("BA_SONDA_FALSOS_DIARIO"); diario != nullptr && *diario != '\0')
            if (std::FILE* fd = std::fopen(diario, "a")) {
                std::fprintf(fd, "%s\n", id.c_str());
                std::fclose(fd);
            }
        if (como == "anda") return {{"ok", true}, {"detalle", "1 dispositivo de prueba"},
                                    {"dispositivos", json::array({{{"nombre", "Prueba"}, {"clase", "Audio/Source"}}})}};
        if (como == "error") return {{"ok", false}, {"motivo", "el driver de prueba no arranca"}};
        if (como == "jack") {
            // Lo que hace JackRouter sin el servidor: lo dice por stderr y no arranca.
            std::fprintf(stderr, "Open error: is JACK server running?\n");
            std::fflush(stderr);
            return {{"ok", false}};
        }
        if (como == "cuelga")
            for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
        if (como == "cae") {
            std::fprintf(stderr, "driver de prueba: me caigo\n");
            std::fflush(stderr);
            std::abort();
        }
        return {{"ausente", true}};
    };
    return f;
}

int principal(int argc, char** argv, const std::vector<Fuente>& fuentes) {
#if defined(_WIN32)
    // Que un driver que se cae no abra el cuadro de «dejó de funcionar» (nadie lo ve
    // y el proceso quedaría esperando un clic): termina y la app se entera ya.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (argc < 3 || std::strcmp(argv[1], kBandera) != 0) {
        std::fprintf(stderr, "uso: %s %s listar | probar <id>…\n", argc > 0 ? argv[0] : "ba-app", kBandera);
        return 2;
    }
    const std::string que = argv[2];
    std::vector<bool> preparada(fuentes.size(), false);
    auto preparar = [&](std::size_t i) {
        if (preparada[i]) return;
        preparada[i] = true;
        if (fuentes[i].preparar) fuentes[i].preparar();
    };
    if (que == "listar") {
        for (std::size_t i = 0; i < fuentes.size(); ++i) {
            if (!fuentes[i].listar) continue;
            preparar(i);
            for (const json& d : fuentes[i].listar()) decir({{"driver", d}});
        }
        decir({{"fin", true}});
        return 0;
    }
    if (que == "probar") {
        for (int a = 3; a < argc; ++a) {
            const std::string id = argv[a];
            decir({{"empieza", id}});
            json r{{"ausente", true}};
            for (std::size_t i = 0; i < fuentes.size(); ++i) {
                if (!fuentes[i].esMia || !fuentes[i].esMia(id)) continue;
                preparar(i);
                r = fuentes[i].probar(id);
                break;
            }
            r["resultado"] = id;
            decir(r);
        }
        decir({{"fin", true}});
        return 0;
    }
    std::fprintf(stderr, "no sé hacer «%s»\n", que.c_str());
    return 2;
}

} // namespace ba::app::dispositivos::sonda
