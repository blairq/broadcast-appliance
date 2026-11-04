// test_json.cpp — json/Json: ba::jsonDeTexto y ba::textoDeJson hacen lo mismo que nlohmann
#include "json/Json.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>

static int g_fallas = 0;
static void check(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++g_fallas;
}

int main() {
    const nlohmann::json j = ba::jsonDeTexto(R"({"a": 1, "b": [true, null, "x"]})");
    check(j.is_object() && j.at("a") == 1 && j.at("b").size() == 3, "leer un objeto");
    check(ba::textoDeJson(j) == j.dump(), "texto = dump()");
    check(ba::jsonDeTexto(ba::textoDeJson(j)) == j, "ida y vuelta");
    const std::string s = "[1,2,3]";
    check(ba::jsonDeTexto(s) == nlohmann::json::parse(s), "igual que parse");

    bool tiro = false;
    try {
        (void)ba::jsonDeTexto("{no es json");
    } catch (const nlohmann::json::parse_error&) {
        tiro = true;
    }
    check(tiro, "lo que no es JSON tira parse_error, como parse");

    std::printf("%s\n", g_fallas == 0 ? "TODO OK" : "HAY FALLAS");
    return g_fallas == 0 ? 0 : 1;
}
