// ============================================================================
// src/core/tests/test_mrz.cpp — la zona de lectura mecánica del dorso (documentos/Mrz)
//
//   1. El dígito verificador de la OACI, con el ejemplo de Doc 9303 (la cédula
//      de «Utopía», un país inventado por la OACI para sus ejemplos).
//   2. Renglones de un texto libre: con espacios, comillas, «« en vez de <<, los
//      tres pegados en uno, un < de más o de menos, con texto alrededor.
//   3. Leer una TD1: la de Doc 9303 entera; las correcciones por posición (O/0,
//      I/1, B/8, S/5 en el número, en las fechas y en el nombre); el número
//      largo que sigue en los opcionales; lo que NO se acepta: un verificador que
//      no cierra (un modelo que inventa), una fecha imposible, basura.
//   4. El DNI argentino por el dorso (dni-ar con formato MRZ), el genérico
//      (mrz-td1) y el catálogo: un dorso de otro país no pasa por DNI.
//   5. Lo que contesta un modelo: bien, con errores corregibles, inventado, «NO
//      SE VE», renglones de 29 y 31 (el relleno mal contado, como en la prueba
//      real), el compuesto que no cierra por un «<» corrido (dudosa) y el criterio
//      de dos fotos que coinciden (ConfirmadorMrz).
//
// TODAS LAS MRZ SON SINTÉTICAS: «Prueba», «Ejemplo», números inventados, armadas
// acá con sus verificadores.
// ============================================================================
#include <array>
#include <cstdio>
#include <string>

#include "documentos/DniArgentino.hpp"
#include "documentos/Documento.hpp"
#include "documentos/Mrz.hpp"

using namespace ba::documentos;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

constexpr int kAnio = 2026; // el «hoy» de los tests: el siglo de las fechas no depende del reloj

std::string relleno(std::string s, std::size_t n) {
    s.resize(n, '<');
    return s;
}

/// Una TD1 sintética con todos sus verificadores, armada a mano (no con leerTd1).
std::string armar(const std::string& emisor, const std::string& numero9, const std::string& opc1,
                  const std::string& nac, char sexo, const std::string& venc, const std::string& nacionalidad,
                  const std::string& opc2, const std::string& nombre) {
    const std::string num = relleno(numero9, 9);
    const std::string l1 = "ID" + emisor + num + std::to_string(digitoMrz(num)) + relleno(opc1, 15);
    std::string l2 = nac + std::to_string(digitoMrz(nac)) + sexo + venc + std::to_string(digitoMrz(venc)) +
                     nacionalidad + relleno(opc2, 11);
    const std::string comp = l1.substr(5, 25) + l2.substr(0, 7) + l2.substr(8, 7) + l2.substr(18, 11);
    l2 += std::to_string(digitoMrz(comp));
    return l1 + "\n" + l2 + "\n" + relleno(nombre, 30);
}

const std::string kUtopia = "I<UTOD231458907<<<<<<<<<<<<<<<\n7408122F1204159UTO<<<<<<<<<<<6\nERIKSSON<<ANNA<MARIA<<<<<<<<<<";

std::string dniPrueba() {
    return armar("ARG", "11222333", "", "900315", 'F', "340420", "ARG", "", "PRUEBA<<JUANA<ANDREA");
}

/// Cambia el carácter `i` del renglón `r` (0..2) de una MRZ de tres renglones.
std::string cambiar(std::string mrz, int r, std::size_t i, char c) {
    mrz[static_cast<std::size_t>(r) * 31 + i] = c;
    return mrz;
}

void verificador() {
    CHECK(digitoMrz("D23145890") == 7, "el número del ejemplo de la OACI");
    CHECK(digitoMrz("740812") == 2, "el nacimiento del ejemplo");
    CHECK(digitoMrz("120415") == 9, "el vencimiento del ejemplo");
    CHECK(digitoMrz("<<<") == 0, "los < valen cero");
    CHECK(digitoMrz("A") == 0 && digitoMrz("B") == 7 && digitoMrz("Z") == 5, "las letras valen 10 a 35");
    CHECK(digitoMrz("12a") == -1, "una minúscula no va");
    CHECK(digitoMrz("1 2") == -1, "un espacio no va");
}

