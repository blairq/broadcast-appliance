// ============================================================================
// src/core/tests/test_documentos.cpp — los tipos de documento (documentos/Documento)
//
//   1. Las ayudas: partir conservando vacíos, fechas a ISO (con las inválidas),
//      mayúsculas de nombres (con Ñ y partículas), el número con puntos, el log
//      que no muestra el documento, el dígito del CUIL.
//   2. El DNI argentino: el formato nuevo (con y sin el noveno campo del CUIL,
//      con un CUIL que no cierra), el viejo (con los espacios del número), y lo
//      que NO es un DNI: basura, campos de menos, otro código con arrobas,
//      fecha imposible, número corto.
//   3. El catálogo: de fábrica, los aceptados, el formato que no corresponde.
//
// TODOS LOS DATOS SON SINTÉTICOS: «Prueba», «Ejemplo», números inventados.
// ============================================================================
#include <cstdio>
#include <string>

#include "documentos/DniArgentino.hpp"
#include "documentos/Documento.hpp"

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

// Sintéticos. El CUIL 27-11222333-4 cierra: 2*5+7*4+1*3+1*2+2*7+2*6+2*5+3*4+3*3+3*2 = 106, 106 % 11 = 7, 11-7 = 4.
const char* kNuevo = "00500000001@PRUEBA@JUANA ANDREA@F@11222333@A@15/03/1990@20/04/2016@274";
const char* kNuevoSinCuil = "00500000002@EJEMPLO DE LA SIERRA@MARTIN@M@9888777@B@29/02/1988@01/07/2019";
const char* kViejo =
    "@11222333    @A@1@PRUEBA@JUANA ANDREA@ARGENTINA@15/03/1990@F@20/04/2010@00500000003@7777 @20/04/2025@555@0@"
    "ILR:2.01 C:110608.01 (No Cap.)@UNIDAD #20 || S/N: 0000>2008>>0000";

Codigo pdf(const std::string& t) { return Codigo{"PDF417", t, {}}; }

bool leerDni(const std::string& texto, Datos& d) {
    DniArgentino t;
    return t.leer(pdf(texto), d);
}

void ayudas() {
    const auto p = partir("@a@@b", '@');
    CHECK(p.size() == 4 && p[0].empty() && p[1] == "a" && p[2].empty() && p[3] == "b", "partir conserva vacíos");
    CHECK(partir("", '@').size() == 1, "partir vacío da un campo");
    CHECK(recortar("  hola \r\n") == "hola", "recortar");

    CHECK(fechaIso("15/03/1990") == "1990-03-15", "fecha con barras");
    CHECK(fechaIso("15-03-1990") == "1990-03-15", "fecha con guiones");
    CHECK(fechaIso("29/02/1988") == "1988-02-29", "29 de febrero bisiesto");
    CHECK(fechaIso("29/02/1989").empty(), "29 de febrero no bisiesto");
    CHECK(fechaIso("31/04/2000").empty(), "abril no tiene 31");
    CHECK(fechaIso("15/13/1990").empty(), "mes 13");
    CHECK(fechaIso("15/03-1990").empty(), "separadores mezclados");
    CHECK(fechaIso("1990-03-15").empty(), "ya en ISO no es dd/mm/aaaa");
    CHECK(fechaIso("").empty(), "fecha vacía");

    CHECK(capitalizar("JUANA ANDREA") == "Juana Andrea", "capitalizar");
    CHECK(capitalizar("EJEMPLO DE LA SIERRA") == "Ejemplo de la Sierra", "partículas en minúscula");
    CHECK(capitalizar("DE LA PRUEBA") == "De la Prueba", "partícula al principio");
    CHECK(capitalizar("MUÑOZ") == "Muñoz", "Ñ a ñ");
    CHECK(capitalizar("ÁVILA") == "Ávila", "inicial acentuada queda mayúscula");
    CHECK(capitalizar("D'PRUEBA-EJEMPLO") == "D'Prueba-Ejemplo", "apóstrofo y guion");
    CHECK(capitalizar("  dos   espacios ") == "Dos Espacios", "espacios de más");

    CHECK(numeroConPuntos("11222333") == "11.222.333", "puntos de a tres");
    CHECK(numeroConPuntos("9888777") == "9.888.777", "siete dígitos");
    CHECK(numeroConPuntos("123") == "123", "corto sin puntos");
    CHECK(numeroConPuntos("A12") == "A12", "no numérico tal cual");

    Datos d;
    d.tipo = "dni-ar";
    d.numero = "11222333";
    CHECK(paraLog(d) == "dni-ar …333", "el log solo lleva los últimos tres");
    CHECK(paraLog(d).find("11222") == std::string::npos, "el log no lleva el número entero");

    CHECK(digitoCuil("2711222333") == 4, "dígito del CUIL");

    CHECK(taparNumeros("Sos Juana, DNI 11.222.333. ¿Está bien?") == "Sos Juana, DNI …333. ¿Está bien?", "tapa el DNI");
    CHECK(taparNumeros("once 11222333 y 11 222 333") == "once …333 y …333", "tapa con y sin separadores");
    CHECK(taparNumeros("tengo 25 años, 3.5 kilos") == "tengo 25 años, 3.5 kilos", "los números cortos quedan");
    CHECK(taparNumeros("") == "", "vacío");
    CHECK(digitoCuil("27112223") == -1, "largo equivocado");
}

