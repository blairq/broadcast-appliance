// ============================================================================
// src/app/tests/test_documento_dorso.cpp — el dorso leído por el modelo propio
//
//   1. El armado del pedido (agent/Mirada): el del dorso (pedidoDelDorso: la
//      pregunta de la prueba real, sin sistema, la franja de abajo, JPEG alto,
//      200 tokens), su cuerpo (modelo, la imagen en data URL, temperatura 0, sin
//      pensar) y el recorte de un cuadro de 1920x1080; con sistema, va primero.
//   2. Lo que contesta el modelo, por el mismo camino que el tótem
//      (interpretarRespuestaMrz y después el catálogo, como LectorDocumentos):
//      bien, con errores corregibles, inventado (no cierra), «NO SE VE», una
//      descripción en vez de los renglones, el dorso de otro país con un campo
//      que solo acepta el DNI, y el criterio de dos fotos (ConfirmadorMrz).
//
// SIN RED: el pedido HTTP no se hace (eso es Mirada.cpp, con Qt y HTTP).
// TODAS LAS MRZ SON SINTÉTICAS.
// ============================================================================
#include <cstdio>
#include <string>

#include "agent/Mirada.hpp"
#include "documentos/Documento.hpp"
#include "documentos/Mrz.hpp"

using namespace ba::documentos;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

constexpr int kAnio = 2026;

std::string relleno(std::string s, std::size_t n) {
    s.resize(n, '<');
    return s;
}

std::string armar(const std::string& emisor, const std::string& numero9, const std::string& nac, char sexo,
                  const std::string& venc, const std::string& nombre) {
    const std::string num = relleno(numero9, 9);
    const std::string l1 = "ID" + emisor + num + std::to_string(digitoMrz(num)) + std::string(15, '<');
    std::string l2 = nac + std::to_string(digitoMrz(nac)) + sexo + venc + std::to_string(digitoMrz(venc)) + emisor +
                     std::string(11, '<');
    l2 += std::to_string(digitoMrz(l1.substr(5, 25) + l2.substr(0, 7) + l2.substr(8, 7) + l2.substr(18, 11)));
    return l1 + "\n" + l2 + "\n" + relleno(nombre, 30);
}

/// Lo que haría LectorDocumentos::leerDorso con la respuesta del modelo.
bool leerRespuesta(const std::string& respuesta, const std::vector<std::string>& aceptados, Datos& d,
                   LecturaMrz& l) {
    l = interpretarRespuestaMrz(respuesta, kAnio);
    if (l.estado != LecturaMrz::Estado::Leida) return false;
    Catalogo cat;
    return cat.leer(Codigo{kFormatoMrz, l.texto, {}}, aceptados, d);
}

void pedido() {
    using namespace ba::app::agent;
    const PedidoMirada p = pedidoDelDorso();
    CHECK(p.sistema.empty() && p.pregunta == pedidoMrz(), "la pregunta de la prueba real, sin sistema");
    CHECK(p.maxTokens == 200 && p.calidad >= 90 && p.esperaMs > 0, "200 tokens, JPEG alto, con tope");
    int x = 0, y = 0, w = 0, h = 0;
    recorteMirada(1920, 1080, p, x, y, w, h);
    CHECK(x == 0 && w == 1920 && y == 626 && h == 1080 - 626, "la franja de abajo, desde el 58 %, ancho completo");
    CHECK(p.anchoMax >= 1920, "una foto de 1920 no se achica");
    const json b = cuerpoMirada("local", p, "QUJD");
    CHECK(b["model"] == "local", "el modelo (el alias del llama-server)");
    CHECK(b["messages"].size() == 1 && b["messages"][0]["role"] == "user", "sin sistema: un solo mensaje");
    const json& c = b["messages"][0]["content"];
    CHECK(c.size() == 2 && c[0]["type"] == "text" && c[0]["text"] == pedidoMrz(), "la pregunta");
    CHECK(c[1]["type"] == "image_url" && c[1]["image_url"]["url"] == "data:image/jpeg;base64,QUJD",
          "la imagen en data URL");
    CHECK(b["max_tokens"] == 200 && b["temperature"] == 0, "max_tokens y temperatura 0");
    CHECK(b["chat_template_kwargs"]["enable_thinking"] == false, "sin pensar");
    CHECK(pedidoMrz().find("30 caracteres") != std::string::npos && pedidoMrz().find("NO SE VE") != std::string::npos &&
              pedidoMrz().find("EXACTAMENTE") != std::string::npos,
          "pide exactitud, 30 caracteres y NO SE VE");
    PedidoMirada con;
    con.sistema = "Sos un lector.";
    const json b2 = cuerpoMirada("m", con, "x");
    CHECK(b2["messages"].size() == 2 && b2["messages"][0]["role"] == "system", "con sistema, va primero");
    CHECK(con.anchoMax == 768 && con.esperaMs == 0 && con.recorteDesde == 0.0, "por defecto, lo de imagen_preguntar");
    recorteMirada(1920, 1080, con, x, y, w, h);
    CHECK(y == 0 && h == 1080, "sin recorte, entera");
}