void renglones() {
    std::array<std::string, 3> r;
    CHECK(renglonesTd1(kUtopia, r) && r[0] == "I<UTOD231458907<<<<<<<<<<<<<<<", "tres renglones limpios");
    CHECK(renglonesTd1("```\nI<UTO D23145890 7<<<<<<<<<<<<<<<\n7408122F1204159UTO<<<<<<<<<<<6\n"
                       "ERIKSSON<<ANNA<MARIA<<<<<<<<<<\n```",
                       r) &&
              r[0] == "I<UTOD231458907<<<<<<<<<<<<<<<",
          "con espacios y el bloque de código de un modelo");
    CHECK(renglonesTd1("Acá están:\ni<utod231458907<<<<<<<<<<<<<<<\n7408122f1204159uto<<<<<<<<<<<6\n"
                       "eriksson<<anna<maria<<<<<<<<<<\nEso es todo.",
                       r) &&
              r[2] == "ERIKSSON<<ANNA<MARIA<<<<<<<<<<",
          "en minúsculas y con texto alrededor");
    CHECK(renglonesTd1("I<UTOD231458907«««««««\n7408122F1204159UTO<<<<<<<<<<<6\nERIKSSON«ANNA<MARIA«««««", r) &&
              r[0] == "I<UTOD231458907<<<<<<<<<<<<<<<" && r[2] == "ERIKSSON<<ANNA<MARIA<<<<<<<<<<",
          "« como <<");
    CHECK(renglonesTd1("I<UTOD231458907<<<<<<<<<<<<<<<7408122F1204159UTO<<<<<<<<<<<6ERIKSSON<<ANNA<MARIA<<<<<<<<<<", r) &&
              r[1] == "7408122F1204159UTO<<<<<<<<<<<6",
          "los tres pegados en un renglón de 90");
    CHECK(renglonesTd1("I<UTOD231458907<<<<<<<<<<<<<\n7408122F1204159UTO<<<<<<<<<<<<<6\nERIKSSON<<ANNA<MARIA<<<<<<<<<<<", r) &&
              r[0].size() == 30 && r[1] == "7408122F1204159UTO<<<<<<<<<<<6" && r[2].size() == 30,
          "un < de más o de menos se ajusta en la última tira");
    CHECK(!renglonesTd1("I<UTOD231458907\n7408122F1204159UTO6\nERIKSSON", r), "renglones cortos");
    CHECK(!renglonesTd1("hola, no veo nada", r), "basura");
    CHECK(!renglonesTd1("P<UTOERIKSSON<<ANNA<MARIA<<<<<<<<<<<<<<<<<<<\nL898902C36UTO7408122F1204159ZE184226B<<<<<10",
                        r),
          "un pasaporte (TD3, 44 caracteres) no es una TD1");
}

