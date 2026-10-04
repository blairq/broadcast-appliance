// src/app/red/Biblioteca.hpp — qué es un equipo de la red y cómo se le pide el video
//
// Dos datos vendoreados (sin internet en el aparato):
//   - datos/oui.tsv: el fabricante de cada bloque de direcciones MAC (el
//     registro público de la IEEE; tools/oui-generar.py).
//   - datos/camaras-urls.json: por marca, cómo reconocerla y sus rutas RTSP
//     típicas, el usuario de fábrica y lo que hay que saber (habilitar el
//     RTSP, de dónde sale la clave).
//
// Con las PISTAS de un equipo (nombre y modelo ONVIF, el encabezado Server y
// el título de su página web, el fabricante de su MAC, sus puertos abiertos)
// se elige la marca, en ese orden de confianza: la MAC sola engaña (EZVIZ usa
// MACs de Hikvision) y un puerto solo, más.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ba::app::red {

/// Fabricantes por MAC.
class Oui {
   public:
    /// Lee datos/oui.tsv (PREFIJO<TAB>BITS<TAB>FABRICANTE). false si no está.
    bool leer(const std::string& ruta);
    bool leerTexto(const std::string& texto);
    /// "00:40:8c:f5:89:c4" (o con guiones, o sin ceros a la izquierda como
    /// lo escribe `arp`) -> "Axis Communications AB", o "" si no se sabe.
    std::string fabricante(const std::string& mac) const;
    size_t cantidad() const { return bloques_.size(); }
    /// La MAC normalizada (12 hex en mayúsculas), o "" si no es una MAC.
    static std::string normalizar(const std::string& mac);

   private:
    /// (bits, prefijo) -> fabricante. Se busca del bloque más largo (36) al más corto (24).
    std::map<std::pair<int, std::uint64_t>, std::string> bloques_;
};

/// Lo que se sabe de un equipo para reconocerlo.
struct Pistas {
    std::string onvifNombre, onvifModelo, server, titulo, fabricanteMac;
    std::set<int> puertos;
};

/// La marca elegida y por qué.
struct Marca {
    std::string id, nombre;
    std::string porque; ///< "ONVIF «AXIS P5512»", "MAC de Hikvision"…
    int confianza = 0;  ///< 3 ONVIF, 2 web, 1 MAC, 0 puerto o genérica
};

/// Una URL a probar.
struct Candidata {
    std::string url;    ///< rtsp://host:puerto/ruta (sin usuario ni clave)
    std::string perfil; ///< principal, secundario, canal 2…
    std::string marca;
};

class Biblioteca {
   public:
    bool leer(const std::string& ruta);
    bool leerJson(const nlohmann::json& j);
    Marca reconocer(const Pistas& p) const;
    /// Las URLs RTSP de esa marca para `host` (con los canales 1..`canales`
    /// si es un grabador). Una marca desconocida da las genéricas.
    std::vector<Candidata> candidatas(const std::string& marca, const std::string& host, int puertoRtsp = 554,
                                      int canales = 1) const;
    /// La ficha de la marca (usuario de fábrica, notas, ptz), o {}.
    nlohmann::json ficha(const std::string& marca) const;

   private:
    nlohmann::json marcas_ = nlohmann::json::array();
};

} // namespace ba::app::red
