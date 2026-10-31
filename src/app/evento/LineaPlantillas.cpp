// ============================================================================
// src/app/evento/LineaPlantillas.cpp — ver LineaPlantillas.hpp
// ============================================================================
#include "evento/LineaPlantillas.hpp"

#include "evento/LeerJson.hpp"

#include <algorithm>

namespace ba::evento::linea {

using nlohmann::json;

namespace {

std::string llenarMarcas(std::string s, const std::map<std::string, std::string>& v) {
    for (const auto& [k, val] : v) {
        const std::string marca = "{" + k + "}";
        for (std::size_t p = s.find(marca); p != std::string::npos; p = s.find(marca, p + val.size()))
            s.replace(p, marca.size(), val);
    }
    return s;
}

bool esNumero(const std::string& s) {
    return !s.empty() && s.size() < 10 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

void llenarDatos(json& d, const std::map<std::string, std::string>& v) {
    if (d.is_string()) {
        const std::string antes = d.get<std::string>();
        const std::string despues = llenarMarcas(antes, v);
        // "{n}" solo, y lo que quedó es un número: va como número (la pieza `tratar` lo espera así).
        const bool soloMarca = antes.size() > 2 && antes.front() == '{' && antes.back() == '}' &&
                               antes.find('{', 1) == std::string::npos;
        if (soloMarca && esNumero(despues)) d = std::stoi(despues);
        else d = despues;
    } else if (d.is_object() || d.is_array()) {
        for (auto& x : d)
            llenarDatos(x, v);
    }
}

bool esDeLaExpansion(const Paso& p) {
    return std::find(p.etiquetas.begin(), p.etiquetas.end(), kEtiquetaOrdenDelDia) != p.etiquetas.end();
}

} // namespace

std::vector<Paso> instanciarPlantilla(const Plantilla& t, const std::string& prefijo, const std::map<std::string, std::string>& v) {
    std::vector<Paso> out;
    for (Paso p : t.pasos) {
        p.id = prefijo + "-" + p.id;
        p.nombre = llenarMarcas(p.nombre, v);
        p.guion = llenarMarcas(p.guion, v);
        p.notaOperador = llenarMarcas(p.notaOperador, v);
        // Las referencias entre pasos de la misma plantilla siguen apuntando adentro.
        for (const Paso& otro : t.pasos)
            if (p.inicio.de == otro.id) p.inicio.de = prefijo + "-" + otro.id;
        for (Pieza& z : p.piezas) {
            z.id = prefijo + "-" + z.id;
            z.texto = llenarMarcas(z.texto, v);
            llenarDatos(z.datos, v);
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool expandirOrdenDelDia(Linea& l, const json& puntos, const std::set<std::string>& emitidos) {
    bool cambio = false;
    for (Bloque& b : l.bloques) {
        if (b.plantilla.empty() || b.origen != kOrigenOrdenDelDia) continue;
        const auto t = std::find_if(l.plantillas.begin(), l.plantillas.end(), [&](const Plantilla& x) { return x.id == b.plantilla; });
        if (t == l.plantillas.end()) continue; // lo marca revisar()

        // Lo que hay: lo emitido de la expansión queda; lo puesto a mano, también.
        std::vector<Paso> nuevos, aMano;
        std::map<std::string, const Paso*> viejos;
        for (const Paso& p : b.pasos) {
            if (esDeLaExpansion(p)) viejos[p.id] = &p;
            else aMano.push_back(p);
        }
        std::set<std::string> puestos;
        if (puntos.is_array())
            for (const json& pt : puntos) {
                if (!pt.is_object() || leerTexto(pt, "estado") == "retirado") continue;
                const int64_t n = leerNumero(pt, "n");
                const std::string titulo = leerTexto(pt, "titulo");
                const std::map<std::string, std::string> v{{"n", std::to_string(n)},
                                                           {"titulo", titulo},
                                                           {"expediente", leerTexto(pt, "expediente")},
                                                           {"punto", std::to_string(n) + ". " + titulo}};
                for (Paso& p : instanciarPlantilla(*t, b.id + "-p" + std::to_string(n), v)) {
                    if (emitidos.count(p.id) && viejos.count(p.id)) {
                        nuevos.push_back(*viejos[p.id]); // salió así: no se toca
                    } else {
                        p.etiquetas.push_back(kEtiquetaOrdenDelDia);
                        nuevos.push_back(std::move(p));
                    }
                    puestos.insert(nuevos.back().id);
                }
            }
        // Lo emitido de puntos que ya no están (se retiró, se renumeró) queda adelante:
        // ya pasó, y el recorrido lo nombra.
        std::vector<Paso> pasados;
        for (const Paso& p : b.pasos)
            if (esDeLaExpansion(p) && emitidos.count(p.id) && !puestos.count(p.id)) pasados.push_back(p);
        nuevos.insert(nuevos.begin(), pasados.begin(), pasados.end());
        nuevos.insert(nuevos.end(), aMano.begin(), aMano.end());

        const auto comoJson = [](const std::vector<Paso>& v) {
            json a = json::array();
            for (const Paso& p : v)
                a.push_back(p.aJson());
            return a;
        };
        if (comoJson(nuevos) != comoJson(b.pasos)) {
            b.pasos = std::move(nuevos);
            cambio = true;
        }
    }
    return cambio;
}

} // namespace ba::evento::linea