void leer() {
    Td1 t;
    std::string motivo;
    CHECK(leerTd1(kUtopia, t, &motivo, kAnio), "el ejemplo de la OACI");
    CHECK(t.codigo == "I<" && t.emisor == "UTO" && t.numero == "D23145890", "tipo, emisor y número");
    CHECK(t.nacimiento == "1974-08-12" && t.vencimiento == "2012-04-15" && t.sexo == "F", "fechas y sexo");
    CHECK(t.apellido == "Eriksson" && t.nombres == "Anna Maria", "apellido y nombres");
    CHECK(t.correcciones == 0, "sin correcciones");

    const std::string dni = dniPrueba();
    CHECK(leerTd1(dni, t, &motivo, kAnio) && t.numero == "11222333" && t.nacimiento == "1990-03-15" &&
              t.vencimiento == "2034-04-20" && t.nacionalidad == "ARG",
          "un DNI sintético");

    // Las confusiones, por posición.
    CHECK(leerTd1(cambiar(dni, 0, 6, 'I'), t, &motivo, kAnio) && t.numero == "11222333" && t.correcciones == 1,
          "una I en el número es un 1 (lo dice el verificador)");
    CHECK(leerTd1(cambiar(dni, 1, 1, 'O'), t, &motivo, kAnio) && t.nacimiento == "1990-03-15", "O en la fecha es 0");
    CHECK(leerTd1(cambiar(cambiar(dni, 1, 4, 'I'), 1, 5, 'S'), t, &motivo, kAnio) && t.nacimiento == "1990-03-15",
          "I y S en la fecha son 1 y 5");
    // El verificador de 900315 es 2: una Z en su lugar es un 2.
    CHECK(leerTd1(cambiar(dni, 1, 6, 'Z'), t, &motivo, kAnio) && t.nacimiento == "1990-03-15",
          "Z en un verificador es 2");
    CHECK(leerTd1(cambiar(dni, 2, 0, '8'), t, &motivo, kAnio) && t.apellido == "Brueba",
          "un 8 en el nombre es una B (el nombre no tiene verificador: se corrige igual)");
    CHECK(leerTd1(cambiar(dni, 0, 2, '4'), t, &motivo, kAnio) && t.emisor == "ARG", "un 4 en el emisor es una A");

    // Número largo (Doc 9303: el verificador va al final de la tira, en los opcionales).
    {
        const std::string largo = "123456789012";
        const std::string num9 = largo.substr(0, 9);
        const std::string resto = largo.substr(9);
        std::string l1 = "IDUTO" + num9 + "<" + resto + std::to_string(digitoMrz(largo));
        l1 = relleno(l1, 30);
        std::string l2 = "8001014M3001019UTO<<<<<<<<<<<";
        l2 += std::to_string(digitoMrz(l1.substr(5, 25) + l2.substr(0, 7) + l2.substr(8, 7) + l2.substr(18, 11)));
        CHECK(leerTd1(l1 + "\n" + l2 + "\nEJEMPLO<<MARTIN<<<<<<<<<<<<<<<<", t, &motivo, kAnio) && t.numero == largo &&
                  t.nacimiento == "1980-01-01" && t.opcional1.empty(),
              "número largo que sigue en los opcionales");
    }

    // Lo que no se acepta.
    CHECK(!leerTd1(cambiar(dni, 0, 12, '9'), t, &motivo, kAnio) && motivo.find("número") != std::string::npos,
          "un dígito del número inventado: no cierra");
    CHECK(motivo == "no cierra el verificador del número",
          "en un número todo de dígitos no se prueban letras: no se «arregla» con una Z");
    CHECK(!leerTd1(cambiar(dni, 1, 4, '2'), t, &motivo, kAnio) && motivo.find("nacimiento") != std::string::npos,
          "el nacimiento cambiado: no cierra");
    CHECK(!leerTd1(cambiar(dni, 1, 12, '9'), t, &motivo, kAnio) && motivo.find("vencimiento") != std::string::npos,
          "el vencimiento cambiado: no cierra");
    CHECK(!leerTd1(cambiar(dni, 1, 20, 'M'), t, &motivo, kAnio) && motivo.find("compuesto") != std::string::npos,
          "un opcional inventado: no cierra el compuesto");
    {
        // Fecha imposible con sus verificadores bien hechos: igual no.
        const std::string mal = armar("ARG", "11222333", "", "901315", 'F', "340420", "ARG", "", "PRUEBA<<JUANA");
        CHECK(!leerTd1(mal, t, &motivo, kAnio) && motivo.find("fecha") != std::string::npos, "mes 13");
    }
    CHECK(!leerTd1(cambiar(dni, 0, 0, 'P'), t, &motivo, kAnio), "no es una cédula");
    CHECK(!leerTd1("NO SE VE", t, &motivo, kAnio), "sin renglones");
    {
        // Un modelo que inventa entera una MRZ «con forma»: casi nunca cierra todo.
        const std::string inventada =
            "IDARG12345678<1<<<<<<<<<<<<<<<\n8501011M3001011ARG<<<<<<<<<<<0\nPEREZ<<JUAN<<<<<<<<<<<<<<<<<<<";
        CHECK(!leerTd1(inventada, t, &motivo, kAnio), "inventada: no cierran los verificadores");
    }
    // El siglo: nacido en el 30 con hoy en 2026 es 1930; vence en el 30, 2030.
    {
        const std::string m = armar("ARG", "5111222", "", "300101", 'M', "300101", "ARG", "", "EJEMPLO<<PEDRO");
        CHECK(leerTd1(m, t, &motivo, kAnio) && t.nacimiento == "1930-01-01" && t.vencimiento == "2030-01-01",
              "el siglo de las fechas");
    }
}

