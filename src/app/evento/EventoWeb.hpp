// SPDX-License-Identifier: GPL-2.0-or-later
// ============================================================================
// src/app/evento/EventoWeb.hpp — La página web del evento en vivo
//
// Cada evento puede tener su sitio: una página por SECCIÓN (bloque) del guion,
// una portada (antes de empezar) y un cierre (al terminar). Viven en la carpeta
// del evento, en Web/, así que son parte del evento y quedan en el historial
// (un evento nuevo tiene la suya y no pisa la de otro):
//
//   <evento>/Web/
//     sitio.json            título, bajada, colores, subdominio, barra en vivo
//     estilo.css            el estilo común (se puede cambiar a gusto)
//     portada.html          antes de empezar: qué es, cuándo, el programa
//     seccion-<bloque>.html una por bloque del guion
//     cierre.html           cuando terminó
//     (y lo que se quiera agregar: imágenes, logos, otros .html)
//
// La sala (gateway) la sirve en la raíz de la dirección pública mientras el
// evento está abierto: la página de la sección que se está tratando, que cambia
// sola cuando el guion avanza (ver assets/web/evento). Los .html son del
// usuario: se generan una vez y después se editan a mano o con el asistente
// (`generar` no pisa lo que ya existe salvo que se lo pida).
//
// Lo que se publica del estado en vivo (`estadoPublico`) es SOLO lo que puede
// ver cualquiera: nombres de las secciones, en cuál se está, horarios. Nunca el
// texto de los cues ni las notas (son instrucciones para el operador).
// ============================================================================
#pragma once

#include "evento/Evento.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ba::evento {

/// Cómo se ve y se llama el sitio (Web/sitio.json). Las claves desconocidas se conservan.
struct SitioWeb {
    std::string titulo;     ///< vacío = el nombre del evento
    std::string bajada;     ///< una línea abajo del título
    std::string color = "#ff4d2e";
    std::string fondo = "#0b0b0c";
    std::string subdominio; ///< «entrevistapepito» → entrevistapepito.a.pinggy.link
    bool barra = true;      ///< la franja «en vivo» arriba de cada página
    /// Al iniciar el evento, la dirección pública pasa a ser la de su subdominio.
    bool direccionAlIniciar = true;
    /// La página ofrece preguntar y votar (/publico/, con su clave): la sala agrega el
    /// enlace y si hay una votación abierta.
    bool participacion = false;
    /// Dónde se ve en vivo, fijado a mano para este evento (pisa lo que se sabe de las
    /// cuentas): {youtube: URL o id del video/vivo o URL del canal, twitch: canal o URL,
    /// facebook: URL del vivo}.
    nlohmann::json vivos = nlohmann::json::object();
    nlohmann::json resto = nlohmann::json::object();

    static SitioWeb leer(const std::filesystem::path& carpetaWeb, const Evento& e);
    bool guardar(const std::filesystem::path& carpetaWeb) const;
    nlohmann::json aJson() const;
};

/// «Entrevista_Pepito 2026» → «entrevistapepito2026»: lo que acepta pinggy (solo minúsculas y números)
/// (letras, números y guiones; sin acentos ni guiones bajos), hasta 40 letras.
std::string subdominioDe(const std::string& nombre);

/// Dónde están las plantillas del sistema (assets/web/evento/plantilla, en el programa:
/// Web/evento/plantilla). Las pone la app al arrancar; las pruebas, las del repositorio.
void setCarpetaPlantillas(const std::filesystem::path& dir);
std::filesystem::path carpetaPlantillas();

/// Carpeta del sitio de un evento.
inline std::filesystem::path carpetaWeb(const std::filesystem::path& carpetaEvento) { return carpetaEvento / "Web"; }

/// El archivo de la página de un bloque («seccion-<id>.html»; sin id, por número).
std::string paginaDeBloque(const Bloque& b, std::size_t indice);

struct ResultadoWeb {
    std::vector<std::string> escritas; ///< relativas a Web/
    std::vector<std::string> salteadas; ///< ya estaban (no se pisan sin `sobrescribir`)
    std::string error;
};

