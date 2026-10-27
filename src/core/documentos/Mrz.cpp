// ============================================================================
// src/core/documentos/Mrz.cpp — ver Mrz.hpp
// ============================================================================
#include "documentos/Mrz.hpp"
#include "i18n/I18n.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <vector>

namespace ba::documentos {
namespace {

constexpr std::size_t kLargo = 30;

bool esDigito(char c) { return c >= '0' && c <= '9'; }
bool esLetra(char c) { return c >= 'A' && c <= 'Z'; }
bool esMrz(char c) { return esDigito(c) || esLetra(c) || c == '<'; }

/// Las confusiones de un lector (o de un modelo) entre letras y dígitos que se
/// parecen en OCR-B. De letra a dígito hay más que al revés: la Q y la D
/// también se leen 0, la L se lee 1; un 0 en un lugar de letras es una O.
char aDigito(char c) {
    switch (c) {
        case 'O': case 'Q': case 'D': return '0';
        case 'I': case 'L': return '1';
        case 'Z': return '2';
        case 'A': return '4';
        case 'S': return '5';
        case 'G': return '6';
        case 'T': return '7';
        case 'B': return '8';
        default: return 0;
    }
}
char aLetra(char c) {
    switch (c) {
        case '0': return 'O';
        case '1': return 'I';
        case '2': return 'Z';
        case '4': return 'A';
        case '5': return 'S';
        case '6': return 'G';
        case '7': return 'T';
        case '8': return 'B';
        default: return 0;
    }
}
/// Lo que podría haber sido en un campo que admite letras y dígitos (0 = nada).
char alternativa(char c) { return esDigito(c) ? aLetra(c) : esLetra(c) ? aDigito(c) : 0; }

/// Qué va en cada lugar: A = letra o «<», N = dígito, X = cualquiera, S = sexo,
/// C = el verificador del número (un dígito, o «<» si el número sigue en los opcionales).
const char* const kForma[3] = {
    "AAAAAXXXXXXXXXCXXXXXXXXXXXXXXX",
    "NNNNNNNSNNNNNNNAAAXXXXXXXXXXXN",
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
};

bool fallar(std::string* motivo, const char* m) {
    if (motivo != nullptr) *motivo = i18n::traducir("documentos", m); // el motivo lo lee el operador
    return false;
}

/// La fecha AAMMDD en ISO, con el siglo: el nacimiento no puede ser futuro y el
/// vencimiento no está a más de 50 años. "" si no es una fecha.
std::string fechaMrz(const std::string& s, bool nacimiento, int anioActual) {
    if (s.size() != 6 || !std::all_of(s.begin(), s.end(), esDigito)) return {};
    const int aa = std::stoi(s.substr(0, 2));
    int anio = 2000 + aa;
    if (nacimiento ? anio > anioActual : anio > anioActual + 50) anio -= 100;
    const std::string ddmmaaaa = s.substr(4, 2) + "/" + s.substr(2, 2) + "/" + std::to_string(anio);
    return fechaIso(ddmmaaaa);
}

/// Prueba cambiar caracteres de `s` en las posiciones `amb` (por su alternativa)
/// hasta que `ok` diga que sí, de a menos cambios. 1 = una sola corrección con
/// la menor cantidad de cambios (queda en `s`); 0 = ninguna; -1 = más de una
/// igual de buena (no se adivina). Entre empatadas gana la que deja menos
/// letras, si es una sola: los números de documento son casi siempre dígitos.
int corregir(std::string& s, const std::vector<std::size_t>& amb, const std::function<bool(const std::string&)>& ok,
             int maxCambios, int& cambios) {
    for (int k = 0; k <= maxCambios && k <= static_cast<int>(amb.size()); ++k) {
        std::vector<std::string> validas;
        // Las combinaciones de k posiciones, con un selector ordenado.
        std::vector<bool> sel(amb.size(), false);
        std::fill(sel.begin(), sel.begin() + k, true);
        do {
            std::string t = s;
            for (std::size_t i = 0; i < amb.size(); ++i)
                if (sel[i]) t[amb[i]] = alternativa(t[amb[i]]);
            if (ok(t)) validas.push_back(std::move(t));
        } while (std::prev_permutation(sel.begin(), sel.end()));
        if (validas.empty()) continue;
        if (validas.size() > 1) {
            const auto letras = [](const std::string& t) { return std::count_if(t.begin(), t.end(), esLetra); };
            std::sort(validas.begin(), validas.end(),
                      [&letras](const std::string& a, const std::string& b) { return letras(a) < letras(b); });
            if (letras(validas[0]) == letras(validas[1])) return -1;
        }
        s = validas[0];
        cambios += k;
        return 1;
    }
    return 0;
}

/// Las posiciones de s[desde, hasta) que se pueden probar al revés (corrida en
/// `base`), solo las de la MINORÍA: en un campo casi todo de dígitos (un número
/// de DNI) se prueba que una letra sea un dígito, nunca que un dígito sea una
/// letra. Si no, con tres cambios libres casi cualquier número inventado
/// «cerraba» metiéndole letras.
void dudosas(const std::string& s, std::size_t desde, std::size_t hasta, std::size_t base,
             std::vector<std::size_t>& out) {
    std::size_t letras = 0, digitos = 0;
    for (std::size_t i = desde; i < hasta; ++i) {
        if (esLetra(s[i])) ++letras;
        else if (esDigito(s[i])) ++digitos;
    }
    for (std::size_t i = desde; i < hasta; ++i) {
        if (alternativa(s[i]) == 0) continue;
        const bool letra = esLetra(s[i]);
        if ((letra && letras > digitos) || (!letra && digitos > letras)) continue;
        out.push_back(base + i);
    }
}

bool verifica(const std::string& campo, char digito) {
    const int d = digitoMrz(campo);
    return d >= 0 && esDigito(digito) && d == digito - '0';
}

/// Ajusta un renglón de 27 a 33 caracteres a 30 estirando o achicando la tira
/// de «<» MÁS LARGA (de dos o más; si empatan, la última): es el relleno, donde un
/// modelo cuenta mal (en la prueba con un DNI real, renglones de 29 y 31). Nunca
/// se toca un dato: si no hay una tira que alcance, no se ajusta.
bool ajustarLargo(std::string& l) {
    if (l.size() == kLargo) return true;
    const int d = static_cast<int>(kLargo) - static_cast<int>(l.size());
    if (d > 3 || d < -3) return false;
    std::size_t mejorIni = 0, mejorLargo = 0;
    for (std::size_t i = 0; i < l.size();) {
        if (l[i] != '<') {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < l.size() && l[j] == '<') ++j;
        if (j - i >= mejorLargo) mejorIni = i, mejorLargo = j - i;
        i = j;
    }
    if (mejorLargo < 2 || static_cast<int>(mejorLargo) + d < 1) return false;
    if (d > 0) l.insert(mejorIni, static_cast<std::size_t>(d), '<');
    else l.erase(mejorIni, static_cast<std::size_t>(-d));
    return true;
}

std::string sinRelleno(const std::string& s) {
    const auto a = s.find_first_not_of('<');
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of('<');
    return s.substr(a, b - a + 1);
}

std::string conEspacios(const std::string& s) {
    std::string out;
    for (char c : s) {
        const char x = c == '<' ? ' ' : c;
        if (x == ' ' && (out.empty() || out.back() == ' ')) continue;
        out.push_back(x);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

int anioDeHoy() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return tm.tm_year + 1900;
}

struct Pais {
    const char* alfa3;
    const char* alfa2;
    const char* nombre;
};
// Los de la región y los que más se ven acá. El resto queda con su código.
constexpr Pais kPaises[] = {
    {"ARG", "AR", "Argentina"}, {"BOL", "BO", "Bolivia"},   {"BRA", "BR", "Brasil"},
    {"CHL", "CL", "Chile"},     {"COL", "CO", "Colombia"},  {"ECU", "EC", "Ecuador"},
    {"PRY", "PY", "Paraguay"},  {"PER", "PE", "Perú"},      {"URY", "UY", "Uruguay"},
    {"VEN", "VE", "Venezuela"}, {"MEX", "MX", "México"},    {"ESP", "ES", "España"},
    {"ITA", "IT", "Italia"},    {"PRT", "PT", "Portugal"},  {"FRA", "FR", "Francia"},
    {"D<<", "DE", "Alemania"},  {"DEU", "DE", "Alemania"},  {"USA", "US", "Estados Unidos"},
};

} // namespace

int digitoMrz(const std::string& s) {
    static const int kPesos[] = {7, 3, 1};
    int suma = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        int v;
        if (esDigito(c)) v = c - '0';
        else if (esLetra(c)) v = c - 'A' + 10;
        else if (c == '<') v = 0;
        else return -1;
        suma += v * kPesos[i % 3];
    }
    return suma % 10;
}

bool renglonesTd1(const std::string& texto, std::array<std::string, 3>& out) {
    std::vector<std::string> lineas;
    std::string l;
    bool rara = false; // un carácter que no va en una MRZ
    const auto cerrar = [&]() {
        if (!l.empty() && !rara) lineas.push_back(l);
        l.clear();
        rara = false;
    };
    for (std::size_t i = 0; i < texto.size(); ++i) {
        const auto c = static_cast<unsigned char>(texto[i]);
        if (c == '\n') {
            cerrar();
            continue;
        }
        // Lo que un modelo escribe en lugar de «<»: comillas angulares, el «<» de ancho
        // completo, la く japonesa (un modelo entrenado en chino la confunde).
        const auto sigue = [&](const char* sec) {
            const std::size_t n = std::char_traits<char>::length(sec);
            if (texto.compare(i, n, sec) != 0) return false;
            i += n - 1;
            return true;
        };
        if (c >= 0x80) {
            if (sigue("\xC2\xAB")) l += "<<";                                        // «
            else if (sigue("\xE2\x80\xB9") || sigue("\xEF\xBC\x9C") || sigue("\xE3\x81\x8F") || sigue("\xE3\x80\x88"))
                l += '<';                                                              // ‹ ＜ く 〈
            else rara = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '`' || c == '"' || c == '\'' || c == '|') continue;
        const char x = static_cast<char>(std::toupper(c));
        if (esMrz(x)) l.push_back(x);
        else rara = true;
    }
    cerrar();
    // Los tres juntos en un renglón de 90 (un modelo que no cortó las líneas).
    if (lineas.size() == 1 && lineas[0].size() == 3 * kLargo) {
        const std::string t = lineas[0];
        lineas = {t.substr(0, kLargo), t.substr(kLargo, kLargo), t.substr(2 * kLargo)};
    }
    std::vector<std::string> cand;
    for (std::string& x : lineas)
        if (x.size() >= kLargo - 3 && x.size() <= kLargo + 3) cand.push_back(x);
    // Los primeros tres seguidos que arrancan como una TD1 (A, C o I: cédula).
    for (std::size_t i = 0; i + 3 <= cand.size(); ++i) {
        const char t = cand[i][0];
        if (t != 'I' && t != 'A' && t != 'C') continue;
        std::array<std::string, 3> r{cand[i], cand[i + 1], cand[i + 2]};
        if (ajustarLargo(r[0]) && ajustarLargo(r[1]) && ajustarLargo(r[2])) {
            out = r;
            return true;
        }
    }
    return false;
}

bool leerTd1(const std::string& texto, Td1& out, std::string* motivo, int anioActual, bool exigirCompuesto) {
    if (anioActual <= 0) anioActual = anioDeHoy();
    std::array<std::string, 3> r;
    if (!renglonesTd1(texto, r)) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "no hay tres renglones de 30 caracteres"));
    int cambios = 0;

    // 1) Lo que la posición obliga: dígitos donde van dígitos, letras donde van letras.
    for (int n = 0; n < 3; ++n) {
        std::string& l = r[static_cast<std::size_t>(n)];
        for (std::size_t i = 0; i < kLargo; ++i) {
            const char f = kForma[n][i];
            char& c = l[i];
            if ((f == 'N' || (f == 'C' && c != '<')) && !esDigito(c)) {
                const char d = aDigito(c);
                if (d == 0) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "una letra donde va un número"));
                c = d;
                ++cambios;
            } else if (f == 'A' && esDigito(c)) {
                const char a = aLetra(c);
                if (a == 0) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "un número donde va una letra"));
                c = a;
                ++cambios;
            }
        }
    }
    std::string& l1 = r[0];
    std::string& l2 = r[1];
    const std::string& l3 = r[2];
    if (l1[0] != 'I' && l1[0] != 'A' && l1[0] != 'C') return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "no es una cédula (TD1)"));
    if (sinRelleno(l1.substr(2, 3)).empty()) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "sin emisor"));

    // 2) El número y su verificador. Si el lugar del verificador es «<», el número
    //    es largo y sigue en los opcionales hasta el próximo «<»; el último de esa
    //    tira es el verificador (Doc 9303, parte 5, nota j).
    std::size_t finNumero = 14; // donde está el verificador
    if (l1[14] == '<') {
        const std::size_t corte = l1.find('<', 15);
        if (corte == std::string::npos || corte <= 16 || corte > 29) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "número largo mal cortado"));
        finNumero = corte - 1;
        if (!esDigito(l1[finNumero])) {
            const char d = aDigito(l1[finNumero]);
            if (d == 0) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "una letra donde va un número"));
            l1[finNumero] = d;
            ++cambios;
        }
    }
    {
        std::string campo = l1.substr(5, 9) + (finNumero > 14 ? l1.substr(15, finNumero - 15) : std::string());
        std::vector<std::size_t> amb;
        dudosas(campo, 0, campo.size(), 0, amb);
        const char dv = l1[finNumero];
        const int res = corregir(campo, amb, [dv](const std::string& t) { return verifica(t, dv); }, 3, cambios);
        if (res == 0) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "no cierra el verificador del número"));
        if (res < 0) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "el número se puede corregir de más de una forma"));
        l1.replace(5, 9, campo.substr(0, 9));
        if (finNumero > 14) l1.replace(15, finNumero - 15, campo.substr(9));
    }

    // 3) Las fechas (ya son todos dígitos: no hay nada que probar).
    if (!verifica(l2.substr(0, 6), l2[6])) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "no cierra el verificador del nacimiento"));
    if (!verifica(l2.substr(8, 6), l2[14])) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "no cierra el verificador del vencimiento"));
    const std::string nac = fechaMrz(l2.substr(0, 6), true, anioActual);
    const std::string venc = fechaMrz(l2.substr(8, 6), false, anioActual);
    if (nac.empty() || venc.empty()) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "una fecha imposible"));

    // 4) El compuesto: renglón 1 desde el número, las fechas con sus verificadores y
    //    los opcionales del 2. Acá NO se prueban correcciones: en los opcionales (letras
    //    y dígitos mezclados, sin verificador propio) cualquier cambio que haga cerrar el
    //    compuesto es tan probable como el bueno, y sería inventar. Si no cierra, la
    //    lectura queda dudosa y la decide otra foto (ConfirmadorMrz).
    const std::string paraCompuesto = l1.substr(5, 25) + l2.substr(0, 7) + l2.substr(8, 7) + l2.substr(18, 11);
    const bool compuestoOk = verifica(paraCompuesto, l2[29]);
    if (!compuestoOk) {
        if (exigirCompuesto) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "no cierra el verificador compuesto"));
        if (motivo != nullptr) *motivo = i18n::traducir("documentos", "no cierra el verificador compuesto");
    }

    // 5) El nombre: APELLIDO<<NOMBRES (los «<» sueltos son espacios).
    const std::size_t corte = l3.find("<<");
    const std::string ap = conEspacios(corte == std::string::npos ? l3 : l3.substr(0, corte));
    const std::string no = corte == std::string::npos ? std::string() : conEspacios(l3.substr(corte + 2));
    if (ap.empty()) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "sin apellido"));

    Td1 t;
    t.codigo = l1.substr(0, 2);
    t.emisor = l1.substr(2, 3);
    t.numero = sinRelleno(l1.substr(5, 9) + (finNumero > 14 ? l1.substr(15, finNumero - 15) : std::string()));
    t.opcional1 = sinRelleno(l1.substr(finNumero + 1));
    t.nacimiento = nac;
    t.sexo = l2[7] == 'M' || l2[7] == 'F' || l2[7] == 'X' ? std::string(1, l2[7]) : std::string();
    t.vencimiento = venc;
    t.nacionalidad = l2.substr(15, 3);
    t.opcional2 = sinRelleno(l2.substr(18, 11));
    t.apellido = capitalizar(ap);
    t.nombres = capitalizar(no);
    t.renglones = r;
    t.correcciones = cambios;
    t.compuesto = compuestoOk;
    if (t.numero.empty()) return fallar(motivo, QT_TRANSLATE_NOOP("documentos", "sin número de documento"));
    out = std::move(t);
    return true;
}

