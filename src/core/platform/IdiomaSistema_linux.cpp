// ============================================================================
// src/core/platform/IdiomaSistema_linux.cpp — ver IdiomaSistema.hpp
//
// Las variables de siempre, en el orden en que las mira gettext: LANGUAGE (una
// lista con «:»), LC_ALL, LC_MESSAGES, LANG. «es_AR.UTF-8@euro» → «es-AR».
// ============================================================================
#include "platform/IdiomaSistema.hpp"

#include <algorithm>
#include <cstdlib>

namespace ba::platform {
namespace {

std::string aBcp47(std::string s) {
    for (const char corte : {'.', '@'}) {
        const auto p = s.find(corte);
        if (p != std::string::npos) s.resize(p);
    }
    for (char& c : s)
        if (c == '_') c = '-';
    return (s == "C" || s == "POSIX") ? std::string() : s;
}

} // namespace

std::vector<std::string> idiomasDelSistema() {
    std::vector<std::string> out;
    if (const char* l = std::getenv("LANGUAGE"); l != nullptr && *l != '\0') {
        std::string lista(l);
        std::size_t i = 0;
        while (i <= lista.size()) {
            const std::size_t f = std::min(lista.find(':', i), lista.size());
            if (auto t = aBcp47(lista.substr(i, f - i)); !t.empty()) out.push_back(t);
            i = f + 1;
        }
    }
    for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* l = std::getenv(v);
        if (l == nullptr || *l == '\0') continue;
        if (auto t = aBcp47(l); !t.empty()) out.push_back(t);
        break;
    }
    return out;
}

} // namespace ba::platform