void tipos() {
    const std::string dni = dniPrueba();
    Datos d;
    DniArgentino dniAr;
    CHECK(dniAr.leer(Codigo{"MRZ", dni, {}}, d), "dni-ar lee el dorso");
    CHECK(d.numero == "11222333" && d.apellido == "Prueba" && d.nombres == "Juana Andrea" && d.sexo == "F" &&
              d.pais == "AR" && d.nacionalidad == "Argentina" && d.nacimiento == "1990-03-15",
          "los datos del dorso");
    CHECK(d.tramite.empty() && d.cuil.empty() && d.vencimiento == "2034-04-20", "el dorso no trae trámite ni CUIL");
    CHECK(d.extra.value("lado", std::string()) == "dorso", "dice que salió del dorso");
    d.tipo = "dni-ar";
    CHECK(paraLog(d) == "dni-ar …333", "al log, solo los últimos tres");

    // El número de DNI en los opcionales (si el campo del número trae otra cosa).
    {
        const std::string m =
            armar("ARG", "A0000001", "11222333", "900315", 'F', "340420", "ARG", "", "PRUEBA<<JUANA");
        CHECK(dniAr.leer(Codigo{"MRZ", m, {}}, d) && d.numero == "11222333", "el DNI en los opcionales");
    }
    // Un dorso de otro país no es un DNI argentino, pero sí una cédula TD1.
    const std::string uy = armar("URY", "12345672", "", "880229", 'M', "310101", "URY", "", "EJEMPLO<<MARTIN");
    CHECK(!dniAr.leer(Codigo{"MRZ", uy, {}}, d), "uruguaya no es dni-ar");
    MrzTd1 gen;
    CHECK(gen.leer(Codigo{"MRZ", uy, {}}, d) && d.pais == "UY" && d.nacionalidad == "Uruguay" && d.numero == "12345672",
          "mrz-td1 la lee");
    CHECK(!gen.leer(Codigo{"PDF417", uy, {}}, d), "con otro formato no es suya");

    Catalogo cat;
    CHECK(cat.porId("mrz-td1") != nullptr, "el genérico está en el catálogo");
    CHECK(cat.leer(Codigo{"MRZ", dni, {}}, {}, d) && d.tipo == "dni-ar", "el DNI por el dorso sale como dni-ar");
    CHECK(cat.leer(Codigo{"MRZ", uy, {}}, {}, d) && d.tipo == "mrz-td1", "la uruguaya, como mrz-td1");
    CHECK(!cat.leer(Codigo{"MRZ", uy, {}}, {"dni-ar"}, d), "si solo se acepta el DNI, la uruguaya no");
    CHECK(cat.leer(Codigo{"", dni, {}}, {"dni-ar"}, d) && d.numero == "11222333", "pegado sin formato, también");
    CHECK(!cat.leer(Codigo{"MRZ", "cualquier cosa", {}}, {}, d), "basura con formato MRZ");
    // El frente sigue igual.
    CHECK(cat.leer(Codigo{"PDF417", "00500000001@PRUEBA@JUANA ANDREA@F@11222333@A@15/03/1990@20/04/2016", {}}, {}, d) &&
              d.tipo == "dni-ar" && d.tramite == "00500000001",
          "el frente no cambió");
}

