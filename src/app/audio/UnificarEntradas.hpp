// ============================================================================
// src/app/audio/UnificarEntradas.hpp — una entrada de audio por dispositivo físico
//
// El monitor de dispositivos de GStreamer junta a todos sus proveedores, y en
// Windows el mismo micrófono aparece varias veces: WASAPI con el id del
// endpoint («{0.0.1.00000000}.{eea76234-…}»), otro proveedor con el GUID solo
// y en mayúsculas («{EEA76234-…}»), más «Default Audio Capture Device» y
// «Default Audio Render Device» (las predeterminadas, que ya están como
// «Entrada de audio predeterminada»), y las salidas como «entrada» en loopback
// (lo que suena en el parlante del monitor). En la lista de Entradas de audio
// y en el registro de la máquina eso eran micrófonos repetidos y salidas
// disfrazadas de micrófono.
//
// La regla:
//   - Solo cuenta lo que abre el elemento que después se usa en el pipeline
//     (wasapi2src, osxaudiosrc, pulsesrc): lo de otro proveedor no serviría.
//   - El dispositivo físico es el GUID del endpoint, sin distinguir mayúsculas:
//     dos ids con el mismo GUID son UNO (se queda el id de endpoint, que es el
//     que entiende wasapi2src).
//   - Las predeterminadas del sistema (el GUID es el de la CLASE de dispositivo
//     de captura o de reproducción) no se repiten: hay una sola, «default».
//   - El loopback de una salida se ofrece como lo que es: «Lo que suena en
//     «…»», con valor «loopback:<id>» (el pipeline le pone loopback=true).
//
// Sin Qt ni GStreamer: se prueba solo (test_dispositivos).
// ============================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ba::app::audio {

/// El prefijo del valor de una entrada que captura lo que suena en una salida.
inline constexpr const char* kPrefijoLoopback = "loopback:";

namespace detalle {
inline std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
/// ¿«xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx»?
inline bool esGuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool guion = i == 8 || i == 13 || i == 18 || i == 23;
        if (guion ? s[i] != '-' : std::isxdigit(static_cast<unsigned char>(s[i])) == 0) return false;
    }
    return true;
}
/// Las clases de interfaz de audio de Windows (KSCATEGORY/DEVINTERFACE_AUDIO_*):
/// no son un dispositivo, son «el que el sistema tenga elegido».
inline bool esClaseDeAudio(const std::string& guid) {
    return guid == "2eef81be-33fa-4800-9670-1cd474972c3f"     // captura
           || guid == "e6327cad-dcec-4949-ae8a-991e976a79d2"; // reproducción
}
/// El último GUID entre llaves del id que no sea una clase de interfaz, en minúsculas.
inline std::string guidDelEndpoint(const std::string& id) {
    std::string hallado;
    for (std::size_t a = id.find('{'); a != std::string::npos; a = id.find('{', a + 1)) {
        const std::size_t b = id.find('}', a);
        if (b == std::string::npos) break;
        const std::string g = minusculas(id.substr(a + 1, b - a - 1));
        if (esGuid(g) && !esClaseDeAudio(g)) hallado = g;
    }
    return hallado;
}
} // namespace detalle

/// El dispositivo físico detrás de un id: el GUID del endpoint (sin
/// mayúsculas) si lo tiene; si no, el id tal cual. «loopback:» no cuenta.
inline std::string claveFisica(std::string id) {
    if (id.rfind(kPrefijoLoopback, 0) == 0) id = id.substr(std::char_traits<char>::length(kPrefijoLoopback));
    const std::string g = detalle::guidDelEndpoint(id);
    return g.empty() ? id : g;
}

/// ¿Es una de las predeterminadas que inventa el proveedor («Default Audio
/// Capture Device»)? Su id solo trae el GUID de la clase de audio.
inline bool esPredeterminadaDelProveedor(const std::string& id) {
    if (id.find('{') == std::string::npos) return false;
    if (!detalle::guidDelEndpoint(id).empty()) return false;
    for (std::size_t a = id.find('{'); a != std::string::npos; a = id.find('{', a + 1)) {
        const std::size_t b = id.find('}', a);
        if (b == std::string::npos) break;
        if (detalle::esClaseDeAudio(detalle::minusculas(id.substr(a + 1, b - a - 1)))) return true;
    }
    return false;
}

/// Lo que dice el monitor de dispositivos de una entrada.
struct EntradaCruda {
    std::string nombre;
    std::string id;       ///< el valor de la propiedad del elemento (device, unique-id)
    std::string elemento; ///< la fábrica del elemento que la abre ("" = no se sabe)
    bool loopback = false;
};

/// Las entradas para ofrecer: [{nombre, valor}], una por dispositivo físico, sin
/// las predeterminadas del proveedor y con los loopback nombrados como tales.
/// No incluye la «default» propia (la agrega quien llama, primera).
inline std::vector<std::pair<std::string, std::string>> unificarEntradas(const std::vector<EntradaCruda>& crudas,
                                                                         const std::string& elementoEsperado) {
    struct Elegida {
        std::string clave, nombre, valor;
        bool endpoint = false;
    };
    std::vector<Elegida> out;
    for (const EntradaCruda& c : crudas) {
        if (c.id.empty() || c.id == "default") continue;
        if (!c.elemento.empty() && !elementoEsperado.empty() && c.elemento != elementoEsperado) continue;
        if (esPredeterminadaDelProveedor(c.id)) continue;
        const std::string valor = c.loopback ? kPrefijoLoopback + c.id : c.id;
        // El loopback de una salida y el micrófono son dispositivos distintos aunque
        // compartan placa: la clave lleva de qué clase es.
        const std::string clave = (c.loopback ? "L:" : "E:") + claveFisica(c.id);
        const std::string nombre = c.loopback ? "Lo que suena en «" + c.nombre + "»" : c.nombre;
        const bool endpoint = c.id.find("{0.0.") != std::string::npos;
        auto it = std::find_if(out.begin(), out.end(), [&](const Elegida& e) { return e.clave == clave; });
        if (it == out.end())
            out.push_back({clave, nombre, valor, endpoint});
        else if (endpoint && !it->endpoint) // el id de endpoint es el que abre wasapi2src
            *it = {clave, nombre, valor, endpoint};
    }
    std::vector<std::pair<std::string, std::string>> r;
    for (const Elegida& e : out) r.emplace_back(e.nombre, e.valor);
    return r;
}

} // namespace ba::app::audio