void respuestas() {
    const std::string dni = armar("ARG", "11222333", "900315", 'F', "340420", "PRUEBA<<JUANA<ANDREA");
    Datos d;
    LecturaMrz l;
    CHECK(leerRespuesta(dni, {"dni-ar"}, d, l) && d.tipo == "dni-ar" && d.numero == "11222333" &&
              d.apellido == "Prueba",
          "buena");
    // Como contesta un modelo: en un bloque, con espacios, una O por 0 y una I por 1.
    std::string mal = dni;
    mal[6] = 'I';       // número: 1 -> I
    mal[31 + 1] = 'O';  // nacimiento: 0 -> O
    CHECK(leerRespuesta("```\n" + mal.substr(0, 14) + " " + mal.substr(14) + "\n```", {"dni-ar"}, d, l) &&
              d.numero == "11222333" && d.nacimiento == "1990-03-15",
          "con errores corregibles");
    std::string inventada = dni;
    inventada[10] = '9'; // un dígito del número cambiado
    CHECK(!leerRespuesta(inventada, {"dni-ar"}, d, l) && l.estado == LecturaMrz::Estado::NoCierra,
          "inventada: no cierra, no se acepta");
    CHECK(!leerRespuesta("NO SE VE", {"dni-ar"}, d, l) && l.estado == LecturaMrz::Estado::NoSeVe, "NO SE VE");
    CHECK(!leerRespuesta("Veo un documento de identidad argentino sobre una mesa.", {"dni-ar"}, d, l) &&
              l.estado == LecturaMrz::Estado::NoCierra,
          "una descripción");
    const std::string uy = armar("URY", "12345672", "880229", 'M', "310101", "EJEMPLO<<MARTIN");
    CHECK(!leerRespuesta(uy, {"dni-ar"}, d, l) && l.estado == LecturaMrz::Estado::Leida,
          "otro país con un campo de DNI: se lee pero no es de los aceptados");
    CHECK(leerRespuesta(uy, {"dni-ar", "mrz-td1"}, d, l) && d.tipo == "mrz-td1" && d.pais == "UY",
          "con mrz-td1 aceptado, sí");
    // Dudosa (el compuesto no cierra): sola no alcanza; con otra foto que coincide, sí.
    std::string corrida = dni;
    corrida[31 + 25] = 'B'; // un opcional del renglón 2 que no estaba: el compuesto no cierra
    l = interpretarRespuestaMrz(corrida, kAnio);
    CHECK(l.estado == LecturaMrz::Estado::Dudosa, "un opcional corrido: dudosa");
    ConfirmadorMrz conf;
    CHECK(!conf.agregar(l), "la primera dudosa no se acepta");
    CHECK(conf.agregar(interpretarRespuestaMrz(corrida, kAnio)), "la segunda que coincide, sí");
    // Lo que se puede mostrar afuera (MCP): tipo, nombre y los últimos tres dígitos.
    leerRespuesta(dni, {"dni-ar"}, d, l);
    const json r = resumenParaAfuera(d.aJson());
    CHECK(r.value("numero", std::string()) == "…333" && !r.contains("crudo") && !r.contains("nacimiento"),
          "resumido para afuera");
}

} // namespace

int main() {
    pedido();
    respuestas();
    if (g_fallos == 0) std::printf("test_documento_dorso: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
