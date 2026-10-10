// test_utf8.cpp — diag/Utf8: un texto de terceros siempre sale UTF-8 válido
//
// El caso de verdad (2026-10-10): ONNX Runtime en un Windows en castellano dijo
// «No hay suficientes recursos de memoria disponibles para completar esta
// operación» en cp1252; el byte 0xF3 suelto, metido en un JSON, colgó la app.
#include "diag/Utf8.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>

namespace {
int fallos = 0;
void check(bool ok, const char* que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que);
    if (!ok) ++fallos;
}
} // namespace

int main() {
    using ba::diag::esUtf8;
    using ba::diag::utf8Valido;

    check(utf8Valido("hola") == "hola", "ASCII queda igual");
    check(utf8Valido("operación ñandú «»") == "operación ñandú «»", "UTF-8 válido queda igual");

    const std::string cp1252 = "esta operaci\xF3n";
    check(!esUtf8(cp1252), "cp1252 suelto no es UTF-8");
    const std::string arreglado = utf8Valido(cp1252);
    check(esUtf8(arreglado), "lo arreglado es UTF-8");
#if defined(_WIN32)
    check(arreglado == "esta operación", "en Windows se lee bien (desde la página del sistema)");
#else
    check(arreglado == "esta operaci?n", "afuera de Windows, el byte inválido pasa a «?»");
#endif

    // Lo que importaba: que el JSON no tire excepción al serializarlo.
    bool tiro = false;
    try {
        nlohmann::json j{{"error", utf8Valido(cp1252)}};
        (void)j.dump();
    } catch (const std::exception&) {
        tiro = true;
    }
    check(!tiro, "un JSON con el texto arreglado se serializa sin excepción");

    check(!esUtf8(std::string("\xC0\xAF")), "secuencia larga de más: inválida");
    check(!esUtf8(std::string("\xED\xA0\x80")), "sustituto UTF-16: inválido");
    check(!esUtf8(std::string("abc\xE2\x82")), "cortada al final: inválida");
    check(esUtf8(utf8Valido(std::string("abc\xE2\x82"))), "cortada al final: se arregla");
    check(utf8Valido(nullptr).empty(), "nulo: vacío");

    std::printf(fallos ? "\n%d fallas\n" : "\ntodo ok\n", fallos);
    return fallos ? 1 : 0;
}