void respuestas() {
    const std::string dni = dniPrueba();
    LecturaMrz l = interpretarRespuestaMrz(dni, kAnio);
    CHECK(l.estado == LecturaMrz::Estado::Leida && l.texto == dni, "la respuesta perfecta");
    l = interpretarRespuestaMrz("```\n" + cambiar(cambiar(dni, 0, 6, 'I'), 1, 1, 'O') + "\n```", kAnio);
    CHECK(l.estado == LecturaMrz::Estado::Leida && l.texto == dni, "con errores corregibles: sale corregida");
    l = interpretarRespuestaMrz(cambiar(dni, 0, 9, '7'), kAnio);
    CHECK(l.estado == LecturaMrz::Estado::NoCierra && !l.motivo.empty(), "un dígito inventado: no cierra");
    CHECK(l.motivo.find("1122") == std::string::npos, "el motivo no trae datos");
    l = interpretarRespuestaMrz("NO SE VE", kAnio);
    CHECK(l.estado == LecturaMrz::Estado::NoSeVe, "NO SE VE");
    l = interpretarRespuestaMrz("No se ve.", kAnio);
    CHECK(l.estado == LecturaMrz::Estado::NoSeVe, "no se ve, en minúsculas y con punto");
    l = interpretarRespuestaMrz("La imagen muestra un documento.", kAnio);
    CHECK(l.estado == LecturaMrz::Estado::NoCierra, "una descripción en vez de los renglones");
    CHECK(pedidoMrz().find("NO SE VE") != std::string::npos && pedidoMrz().find("30 caracteres") != std::string::npos,
          "el pedido dice qué contestar si no se ve");

    // El relleno mal contado (la prueba real: renglones de 29 y 31): se ajusta la tira
    // de «<» más larga, no un dato.
    {
        const std::string l1 = dni.substr(0, 30), l2 = dni.substr(31, 30), l3 = dni.substr(62, 30);
        const std::string corto = l1.substr(0, 20) + l1.substr(21);              // 29: un < menos en el relleno
        const std::string largo = l2.substr(0, 20) + "<" + l2.substr(20);       // 31: uno más en los opcionales
        const std::string nombre = l3.substr(0, 25) + "<<";                      // 27: tres menos al final
        l = interpretarRespuestaMrz(corto + "\n" + largo + "\n" + nombre, kAnio);
        CHECK(l.estado == LecturaMrz::Estado::Leida && l.texto == dni, "29, 31 y 27: ajustados al relleno");
        std::array<std::string, 3> r;
        // «PRUEBA<<JUANA<ANDREA<<<<…» con un < de más: la tira larga es la del final, no la del medio.
        CHECK(renglonesTd1(l1 + "\n" + l2 + "\n" + l3 + "<", r) && r[2] == l3, "la tira más larga, no la primera");
        CHECK(!renglonesTd1(l1.substr(0, 26) + "\n" + l2 + "\n" + l3, r), "cuatro de menos: no se inventa");
    }

    // El compuesto que no cierra por un «<» corrido en los opcionales: dudosa.
    const std::string conOpc = armar("ARG", "11222333", "AB12", "900315", 'F', "340420", "ARG", "", "PRUEBA<<JUANA<ANDREA");
    std::string corrida = conOpc;
    corrida.replace(15, 5, "A<B12"); // «AB12<» -> «A<B12»: el renglón sigue de 30
    l = interpretarRespuestaMrz(conOpc, kAnio);
    CHECK(l.estado == LecturaMrz::Estado::Leida, "con opcionales, bien");
    const LecturaMrz d1 = interpretarRespuestaMrz(corrida, kAnio);
    CHECK(d1.estado == LecturaMrz::Estado::Dudosa && d1.motivo.find("compuesto") != std::string::npos,
          "un < corrido en los opcionales: dudosa (cierran los otros tres)");
    Td1 t;
    CHECK(!leerTd1(corrida, t, nullptr, kAnio), "leerTd1 por defecto exige el compuesto");
    CHECK(leerTd1(corrida, t, nullptr, kAnio, false) && !t.compuesto && t.numero == "11222333", "sin exigirlo, avisa");
    Datos dd;
    CHECK(Catalogo().leer(Codigo{"MRZ", d1.texto, {}}, {"dni-ar"}, dd) && dd.numero == "11222333",
          "los tipos no exigen el compuesto (lo decide quien leyó)");

    // El criterio de dos fotos.
    ConfirmadorMrz c;
    CHECK(!c.agregar(d1), "una dudosa sola no alcanza");
    CHECK(!c.agregar(interpretarRespuestaMrz("NO SE VE", kAnio)), "un NO SE VE no suma");
    std::string otroNombre = corrida;
    otroNombre.replace(62 + 9, 5, "JUANO");
    CHECK(!c.agregar(interpretarRespuestaMrz(otroNombre, kAnio)), "otra con el nombre distinto: no coincide");
    std::string otraCorrida = conOpc;
    otraCorrida.replace(15, 5, "AB<12"); // el < corrido en otro lugar: los opcionales no cuentan
    CHECK(c.agregar(interpretarRespuestaMrz(otraCorrida, kAnio)), "dos dudosas que coinciden en lo que importa: se acepta");
    c.reiniciar();
    CHECK(!c.agregar(d1), "reiniciado, empieza de nuevo");
    CHECK(c.agregar(interpretarRespuestaMrz(dni, kAnio)), "una leída entera se acepta sola");
    CHECK(d1.clave.find("11222333") != std::string::npos, "la clave compara el número");
}

} // namespace

int main() {
    verificador();
    renglones();
    leer();
    tipos();
    respuestas();
    if (g_fallos == 0) std::printf("test_mrz: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