std::string paisAlfa2(const std::string& alfa3) {
    for (const Pais& p : kPaises)
        if (alfa3 == p.alfa3) return p.alfa2;
    return {};
}

std::string nombrePais(const std::string& alfa3) {
    for (const Pais& p : kPaises)
        if (alfa3 == p.alfa3) return p.nombre;
    return sinRelleno(alfa3);
}

Datos datosDeTd1(const Td1& t) {
    Datos d;
    d.pais = paisAlfa2(t.emisor);
    if (d.pais.empty()) d.pais = sinRelleno(t.emisor);
    d.numero = t.numero;
    d.apellido = t.apellido;
    d.nombres = t.nombres;
    d.sexo = t.sexo;
    d.nacimiento = t.nacimiento;
    d.vencimiento = t.vencimiento;
    d.nacionalidad = nombrePais(t.nacionalidad);
    d.crudo = t.renglones[0] + "\n" + t.renglones[1] + "\n" + t.renglones[2];
    d.extra = nlohmann::json{{"lado", "dorso"}};
    return d;
}

bool MrzTd1::leer(const Codigo& c, Datos& out) const {
    if (!c.formato.empty() && c.formato != kFormatoMrz) return false;
    Td1 t;
    // Sin exigir el compuesto: eso lo decide quien leyó (interpretarRespuestaMrz y
    // ConfirmadorMrz), como zxing decide si un código está bien leído.
    if (!leerTd1(c.texto, t, nullptr, 0, false)) return false;
    out = datosDeTd1(t);
    return true;
}

