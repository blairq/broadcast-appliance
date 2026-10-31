// ============================================================================
// src/app/ui/HistoricoConsulta.hpp — leer el histórico en JSON: lo de la página Histórico y del MCP
//
// La página Histórico (HistoricoViewModel) y las herramientas evento_* e historico_* del
// MCP dicen lo mismo porque leen por acá: un evento por su nombre en el histórico, su
// resumen, sus grabaciones y vivos, su bitácora, sus fotos de Config/ (y qué cambió),
// sus archivos, exportarlo y verificar sus sellos.
//
// LOS NOMBRES. Los eventos del evento abierto (la configuración activa) se nombran con su
// carpeta a secas («2026/Octubre/Sesion-03»; vacío = el en curso); los de las OTRAS
// configuraciones, «@<configuración>/<carpeta>». Todo lo de acá entiende los dos.
//
// SIN ESTADO Y SIN LA INTERFAZ: es un valor (las raíces) que se copia a otro hilo. Todo
// lee el disco, así que se llama desde un hilo de trabajo (el de la vista) o desde el
// pool del MCP (Hilo::Foto), nunca desde la interfaz.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {

class HistoricoConsulta {
   public:
    using json = nlohmann::json;

    /// `configuraciones`: la carpeta de todas las configuraciones (cada una con su
    /// Eventos/ y su Config/); `activa`: el nombre de la abierta; `eventos` y `config`:
    /// las de la abierta.
    HistoricoConsulta(std::filesystem::path configuraciones, std::string activa, std::filesystem::path eventos,
                      std::filesystem::path config);
    /// Las de este programa (ba::platform::dataRoot()) con `eventos`/`config` de la abierta.
    static HistoricoConsulta deLaApp(const std::filesystem::path& eventos, const std::filesystem::path& config);

    /// Dónde vive un evento del histórico.
    struct Lugar {
        std::filesystem::path raiz;   ///< su Eventos/
        std::filesystem::path config; ///< su Config/
        std::string rel;              ///< la carpeta del evento dentro de Eventos/ ("" = el en curso)
        std::string evento;           ///< la configuración (la carpeta de arriba)
        bool abierto = true;          ///< es de la configuración abierta
    };
    Lugar lugar(const std::string& carpeta) const;
    /// La carpeta del evento si existe ("" = el en curso de la abierta); vacía si no.
    std::filesystem::path carpeta(const std::string& carpeta) const;
    const std::filesystem::path& eventos() const { return eventos_; }
    const std::filesystem::path& config() const { return config_; }
    const std::string& activa() const { return activa_; }
    /// Los Eventos/ de las otras configuraciones: {nombre, raiz}.
    std::vector<std::pair<std::string, std::filesystem::path>> otras() const;

    /// "no hay un evento «x»" (o «no hay un evento en curso»), traducido.
    static std::string noHay(const std::string& carpeta);

    /// El resumen de la lista (evento::Historico::resumen) y la ficha: {…, estado, desde_ms,
    /// hasta_ms, lugar, notas, etiquetas, automatico, documentos:[{archivo, ruta}],
    /// porTipo:{tipo: líneas}}. {error} si no está.
    json resumen(const std::string& carpeta, bool conDetalle) const;
    /// Las líneas de la bitácora con los filtros (las últimas `limite`); `hora` en cada una.
    /// {evento, lineas, total_en_archivo} o {error}. `desdeMs`/`hastaMs` 0 = sin límite.
    json bitacora(const std::string& carpeta, std::int64_t desdeMs, std::int64_t hastaMs, const std::set<std::string>& tipos,
                  const std::string& texto, std::size_t limite) const;
    /// Las grabaciones (con capítulos), los vivos y las transcripciones, con `desde`/`hasta`
    /// legibles. {carpeta, grabaciones, transmisiones, transcripciones} o {error}.
    json grabaciones(const std::string& carpeta) const;
    /// Los archivos de la carpeta del evento: [{archivo, bytes}] o {error}.
    json archivos(const std::string& carpeta) const;

    /// La foto `foto` (inicio | cierre | ultima; vacío = ultima) de Config/ del evento:
    /// {hora, archivos:[{archivo, bytes}]} o {error}.
    json config(const std::string& carpeta, const std::string& foto) const;
    /// El contenido de un archivo de esa foto: {archivo, contenido} o {error}.
    json contenidoConfig(const std::string& carpeta, const std::string& foto, const std::string& archivo) const;
    /// Qué cambió entre la foto de un evento y otra (`otro` vacío y `otraFoto` "actual": la
    /// configuración de ahora). Se comparan fotos de la misma configuración (cada una lee
    /// sus contenidos de su Eventos/.objetos). {cambios, archivos_distintos} o {error}.
    json comparar(const std::string& carpeta, const std::string& foto, const std::string& otro,
                  const std::string& otraFoto) const;

    /// Copia el evento (y lo que nombran sus fotos) a `destino` (vacío = Exportados/ al lado
    /// de Eventos/ de su configuración). "" o el error; `copia`: dónde quedó.
    std::string exportar(const std::string& carpeta, const std::filesystem::path& destino, bool conGrabaciones,
                         std::filesystem::path* copia) const;
    /// Exportados/ de la abierta.
    std::filesystem::path carpetaDeExportar() const { return eventos_.parent_path() / "Exportados"; }
    /// Verifica los sellos (relee todo: con grabaciones grandes, tarda). {ok, …} o {error}.
    json sellos(const std::string& carpeta, const std::atomic<bool>* cancelar = nullptr) const;

   private:
    std::filesystem::path configuraciones_, eventos_, config_;
    std::string activa_;
};

} // namespace ba::app