/// Escribe el sitio del evento (estilo, portada, una página por bloque, cierre).
/// No pisa lo que ya existe salvo `sobrescribir`.
ResultadoWeb generar(const Evento& e, const std::filesystem::path& carpetaWeb, bool sobrescribir);

/// Lo que la app sabe de las cuentas conectadas (lo arma StudioViewModel):
/// {youtube: {liveId, canalId, handle}, twitch: {login, enVivo}, facebook: {liveId, pageId, url}}.
/// Con lo fijado a mano en el sitio, la lista pública de dónde verlo:
/// [{plataforma, nombre, url (para abrir), embed (para un <iframe>), enVivo}].
/// El embed de Twitch necesita `&parent=<el host de la página>` (lo agrega vivo.js).
nlohmann::json armarVivos(const SitioWeb& s, const nlohmann::json& plataformas);

/// El id de un video de YouTube en una URL (watch?v=, youtu.be/, /live/, /embed/) o
/// suelto (11 letras). «» si no hay.
std::string idDeYouTube(const std::string& texto);

/// Lo que se agrega al estado en vivo además del guion.
struct ExtrasVivo {
    nlohmann::json vivos = nlohmann::json::array();
    /// Lo que publica el operador: {texto, nivel: info|urgente, desdeMs}. Vacío = nada.
    nlohmann::json aviso = nlohmann::json::object();
    /// Quién tiene la palabra (el cue de orador que corre), si el guion lo dice.
    std::string habla;
    /// Cuándo empezó de verdad cada sección (índice → ms).
    std::map<int, std::int64_t> inicioReal;
    /// Lo que le falta a la sección actual según lo previsto (-1 = no se sabe).
    int restanteActualSeg = -1;
    /// Las preguntas del público YA MODERADAS: [{texto, nombre, estado: al_aire|elegida|
    /// respondida, votos}]. Nunca las nuevas sin revisar ni las descartadas.
    nlohmann::json preguntas = nlohmann::json::array();
    /// Cuántos están en la sala (invitados adentro e integrantes conectados).
    int enLaSala = 0;
    /// Lo que sabe el resto del sistema y es público (lo arma StudioViewModel; cada uno
    /// puede faltar): aire {transmitiendo, grabando}, oradores {habla: {nombre, bloque,
    /// cargo}, hastaMs (cuándo se le termina el tiempo), asignadoSeg, fila: [{nombre, bloque}]}, ordenDelDia [{n,
    /// titulo, expediente, estado, resultado}], quorum {configurado, presentes, necesarios,
    /// hay}, votacion {pregunta, abierta, modo, votaron, podian, resultado?}, asistencia
    /// {total, presentes}, asamblea {cuerpo, integrantes [{nombre, bloque, cargo, banca,
    /// presente, lugar: sala|remoto, habla}], presentes, total, quorum, punto {n, titulo,
    /// expediente}, votaciones [{asunto, afirmativos, negativos, abstenciones, aprobado}]}.
    nlohmann::json sala = nlohmann::json::object();
};

/// Lo que ve cualquiera del evento en vivo. `estado`: proximo | en_vivo | pausa | terminado.
/// `bloque` = el que se está tratando (-1 = ninguno). `inicioMs` = la hora prevista
/// (0 = sin hora). `arrancoMs` = cuándo se arrancó el guion (0 = no arrancó).
nlohmann::json estadoPublico(const Evento& e, const SitioWeb& s, const std::string& estado, int bloque,
                             std::int64_t inicioMs, std::int64_t arrancoMs, std::int64_t ahoraMs,
                             const nlohmann::json& vivos = nlohmann::json::array());
/// Con lo de más: horarios estimados (si se va atrasado se corren), aviso, quién habla.
/// Cada sección trae `inicioRealMs` (si ya empezó) e `inicioEstimadoMs`.
nlohmann::json estadoPublico(const Evento& e, const SitioWeb& s, const std::string& estado, int bloque,
                             std::int64_t inicioMs, std::int64_t arrancoMs, std::int64_t ahoraMs, const ExtrasVivo& x);

/// Una página del sitio por nombre: «portada», «cierre», el id o el nombre de un
/// bloque, o un archivo .html. Devuelve la ruta relativa a Web/ («» si no hay).
std::string resolverPagina(const Evento& e, const std::string& pedida);

} // namespace ba::evento
