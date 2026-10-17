// El Indice de verdad (src/app/mcp/McpIndice.cpp) sobre docs/mcp.json, para
// comparar con el port de tools/dev/medir-busqueda.py (--arnes lo compila y lo
// corre). Entrada: el inventario y un jsonl de casos [frase, [herramientas]].
// Salida: «casos primera primeras3 primeras8», en numeros enteros.
#include "mcp/McpIndice.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

using namespace ba::app::mcp;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: %s docs/mcp.json casos.jsonl\n", argv[0]);
        return 2;
    }
    nlohmann::json inv;
    std::ifstream(argv[1]) >> inv;
    Indice i;
    for (const auto& h : inv["herramientas"]) {
        Documento d;
        d.id = h["nombre"];
        d.clase = "herramienta";
        d.nombre = h["nombre"];
        d.titulo = h.value("titulo", "");
        d.etiquetas = {h["etiquetas"].value("porPrefijo", ""), h["etiquetas"].value("propias", "")};
        d.cuerpo = h.value("descripcion", "");
        i.agregar(std::move(d));
    }
    i.armar();
    int n = 0, e1 = 0, e3 = 0, e8 = 0;
    std::ifstream f(argv[2]);
    for (std::string l; std::getline(f, l);) {
        if (l.empty()) continue;
        const auto c = nlohmann::json::parse(l);
        const std::set<std::string> validas(c[1].begin(), c[1].end());
        const auto r = i.buscar(c[0].get<std::string>(), 8);
        int p = -1;
        for (std::size_t k = 0; k < r.size() && p < 0; ++k)
            if (validas.count(i.doc(r[k].doc).id) != 0) p = static_cast<int>(k);
        ++n;
        e1 += p == 0;
        e3 += p >= 0 && p < 3;
        e8 += p >= 0;
    }
    std::printf("%d %d %d %d\n", n, e1, e3, e8);
    return 0;
}
