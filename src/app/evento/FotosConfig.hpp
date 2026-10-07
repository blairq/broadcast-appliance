// ============================================================================
// src/app/evento/FotosConfig.hpp — la configuración de cada evento (H3 del histórico)
//
// «La configuración de cada dato del sistema»: una FOTO de Config/ cuando un
// evento entra en curso y otra cuando deja de estarlo, y cada cambio del medio
// en la bitácora. Así se puede ver con qué configuración se hizo un evento,
// comparar dos y (H6) volver a una.
//
// LA FOTO es un índice chico, no una copia:
//
//     <evento>/config/inicio.json   {"t": ms, "archivos": {"escenas.json": {"sha": "…", "bytes": n}, …}}
//     <evento>/config/cierre.json
//     Eventos/.objetos/<sha>.json   el contenido de cada archivo, UNA vez
//
// Cien eventos con la misma configuración ocupan lo de una: cada contenido se
// guarda por su sha256 y los índices solo lo nombran.
//
// Qué entra: los *.json de Config/ (también en subcarpetas), menos las copias
// de seguridad (*.anterior, *.migrado) y lo que pese más de 4 MB.
//
// SIN SECRETOS: antes de guardar, lo que tenga nombre de secreto (token, pin,
// clave, password, secret, key…) y valor de secreto (una tira larga sin
// espacios) se reemplaza por «tapado». Una foto se puede mostrar o mandar sin
// miedo; al restaurar (H6), lo tapado se deja como está ahora.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::evento {

class FotosConfig {
   public:
    using json = nlohmann::json;
    static constexpr const char* kTapado = "«tapado»";

    /// :param config: la carpeta Config/. :param objetos: Eventos/.objetos/.
    FotosConfig(std::filesystem::path config, std::filesystem::path objetos);

    /// La foto de ahora: {"t", "archivos": {ruta: {sha, bytes}}}. Guarda en
    /// objetos/ los contenidos que todavía no estén. No vuelve a leer un
    /// archivo que no cambió de fecha ni de tamaño desde la última vez.
    json foto(std::int64_t ahoraMs);

    /// Qué cambió entre dos fotos: [{archivo, que: agregado|quitado|cambiado,
    /// antes, despues (sha), cambios: JSON Patch (RFC 6902) recortado a `tope`
    /// operaciones, total}].
    json comparar(const json& antes, const json& despues, std::size_t tope = 40) const;

    /// El contenido guardado con ese sha (null si no está).
    json leer(const std::string& sha) const;

    /// El JSON con los secretos tapados.
    static json sinSecretos(const json& j);

    /// Guarda y lee una foto en un archivo (con .tmp + rename).
    static bool guardar(const std::filesystem::path& archivo, const json& foto);
    static json cargar(const std::filesystem::path& archivo);

   private:
    struct Visto {
        std::int64_t mtime = 0;
        std::uintmax_t bytes = 0;
        std::string sha;
    };
    std::filesystem::path config_, objetos_;
    std::map<std::string, Visto> vistos_;
};

} // namespace ba::evento