const std::string& pedidoMrz() {
    // El que anduvo en la prueba con un DNI real (dorso, cámara de 1920x1080, la
    // franja de abajo, el modelo local Qwen3.5-4B con su proyector de visión). Es un archivo
    // del idioma del operador (prompts/vision-mrz.md); «NO SE VE» va como %1 y no se traduce:
    // es lo que busca interpretarRespuestaMrz.
    static const std::string s = [] {
        std::string t = i18n::llenar(i18n::leer("prompts/vision-mrz.md"), {"NO SE VE"});
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
        return t;
    }();
    return s;
}

LecturaMrz interpretarRespuestaMrz(const std::string& respuesta, int anioActual) {
    LecturaMrz l;
    Td1 t;
    std::string motivo;
    if (leerTd1(respuesta, t, &motivo, anioActual, false)) {
        l.estado = t.compuesto ? LecturaMrz::Estado::Leida : LecturaMrz::Estado::Dudosa;
        l.texto = t.renglones[0] + "\n" + t.renglones[1] + "\n" + t.renglones[2];
        l.motivo = t.compuesto ? std::string() : motivo;
        // Lo que importa para comparar dos fotos: todo lo que tiene verificador y el
        // nombre (que no tiene). Los opcionales no: ahí está el «<» corrido.
        l.clave = t.codigo + "|" + t.emisor + "|" + t.numero + "|" + t.nacimiento + "|" + t.sexo + "|" +
                  t.vencimiento + "|" + t.nacionalidad + "|" + t.renglones[2];
        return l;
    }
    std::string m;
    for (char c : respuesta)
        if (c != ' ' && c != '\n' && c != '\r' && c != '.')
            m.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (m.find("NOSEVE") != std::string::npos) {
        l.estado = LecturaMrz::Estado::NoSeVe;
        l.motivo = i18n::traducir("documentos", "no se ven los renglones");
        return l;
    }
    l.estado = LecturaMrz::Estado::NoCierra;
    l.motivo = motivo;
    return l;
}

bool ConfirmadorMrz::agregar(const LecturaMrz& l) {
    if (l.estado == LecturaMrz::Estado::Leida) return true;
    if (l.estado != LecturaMrz::Estado::Dudosa || l.clave.empty()) return false;
    if (std::find(claves_.begin(), claves_.end(), l.clave) != claves_.end()) return true;
    claves_.push_back(l.clave);
    if (claves_.size() > kRecuerda) claves_.erase(claves_.begin());
    return false;
}

void ConfirmadorMrz::reiniciar() { claves_.clear(); }

} // namespace ba::documentos
