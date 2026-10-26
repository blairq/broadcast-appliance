// ============================================================================
// src/core/documentos/lector/LectorCodigos.hpp — leer códigos de barras de una imagen
//
// PDF417 (el del DNI argentino y las licencias AAMVA), QR, DataMatrix, Aztec,
// Code128 y Code39, con zxing-cpp (Apache-2.0; lee bien el PDF417, que zbar no).
// Se construye desde la fuente en los tres sistemas, con versión y hash fijos
// (cmake/BaZxing.cmake, `make deps-zxing`). Es otra biblioteca (`ba_codigos`) y
// no parte de ba_core: el launcher y el gateway no la necesitan.
//
// PESADO: buscar un PDF417 en un cuadro de 1280x720 con «a fondo» tarda decenas
// de milisegundos. Nunca en el hilo de la interfaz (ver ui/LectorDocumentos en
// la app). Sin estado: se puede llamar desde cualquier hilo, a la vez.
//
// ESPEJADO TAMBIÉN: la cámara de adelante de una tablet a veces llega dada
// vuelta, y el lector de PDF417 y el de QR de zxing ya prueban el espejo solos
// (test_codigos lo verifica): no hace falta una segunda pasada.
//
// Devuelve el código crudo ({formato, texto, bytes}); qué documento es lo decide
// documentos/Documento (los tipos).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "documentos/Documento.hpp"

namespace ba::documentos {

struct OpcionesLector {
    /// Los formatos a buscar, con los nombres de zxing ("PDF417", "QRCode"…).
    /// Vacío = los de `formatosDeFabrica()`.
    std::vector<std::string> formatos;
    /// Más lento y más tolerante (girado, chico, borroso): para una cámara.
    bool aFondo = true;
    /// A lo sumo cuántos códigos por imagen.
    int maximo = 4;
};

/// Los que se buscan si no se dice nada: PDF417, QRCode, DataMatrix, Aztec, Code128, Code39.
const std::vector<std::string>& formatosDeFabrica();
/// Si `formato` es un nombre que zxing entiende.
bool formatoValido(const std::string& formato);

/// Los códigos de un cuadro BGRA (4 bytes por píxel; `bytesPorFila` 0 = ancho*4).
std::vector<Codigo> leerCodigos(const std::uint8_t* bgra, int ancho, int alto, int bytesPorFila = 0,
                                const OpcionesLector& o = {});
/// Lo mismo con una imagen en grises (1 byte por píxel).
std::vector<Codigo> leerCodigosGris(const std::uint8_t* gris, int ancho, int alto, int bytesPorFila = 0,
                                    const OpcionesLector& o = {});

/// Dibuja un código (gris: 0 = negro, 255 = blanco) de al menos anchoMin x altoMin,
/// con margen. Para los tests de punta a punta y para mostrar un ejemplo. El
/// texto va como ISO-8859-1 si se puede (como el del DNI). false y `error` si no.
bool generarCodigo(const std::string& formato, const std::string& texto, int anchoMin, int altoMin,
                   std::vector<std::uint8_t>& gris, int& ancho, int& alto, std::string* error = nullptr);

} // namespace ba::documentos
