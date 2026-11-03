// SPDX-License-Identifier: GPL-2.0-or-later
//
// PruebaAsambleaReglas: las reglas de la «Prueba de asamblea» (docs/funciones/PRUEBA-ASAMBLEA.md),
// sin Qt ni sala, para que se prueben solas (tests/test_prueba_asamblea_reglas.cpp). Las usan
// la tarjeta de Asamblea → Conexiones remotas y el MCP (prueba_asamblea_*) por igual, a
// través de ui/PruebaAsambleaViewModel.
//
// QUÉ PROTEGE. La prueba crea integrantes en el Cuerpo, les da acceso a la sesión remota y
// los hace votar: todo eso va al padrón, a Quórum y al acta del evento. Por eso:
//   - solo corre en un evento SIN integrantes reales (puedeGenerar): si el padrón del evento
//     abierto tiene alguno que no es de prueba, no arranca y pide un evento de prueba (la
//     tarjeta ofrece crear uno vacío y abrirlo). Nunca toca un padrón real, ni le suma gente.
//   - los que crea van marcados `prueba: true` (asamblea/Cuerpo::esDePrueba), con id, banca
//     y nombre propios que no chocan con nada (integrantesNuevos);
//   - al terminar (o al arrancar el programa, si quedó una a medias) se revoca el acceso y se
//     borran SOLO los marcados (aBorrar), y el recinto vuelve a como estaba.
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace ba::app::asamblea::prueba {

using json = nlohmann::json;

/// Cuántos integrantes de prueba como mucho (por la máquina del probador y la sala; el
/// límite técnico de la sesión remota es 4096).
constexpr int kMaxIntegrantes = 500;

/// Lo que hacen los simulados (las opciones de hinchada --servidor integrantes).
struct Comportamiento {
    int afueraPct = 20;          ///< dicen estar afuera
    int pidenPalabraPct = 30;    ///< habilitados, piden la palabra
    int votanPct = 90;           ///< votan si hay una votación abierta
    std::string voto = "azar";   ///< «azar» o «afirmativo:60,negativo:30,abstencion:10»
    int reconectanPct = 10;      ///< se les corta la red una vez y vuelven
    int pinErradoPct = 10;       ///< prueban primero con el PIN errado
    int pinErradoIntentos = 5;   ///< 5: el gateway los bloquea 15 minutos
    int conUsuarioPct = 20;      ///< entran con usuario y contraseña en vez del link
    int entradaCadaMs = 1000;    ///< entrada escalonada
    int habilitarSolosS = 0;     ///< > 0: la prueba los habilita sola a los N s (0: el operador, en el Recinto)
    bool programa = true;        ///< habilitados, reciben el programa (como la página)
};
/// Lee el comportamiento (las claves de arriba en snake_case: afuera_pct, piden_palabra_pct,
/// votan_pct, voto, reconectan_pct, pin_errado_pct, pin_errado_intentos, con_usuario_pct,
/// entrada_cada_ms, habilitar_solos_s, programa); lo que falta queda de fábrica. "" o el error.
std::string comportamientoDeJson(const json& j, Comportamiento& c);
json aJson(const Comportamiento& c);

/// Los del padrón que NO son de prueba.
int integrantesReales(const json& integrantes);
/// Los ids de los que son de prueba (lo único que se borra al terminar).
std::vector<std::string> aBorrar(const json& integrantes);
/// ¿Se pueden crear `n` de prueba en este padrón? "" o por qué no (hay reales, n fuera de rango).
std::string puedeGenerar(const json& integrantes, int n);
/// Los campos de `n` integrantes de prueba nuevos ({id, nombre, banca, cargo_oficial, bloque,
/// prueba: true}): ids «prueba-01»…, bancas «P01»… y nombres «Prueba 01»… que no chocan con
/// los que ya hay (se corre la numeración).
json integrantesNuevos(const json& integrantes, int n);
/// El recinto para `n` bancas: {filas, columnas, auto: true} (una grilla más ancha que alta).
json recintoPara(int n);
/// El usuario para entrar tipeando de un integrante de prueba («prueba01»…).
std::string usuarioDe(const std::string& banca);

/// El archivo que lee hinchada (--integrantes): {version, integrantes: [{id, nombre, banca,
/// link, pin, usuario?, clave?}]}. `accesos`: lo que devolvió dar el acceso a cada uno.
json archivoHinchada(const json& accesos);
/// Los argumentos de hinchada para la prueba (sin el ejecutable): --servidor integrantes…
std::vector<std::string> argumentosHinchada(const Comportamiento& c, const std::string& url, const std::string& archivo,
                                            int minutos, const std::string& salida, const std::string& grabado);
/// La línea para correrlo desde otra máquina (con el archivo copiado al lado).
std::string comandoExterno(const Comportamiento& c, const std::string& url, int minutos);

/// Cómo va, de lo que sabe la app: `participantes` (SesionRemota::participantes) y la
/// votación (SesionRemota::votacion), contando solo a los de `ids`.
/// {pedidos, conectados, esperando, habilitados, afuera, bloqueados, enLaFila, conPalabra, votaron, votacion}
json vivo(const json& participantes, const json& votacion, const std::vector<std::string>& ids, int pedidos);
/// La nota de una línea del informe final.
std::string nota(const json& vivoFinal, const json& hinchada);

} // namespace ba::app::asamblea::prueba
