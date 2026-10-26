// ============================================================================
// src/core/documentos/lector/LectorCodigos.cpp — ver LectorCodigos.hpp
// ============================================================================
#include "documentos/lector/LectorCodigos.hpp"

#include <exception>

#include "BitMatrix.h"
#include "CharacterSet.h"
#include "MultiFormatWriter.h"
#include "ReadBarcode.h"

namespace ba::documentos {
namespace {

ZXing::BarcodeFormats formatosDe(const std::vector<std::string>& nombres) {
    ZXing::BarcodeFormats fs;
    for (const std::string& n : nombres.empty() ? formatosDeFabrica() : nombres) {
        try {
            const ZXing::BarcodeFormat f = ZXing::BarcodeFormatFromString(n);
            if (f != ZXing::BarcodeFormat::None) fs |= f;
        } catch (const std::exception&) {
            // Un nombre que zxing no conoce: se ignora (formatoValido lo dice antes).
        }
    }
    return fs;
}

std::vector<Codigo> leer(const ZXing::ImageView& img, const OpcionesLector& o) {
    ZXing::ReaderOptions op;
    op.setFormats(formatosDe(o.formatos));
    op.setTryHarder(o.aFondo);
    op.setTryRotate(true);
    op.setTryDownscale(true);
    op.setTryInvert(false); // un documento es oscuro sobre claro: invertir duplica el trabajo por nada
    op.setMaxNumberOfSymbols(static_cast<std::uint8_t>(o.maximo < 1 ? 1 : o.maximo > 255 ? 255 : o.maximo));
    std::vector<Codigo> out;
    for (const auto& b : ZXing::ReadBarcodes(img, op)) {
        if (!b.isValid()) continue;
        Codigo c;
        c.formato = ZXing::ToString(b.format());
        c.texto = b.text();
        c.bytes.assign(b.bytes().begin(), b.bytes().end());
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Codigo> leerImagen(const std::uint8_t* p, int ancho, int alto, int fila, int bpp, ZXing::ImageFormat formato,
                               const OpcionesLector& o) {
    if (p == nullptr || ancho <= 0 || alto <= 0) return {};
    if (fila <= 0) fila = ancho * bpp;
    try {
        return leer(ZXing::ImageView(p, ancho, alto, formato, fila), o);
    } catch (const std::exception&) {
        return {}; // una imagen que zxing no acepta no es motivo para tirar la app
    }
}

} // namespace

const std::vector<std::string>& formatosDeFabrica() {
    static const std::vector<std::string> v{"PDF417", "QRCode", "DataMatrix", "Aztec", "Code128", "Code39"};
    return v;
}

bool formatoValido(const std::string& formato) {
    try {
        return ZXing::BarcodeFormatFromString(formato) != ZXing::BarcodeFormat::None;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<Codigo> leerCodigos(const std::uint8_t* bgra, int ancho, int alto, int bytesPorFila, const OpcionesLector& o) {
    return leerImagen(bgra, ancho, alto, bytesPorFila, 4, ZXing::ImageFormat::BGRA, o);
}

std::vector<Codigo> leerCodigosGris(const std::uint8_t* gris, int ancho, int alto, int bytesPorFila,
                                    const OpcionesLector& o) {
    return leerImagen(gris, ancho, alto, bytesPorFila, 1, ZXing::ImageFormat::Lum, o);
}

bool generarCodigo(const std::string& formato, const std::string& texto, int anchoMin, int altoMin,
                   std::vector<std::uint8_t>& gris, int& ancho, int& alto, std::string* error) {
    try {
        const ZXing::BarcodeFormat f = ZXing::BarcodeFormatFromString(formato);
        if (f == ZXing::BarcodeFormat::None) {
            if (error != nullptr) *error = "formato desconocido: " + formato;
            return false;
        }
        ZXing::MultiFormatWriter w(f);
        w.setMargin(10);
        if (f == ZXing::BarcodeFormat::PDF417) w.setEncoding(ZXing::CharacterSet::ISO8859_1);
        const ZXing::BitMatrix m = w.encode(texto, anchoMin, altoMin);
        const auto img = ZXing::ToMatrix<std::uint8_t>(m);
        ancho = img.width();
        alto = img.height();
        gris.assign(img.data(), img.data() + static_cast<std::size_t>(ancho) * alto);
        return ancho > 0 && alto > 0;
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return false;
    }
}

} // namespace ba::documentos
