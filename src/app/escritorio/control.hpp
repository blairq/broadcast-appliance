// ============================================================================
// src/app/escritorio/control.hpp — lo que comparten las partes de ba-control
//
// ba_control_main.cpp       el servidor MCP (stdio), los permisos y lo básico:
//                           pantalla, mouse, teclado, ventanas, procesos, comandos
// control_uia.cpp           UI Automation: leer los elementos de una ventana
//                           (botones, campos, enlaces… también las páginas de
//                           Chrome), buscarlos por nombre, hacerles clic,
//                           escribirles, leer el texto, esperar a que aparezcan
// control_chrome.cpp        Chrome y Edge por su protocolo de depuración (CDP):
//                           pestañas, la consola, evaluar JavaScript, navegar
// control_archivos.cpp      archivos (leer, escribir, listar, copiar, mover, a
//                           la papelera), el registro, avisos de Windows
//
// Cada parte da su catálogo (herramientas*) y atiende las suyas (atender*:
// true si era suya). Fuera de Windows compilan y cada herramienta dice que es
// solo para Windows.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bactl {

using json = nlohmann::json;

/// Las capacidades, con su valor de fábrica: APAGADO todo (se prende en
/// Configuración). Prendido el interruptor general, cada capacidad viene
/// habilitada salvo `ejecutar_admin` y `registro` (escribir el registro), que
/// hay que prender aparte.
struct Permisos {
    bool habilitado = false;
    bool pantalla = true, mouse = true, teclado = true, ejecutar = true, ejecutar_admin = false, buscar = true,
         ventanas = true, portapapeles = true, procesos = true, comandos = true, sistema = true;
    bool archivos = true, archivos_escribir = true, navegador = true, registro = false, avisos = true;
};

json resultadoTexto(const json& id, const std::string& texto, bool error = false);
json resultadoImagen(const json& id, const std::string& base64, const std::string& texto);
json resultadoDatos(const json& id, const json& datos);
std::string base64(const unsigned char* d, size_t n);
/// {"type": "object", "properties": props (siempre un objeto), "required": req}
json esquema(json props, std::vector<std::string> req = {});
/// Una herramienta para tools/list.
json herramienta(const char* nombre, const char* descripcion, json esquema);
/// «El control de Windows no tiene permiso para «que»…» (error).
json sinPermiso(const json& id, const char* que);

// Los catálogos y los que atienden de cada parte.
json herramientasUia();
json herramientasChrome();
json herramientasArchivos();
bool atenderUia(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out);
bool atenderChrome(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out);
bool atenderArchivos(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out);

/// La carpeta de datos de la máquina (BA_DATA_ROOT; vacío si no hay).
std::string dataRoot();

#if defined(_WIN32)
std::wstring aW(const std::string& s);
std::string aU(const std::wstring& w);

struct Ventana {
    HWND hwnd;
    std::string titulo;
    DWORD pid;
    RECT rect;
};
std::vector<Ventana> ventanas();
/// Por `id` (de windows_ventanas) o `titulo` (parte del título); nullptr si no está.
HWND buscarVentana(const json& a);
std::string tituloDe(HWND h);
/// «en «título» (id N)»
std::string donde(HWND h);
bool traerAlFrente(HWND h);
/// El ejecutable de un proceso («chrome.exe»), o vacío.
std::string ejecutableDe(DWORD pid);
void escribirTexto(const std::wstring& w);
void mouseAbsoluto(int x, int y);
void mouseBoton(const std::string& boton, bool abajo);
/// Una combinación como «ctrl+a»; false si no se entendió.
bool mandarTeclas(const std::string& combo);
#endif

} // namespace bactl