void dniNuevo() {
    Datos d;
    CHECK(leerDni(kNuevo, d), "lee el formato nuevo");
    CHECK(d.pais == "AR", "país");
    CHECK(d.tramite == "00500000001", "trámite");
    CHECK(d.apellido == "Prueba", "apellido");
    CHECK(d.nombres == "Juana Andrea", "nombres");
    CHECK(d.sexo == "F", "sexo");
    CHECK(d.numero == "11222333", "número");
    CHECK(d.ejemplar == "A", "ejemplar");
    CHECK(d.nacimiento == "1990-03-15", "nacimiento ISO");
    CHECK(d.emision == "2016-04-20", "emisión ISO");
    CHECK(d.vencimiento.empty(), "el nuevo no trae vencimiento");
    CHECK(d.cuil == "27-11222333-4", "CUIL que cierra");
    CHECK(d.crudo == kNuevo, "crudo tal cual");
    CHECK(d.nombreCompleto() == "Juana Andrea Prueba", "nombre completo");

    Datos s;
    CHECK(leerDni(kNuevoSinCuil, s), "nuevo sin el noveno campo");
    CHECK(s.cuil.empty(), "sin noveno, sin CUIL");
    CHECK(s.numero == "9888777", "siete dígitos");
    CHECK(s.apellido == "Ejemplo de la Sierra", "apellido compuesto");

    Datos m;
    CHECK(leerDni("00500000001@PRUEBA@JUANA@F@11222333@A@15/03/1990@20/04/2016@270", m), "con CUIL que no cierra");
    CHECK(m.cuil.empty(), "un CUIL que no cierra no se inventa");

    Datos z;
    CHECK(leerDni("00500000001@PRUEBA@JUANA@F@011222333@A@15/03/1990@20/04/2016\r\n", z), "ceros y fin de línea");
    CHECK(z.numero == "11222333", "sin ceros a la izquierda");

    Datos x;
    CHECK(leerDni("00500000004@PRUEBA@ALEX@X@30111222@C@01/01/2001@02/02/2022", x) && x.sexo == "X", "sexo X");
}

