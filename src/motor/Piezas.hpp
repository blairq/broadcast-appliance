// ============================================================================
// src/motor/Piezas.hpp — lo que comparten las cadenas del motor
//
// Sacado de la capa de entrada vieja (ba_gst_fuente, que vivía dentro de OBS)
// sin nada de OBS: los filtros como elementos de GStreamer, las cámaras y
// micrófonos del equipo por su id estable, el vigía de dispositivos, qué tiene
// un archivo y las caps del lienzo.
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ba::motor {

/// El lienzo del estudio: a qué se normaliza cada entrada y a qué sale el programa.
struct Lienzo {
    int ancho = 1920, alto = 1080;
    int fpsN = 30, fpsD = 1;
    std::string color = "bt709";  ///< bt601, bt709, bt2100-pq, bt2100-hlg
    bool rangoCompleto = false;
    bool diezBits = false;        ///< P010 en vez de NV12 (también con HDR)
};

/// Las caps de color del lienzo («video/x-raw,format=NV12,colorimetry=…»).
std::string capsColorDelLienzo(const Lienzo& l);

/// Un filtro de la cadena: un elemento de GStreamer con sus propiedades (como texto).
struct Filtro {
    std::string id, elemento;
    std::string nombre;  ///< el id sin caracteres raros: el elemento se llama <prefijo><nombre>
    std::string memoria; ///< la que pide (CUDAMemory, GLMemory, D3D11Memory, D3D12Memory; "" = sistema)
    bool saleEnSistema = false; ///< entrega en memoria de sistema (no hace falta bajar después)
    std::vector<std::pair<std::string, std::string>> props;
};

/// [{"id","elemento","memoria","memoria_salida","propiedades":{…}}] -> filtros.
std::vector<Filtro> leerFiltros(const nlohmann::json& lista);
/// Lo que obliga a rearmar: qué elementos, en qué orden, y las propiedades que no cambian andando.
std::string firmaDe(const std::vector<Filtro>& v, const std::vector<Filtro>& a, bool alfa);
/// Las propiedades de un filtro sobre su elemento (las que no tiene, a `avisos`).
void ponerPropiedades(GstElement* e, const Filtro& f, std::vector<std::string>* avisos);
/// Pone en vivo las propiedades que se pueden cambiar andando. true si todas.
bool ponerEnVivo(GstElement* pipeline, const std::vector<Filtro>& v, const char* prefijo);
bool hayFabrica(const std::string& elemento);
/// La cadena de un tipo de filtros para gst_parse_launch ("" si no hay ninguno).
std::string cadenaDe(const std::vector<Filtro>& v, const char* conv, const char* prefijo,
                     std::vector<std::string>& avisos);
/// Entre comillas para gst_parse_launch (los id de Windows traen \\ y #).
std::string entreComillas(const std::string& v);

/// El id estable de un dispositivo de video (el que guarda `camara`).
std::string idDeCamara(GstDevice* dev, GstElement* e);
/// El id estable de un micrófono (el que guarda `microfono`).
std::string idDeMicrofono(GstElement* e);
/// La parte de origen de la cámara `id` como está enchufada AHORA ("" si no está).
/// Sin `modo`: el de la cámara que mejor va con el lienzo.
std::string origenDeCamara(const std::string& id, std::string modo, const Lienzo& lienzo);

/// Un GstDeviceMonitor de todo el proceso: qué micrófonos y cámaras hay, y una
/// generación que cambia cuando se conecta o se va uno.
class Vigia {
   public:
    static Vigia& el();
    /// Lee los avisos del monitor (no bloquea).
    void revisar();
    /// ¿Está? (sin saberlo todavía: sí, para no trabar el arranque).
    bool hay(bool esVideo, const std::string& id);
    std::uint64_t generacion() const { return generacion_.load(); }

   private:
    void anotar(GstDevice* dev, bool esta);
    void arrancar();
    std::mutex mu_;
    GstDeviceMonitor* mon_ = nullptr;
    std::set<std::string> audio_, video_;
    std::atomic<std::uint64_t> generacion_{1};
};

/// Qué tiene un archivo (video, audio), con GstDiscoverer. false + error si no se puede leer.
bool queTieneElArchivo(const std::string& uri, bool& video, bool& audio, std::string& error);

/// Lo que llega (caps de video) en castellano, por campo, para el diagnóstico.
nlohmann::json describirCaps(GstCaps* c);

/// Ningún elemento del pipeline ofrece su reloj (un micrófono que se desenchufa
/// se llevaría el reloj: CLOCK_LOST y todo trabado).
void sinRelojPropio(GstElement* pipeline);

/// Un valor JSON como texto para gst_util_set_object_arg.
std::string comoTexto(const nlohmann::json& v);

} // namespace ba::motor
