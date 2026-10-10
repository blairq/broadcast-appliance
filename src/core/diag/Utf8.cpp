// src/core/diag/Utf8.cpp — ver Utf8.hpp
#include "diag/Utf8.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ba::diag {

namespace {
/// Cuántos bytes ocupa la secuencia UTF-8 que empieza en s[i] (0 si es inválida).
size_t largo(std::string_view s, size_t i) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t n = 0;
    unsigned min = 0;
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) {
        n = 2;
        min = 0x80;
    } else if ((c >> 4) == 0xE) {
        n = 3;
        min = 0x800;
    } else if ((c >> 3) == 0x1E) {
        n = 4;
        min = 0x10000;
    } else {
        return 0;
    }
    if (i + n > s.size()) return 0;
    unsigned cp = c & (0xFF >> (n + 1));
    for (size_t k = 1; k < n; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b >> 6) != 0x2) return 0;
        cp = (cp << 6) | (b & 0x3F);
    }
    // Ni secuencias largas de más, ni sustitutos, ni más allá de U+10FFFF.
    if (cp < min || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return 0;
    return n;
}
} // namespace

bool esUtf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const size_t n = largo(s, i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

std::string utf8Valido(std::string_view s) {
    if (esUtf8(s)) return std::string(s);
#if defined(_WIN32)
    // De la página de códigos del sistema (lo que dan FormatMessage, los drivers…).
    const int nw = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (nw > 0) {
        std::wstring w(static_cast<size_t>(nw), L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), w.data(), nw);
        const int n8 = WideCharToMultiByte(CP_UTF8, 0, w.data(), nw, nullptr, 0, nullptr, nullptr);
        if (n8 > 0) {
            std::string o(static_cast<size_t>(n8), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), nw, o.data(), n8, nullptr, nullptr);
            return o;
        }
    }
#endif
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t n = largo(s, i);
        if (n == 0) {
            o.push_back('?');
            ++i;
        } else {
            o.append(s.substr(i, n));
            i += n;
        }
    }
    return o;
}

} // namespace ba::diag
