// ============================================================================
// src/app/agent/Mirada.hpp — mostrarle UNA imagen a un modelo de visión
//
// Un pedido suelto, sin estado, a un servidor con la API de OpenAI: una imagen,
// una pregunta, un texto. POST <url>/chat/completions con la imagen en una data
// URL. Los servidores (`FuenteMirada`):
//   - el MODELO LOCAL con su proyector de visión: el llama-server del programa
//     (agent/ServidorModeloLocal, con --mmproj) en 127.0.0.1. La imagen no sale
//     de la máquina;
//   - el MODELO PROPIO del operador (Config/modelo-propio.json, con la credencial
//     "modelo-propio-api-key"): su servidor, donde sea que esté.
//
// Lo usan `imagen_preguntar` del MCP (mirar el programa, una fuente, la
// interfaz; con el propio) y el tótem de registro (leer el dorso de un DNI, la
// zona de lectura mecánica, ver documentos/Mrz; primero el local). Cada uno con
// su pedido: la pregunta, qué parte de la imagen (para la MRZ, la franja de
// abajo), cuánto achicarla, cuántos tokens.
//
// POR QUE DIRECTO Y NO POR UN AGENTE: arrancar un agente cuesta minutos; un
// pedido suelto contesta en segundos, y la imagen no entra a ninguna
// conversación. Ver el comentario de `imagen_preguntar`.
//
// BLOQUEA mientras espera la respuesta: `mirarCon` nunca en el hilo de la
// interfaz; para eso está `mirarAparte`, que corre en un hilo suyo y contesta en
// el de la interfaz.
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

class QImage;
class QObject;

namespace ba::app::agent {

/// A qué servidor se le pregunta.
struct FuenteMirada {
    std::string url;    ///< la base, con /v1 si el servidor lo usa ("http://127.0.0.1:8123/v1")
    std::string clave;  ///< "" = sin Authorization (el local)
    std::string modelo; ///< el id que pide el servidor ("local" es el alias del llama-server)
    std::string nombre; ///< para decir quién miró: "modelo local", "modelo propio"
};

struct PedidoMirada {
    std::string sistema;  ///< el mensaje de sistema ("" = sin mensaje de sistema)
    std::string pregunta; ///< el texto que acompaña la imagen
    /// Solo la franja de abajo, desde esta fracción del alto (0 = la imagen entera).
    /// Para la MRZ del dorso, 0.58: los renglones quedan grandes y no se distrae.
    double recorteDesde = 0.0;
    int anchoMax = 768;   ///< se achica a este ancho (no se agranda; 0 = como venga)
    int calidad = 85;     ///< del JPEG
    int maxTokens = 300;
    int esperaMs = 0;     ///< tope de espera; 0 = el ajuste mcp.imagenTimeoutS
};

/// El cuerpo del pedido (la imagen va ya codificada en `jpegBase64`). Puro, para
/// probarlo: modelo, mensajes ({sistema} si hay, y [pregunta, imagen]),
/// max_tokens, temperatura 0 y sin pensar (para mirar no hace falta, y en vivo el
/// tiempo es lo que importa).
nlohmann::json cuerpoMirada(const std::string& modelo, const PedidoMirada& p, const std::string& jpegBase64);
/// El pedido para leer el DORSO de un documento (la zona de lectura mecánica):
/// documentos::pedidoMrz, sin sistema, la franja de abajo desde el 58 % del alto,
/// sin achicar una foto de 1920, JPEG 95, 200 tokens, 20 s. Lo que anduvo en la
/// prueba con un DNI real (unos 6 s por foto con el modelo local en una M1 Pro).
PedidoMirada pedidoDelDorso();
/// El rectángulo que se manda de una imagen de ancho x alto (la franja de
/// `recorteDesde`): {x, y, ancho, alto}. Puro.
void recorteMirada(int ancho, int alto, const PedidoMirada& p, int& x, int& y, int& w, int& h);

/// El modelo propio como fuente, si está configurado y con su clave (lee los
/// archivos: barato, pero no para cada cuadro). false y `error` si no.
bool fuentePropia(const std::filesystem::path& dataRoot, FuenteMirada& out, std::string* error = nullptr);
bool modeloPropioListo(const std::filesystem::path& dataRoot);

/// El texto que contestó, o "" y el motivo en `error`. BLOQUEA (ver arriba).
std::string mirarCon(const FuenteMirada& f, const QImage& img, const PedidoMirada& p, std::string& error);
/// Lo mismo con el modelo propio (lo que hace `imagen_preguntar`).
std::string mirarConElModeloPropio(const std::filesystem::path& dataRoot, const QImage& img, const PedidoMirada& p,
                                   std::string& error);

/// `mirarCon` en un hilo propio; `luego(texto, error)` se llama en el hilo de la
/// interfaz, y no se llama si `contexto` ya no existe.
using RespuestaMirada = std::function<void(std::string texto, std::string error)>;
void mirarAparte(FuenteMirada f, const QImage& img, PedidoMirada p, QObject* contexto, RespuestaMirada luego);

} // namespace ba::app::agent
