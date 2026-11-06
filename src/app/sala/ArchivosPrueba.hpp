// ============================================================================
// src/app/sala/ArchivosPrueba.hpp — el disco y el probador de las pruebas de sala y de asamblea
//
// La Prueba de sala y la de asamblea (ui/PruebaSalaViewModel, ui/PruebaAsambleaViewModel)
// viven en el hilo de la interfaz: lo que toca el disco (crear carpetas, escribir el archivo
// de credenciales, leer el informe de hinchada, borrar) va acá y se llama SOLO desde un hilo
// del pool (QThreadPool), con el resultado de vuelta en la interfaz por una cola. Son
// funciones sin estado: se prueban solas (test_archivos_prueba).
//
// `soltarProceso`: el probador es un hijo nuestro; al cerrar el programa se corta sin
// esperarlo (kill y listo, nunca waitForFinished): ver el .cpp para por qué no queda ni
// zombi ni huérfano.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class QObject;
class QProcess;

namespace ba::app::sala::archivos {

/// Crea las carpetas (con las de arriba). "" o el primer error. En el pool.
std::string crearCarpetas(const std::vector<std::filesystem::path>& carpetas);

/// Escribe las credenciales del probador en `archivo` (creando su carpeta), solo legibles por
/// el dueño. "" o el error. En el pool.
std::string escribirCredenciales(const std::filesystem::path& archivo, const std::string& contenido);

/// Lo que dejó hinchada en <salida>/hinchada/informe.json, para el informe de la app:
/// {carpeta, asamblea, integrantes, resumen, notas}; objeto vacío si no hay o no se entiende.
/// En el pool.
nlohmann::json informeHinchada(const std::filesystem::path& salida);

/// Lo nuevo de un archivo de texto que otro va escribiendo (la consola del coordinador de
/// hinchada): desde el byte `desde` hasta el último fin de línea (lo que quedó a medio
/// escribir se lee la próxima vez), `maxBytes` como mucho. `hasta`: desde dónde seguir. Sin
/// el archivo, nada (y `hasta` = `desde`). En el pool.
struct Tramo {
    std::string texto;
    std::int64_t hasta = 0;
};
Tramo leerLineasDesde(const std::filesystem::path& archivo, std::int64_t desde, std::int64_t maxBytes = 1 << 20);

/// Borra un archivo (si no está, nada). En el pool.
void borrar(const std::filesystem::path& archivo);

/// Lee un archivo de la cola de escritura (lo último encolado o lo del disco) como JSON;
/// null si no hay. En el pool.
nlohmann::json leerJsonEncolado(const std::filesystem::path& archivo);

/// Corta el probador SIN esperarlo (para un destructor en el hilo de la interfaz): lo saca del
/// árbol de objetos (que el destructor del dueño no lo destruya: ~QProcess sí espera), le corta
/// las señales hacia `dueno`, lo mata y se borra solo cuando el sistema avisa que terminó.
void soltarProceso(QProcess* p, QObject* dueno);

} // namespace ba::app::sala::archivos
