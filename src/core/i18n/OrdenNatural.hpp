// OrdenNatural.hpp — ordenar como una persona: «banca 2» antes que «banca 10», «cuadro9» antes
// que «cuadro10».
//
// QCollator::setNumericMode lo hace solo si Qt tiene ICU (o el sistema lo da: macOS, Windows).
// El Qt de Linux del bundle no tiene ICU: ahí setNumericMode no hace nada (avisa «not supported»)
// y la lista de bancas salía 1, 10, 2 (y la secuencia de cuadros de un gráfico, desordenada). Acá
// los tramos de dígitos se comparan por su valor en todas las plataformas y el texto entre ellos,
// con la comparación que se pase (la del idioma: QCollator).
//
//   QCollator col;
//   ba::i18n::compararNatural(u16(a), u16(b), [&](auto x, auto y) { return col.compare(vista(x), vista(y)); })
#pragma once

#include <cstddef>
#include <string_view>

namespace ba::i18n {

inline bool esDigitoAscii(char16_t c) { return c >= u'0' && c <= u'9'; }

/// < 0, 0 o > 0. `texto(a, b)` compara dos tramos sin dígitos (otra vez < 0, 0, > 0).
template <class CompararTexto>
int compararNatural(std::u16string_view a, std::u16string_view b, CompararTexto&& texto) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = esDigitoAscii(a[i]), db = esDigitoAscii(b[j]);
        if (da != db) return da ? -1 : 1; // los números antes que las letras (como QCollator)
        std::size_t fi = i, fj = j;
        while (fi < a.size() && esDigitoAscii(a[fi]) == da) ++fi;
        while (fj < b.size() && esDigitoAscii(b[fj]) == db) ++fj;
        const std::u16string_view ta = a.substr(i, fi - i), tb = b.substr(j, fj - j);
        if (da) {
            // Por su valor: sin los ceros de adelante, el más largo es el mayor; si no, dígito a dígito.
            std::size_t ca = 0, cb = 0;
            while (ca + 1 < ta.size() && ta[ca] == u'0') ++ca;
            while (cb + 1 < tb.size() && tb[cb] == u'0') ++cb;
            const std::u16string_view va = ta.substr(ca), vb = tb.substr(cb);
            if (va.size() != vb.size()) return va.size() < vb.size() ? -1 : 1;
            if (const int c = va.compare(vb); c != 0) return c < 0 ? -1 : 1;
        } else if (const int c = texto(ta, tb); c != 0) {
            return c < 0 ? -1 : 1;
        }
        i = fi;
        j = fj;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

} // namespace ba::i18n