void dniViejo() {
    Datos d;
    CHECK(leerDni(kViejo, d), "lee el formato viejo");
    CHECK(d.numero == "11222333", "número sin los espacios");
    CHECK(d.ejemplar == "A", "ejemplar");
    CHECK(d.apellido == "Prueba", "apellido");
    CHECK(d.nombres == "Juana Andrea", "nombres");
    CHECK(d.nacionalidad == "Argentina", "nacionalidad");
    CHECK(d.nacimiento == "1990-03-15", "nacimiento");
    CHECK(d.sexo == "F", "sexo");
    CHECK(d.emision == "2010-04-20", "emisión");
    CHECK(d.tramite == "00500000003", "trámite");
    CHECK(d.vencimiento == "2025-04-20", "vencimiento");

    Datos l;
    CHECK(leerDni("@F11222333@A@1@PRUEBA@JUANA@ARGENTINA@15/03/1990@F@20/04/2010@00500000003@7777@20/04/2025@", l),
          "viejo con letra adelante del número");
    CHECK(l.numero == "11222333", "la letra no queda en el número");
}

void noEsDni() {
    Datos d;
    CHECK(!leerDni("", d), "vacío");
    CHECK(!leerDni("hola mundo", d), "sin arrobas");
    CHECK(!leerDni("https://ejemplo.com/@usuario", d), "una url con arroba");
    CHECK(!leerDni("juana@ejemplo.com", d), "un correo");
    CHECK(!leerDni("00500000001@PRUEBA@JUANA@F@11222333@A@15/03/1990", d), "nuevo con campos de menos");
    CHECK(!leerDni("@11222333@A@1@PRUEBA@JUANA@ARGENTINA@15/03/1990@F", d), "viejo con campos de menos");
    CHECK(!leerDni("00500000001@PRUEBA@JUANA@F@1234@A@15/03/1990@20/04/2016", d), "número corto");
    CHECK(!leerDni("00500000001@PRUEBA@JUANA@F@11222333@A@31/02/1990@20/04/2016", d), "fecha imposible");
    CHECK(!leerDni("ABC@PRUEBA@JUANA@F@11222333@A@15/03/1990@20/04/2016", d), "trámite no numérico");
    CHECK(!leerDni("00500000001@1234@JUANA@F@11222333@A@15/03/1990@20/04/2016", d), "apellido con números");
    CHECK(!leerDni("@@@@@@@@@@@@@@@@", d), "solo arrobas");
    CHECK(d.numero.empty(), "lo que no se lee no toca la salida");
}

void catalogo() {
    Catalogo c;
    CHECK(c.porId("dni-ar") != nullptr, "el DNI viene de fábrica");
    CHECK(c.tipos().size() == 2 && c.porId("mrz-td1") != nullptr, "de fábrica: el DNI y la cédula con MRZ (test_mrz)");
    Datos d;
    CHECK(c.leer(pdf(kNuevo), {}, d) && d.tipo == "dni-ar", "lee con todos");
    CHECK(c.leer(pdf(kNuevo), {"plugin:aamva/licencia", "dni-ar"}, d), "los de plugin se saltean");
    CHECK(!c.leer(pdf(kNuevo), {"plugin:aamva/licencia"}, d), "si no se acepta el DNI, no lo lee");
    CHECK(!c.leer(Codigo{"QRCode", kNuevo, {}}, {}, d), "un QR no es un DNI argentino");
    CHECK(c.leer(Codigo{"", kNuevo, {}}, {}, d), "tipeado (sin formato) se acepta");
    const auto j = d.aJson();
    CHECK(j.value("numero", "") == "11222333" && !j.contains("vencimiento"), "aJson solo con lo que hay");
    const auto afuera = resumenParaAfuera(j);
    CHECK(afuera.value("numero", "") == "…333" && afuera.value("nombre", "") == "Juana Andrea Prueba", "resumen para afuera");
    CHECK(!afuera.contains("crudo") && !afuera.contains("cuil") && !afuera.contains("nacimiento") &&
              !afuera.contains("tramite"),
          "afuera no va lo sensible");
    const Datos v = Datos::deJson(j);
    CHECK(v.apellido == d.apellido && v.cuil == d.cuil && v.tipo == "dni-ar", "ida y vuelta por JSON");
}

} // namespace

int main() {
    ayudas();
    dniNuevo();
    dniViejo();
    noEsDni();
    catalogo();
    if (g_fallos == 0) std::printf("test_documentos: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
