// test_qr.cpp — el generador de codigos QR con estilo
//
//   1. Un QR valido: SVG bien formado, con la zona silenciosa y el tamaño de
//      modulos que corresponde al texto.
//   2. Los estilos no rompen el codigo: cambiar la forma de los puntos o de
//      los ojos no cambia CUANTOS modulos hay ni cuales estan prendidos.
//   3. El logo sube la correccion a la maxima SOLO (si no, tapa modulos y el
//      codigo deja de leerse) y deja su hueco.
//   4. Colores: los que no se entienden no rompen el SVG.
//   5. Lo que no entra avisa en vez de devolver algo roto.
//   6. Las cargas: WiFi, vCard, reunion, tel/sms/mail, con el escapado que
//      hace falta (un ";" en el nombre de la red partiria el codigo).
#include <clocale>
#include <cstdio>
#include <string>

#include "qr/Qr.hpp"

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

bool tiene(const std::string& h, const std::string& a) { return h.find(a) != std::string::npos; }

/// Cuenta cuantas figuras hay en el camino del cuerpo (una por modulo).
int figuras(const std::string& svg) {
    // Cada figura arranca con "M". Alcanza para comparar dos estilos entre si.
    int n = 0;
    for (std::size_t i = 0; i < svg.size(); ++i)
        if (svg[i] == 'M') ++n;
    return n;
}
} // namespace

int main() {
    std::printf("== test_qr ==\n");
    using namespace ba::qr;

    // --- 1) un QR valido ------------------------------------------------------
    {
        Estilo e;
        std::string err;
        const std::string s = svg("https://ejemplo.com/sala", e, &err);
        CHECK(!s.empty() && err.empty(), "genera");
        CHECK(s.rfind("<svg", 0) == 0 && tiene(s, "</svg>"), "es un SVG entero");
        CHECK(tiene(s, "viewBox='0 0 "), "tiene viewBox (escala sin borronearse)");
        // 21 modulos (version 1) + 4 de zona silenciosa de cada lado = 29.
        const int n = lado("https://ejemplo.com/sala", Correccion::Media);
        CHECK(n > 0, "sabe cuantos modulos son");
        CHECK(tiene(s, "viewBox='0 0 " + std::to_string(n + 8) + " " + std::to_string(n + 8) + "'"),
              "la zona silenciosa de 4 esta en el viewBox");
    }
    {
        // Sin zona silenciosa no lo lee casi ningun telefono, pero si lo
        // piden, se respeta (a veces se recorta a proposito para componer).
        Estilo e;
        e.zonaSilenciosa = 0;
        const int n = lado("hola", Correccion::Media);
        CHECK(tiene(svg("hola", e), "viewBox='0 0 " + std::to_string(n) + " " + std::to_string(n) + "'"),
              "zona silenciosa 0");
    }

    // --- 2) el estilo no cambia el codigo ---------------------------------------
    {
        const std::string t = "https://ejemplo.com/una-direccion-mas-larga";
        Estilo a;
        Estilo b;
        b.puntos = Puntos::Circulos;
        b.ojos = Ojos::Redondeados;
        const std::string sa = svg(t, a);
        const std::string sb = svg(t, b);
        CHECK(!sa.empty() && !sb.empty(), "los dos generan");
        CHECK(sa != sb, "y se ven distinto");
        // El tamaño en modulos NO cambia: el estilo es como se dibuja, no que.
        CHECK(tiene(sa, "viewBox='0 0 " + std::to_string(lado(t, Correccion::Media) + 8)) &&
                  tiene(sb, "viewBox='0 0 " + std::to_string(lado(t, Correccion::Media) + 8)),
              "el estilo no cambia el tamaño del codigo");
    }
    {
        // El degradado se declara una sola vez y se usa: sin el <defs> el SVG
        // saldria con relleno a una url que no existe (invisible).
        Estilo e;
        e.primario = "#ff0000";
        e.secundario = "#0000ff";
        const std::string s = svg("hola", e);
        CHECK(tiene(s, "<linearGradient id='g'") && tiene(s, "url(#g)"), "degradado declarado y usado");
        Estilo liso;
        liso.primario = "#ff0000";
        liso.secundario = "#ff0000"; // el mismo: no es un degradado
        CHECK(!tiene(svg("hola", liso), "linearGradient"), "dos colores iguales no son degradado");
    }
    {
        // Fondo transparente: sin el <rect> de fondo. Es lo que permite poner
        // el QR sobre una foto.
        Estilo e;
        e.fondo = "transparent";
        const std::string s = svg("hola", e);
        CHECK(!tiene(s, "<rect width='100%'"), "transparente: sin fondo");
        CHECK(tiene(svg("hola", Estilo{}), "<rect width='100%'"), "por defecto si hay fondo");
    }

    // --- 3) el logo --------------------------------------------------------------
    {
        const std::string t = "https://ejemplo.com/con-logo";
        Estilo e;
        e.correccion = Correccion::Baja; // a proposito: el logo tiene que pisarla
        e.logo = 0.2;
        const std::string s = svg(t, e);
        CHECK(!s.empty(), "genera con logo");
        // Con correccion maxima el codigo es MAS grande que con la baja: eso
        // prueba que la subio sola.
        CHECK(lado(t, Correccion::Maxima) > lado(t, Correccion::Baja), "maxima ocupa mas");
        CHECK(tiene(s, "viewBox='0 0 " + std::to_string(lado(t, Correccion::Maxima) + 8)),
              "el logo sube la correccion a maxima solo");
        // Comparar contra el MISMO nivel de correccion: si no, el codigo con
        // logo es mas grande (la subio) y tendria mas modulos igual.
        Estilo sinLogo = e;
        sinLogo.logo = 0;
        sinLogo.correccion = Correccion::Maxima;
        CHECK(figuras(s) < figuras(svg(t, sinLogo)), "el hueco del logo saltea modulos");
    }
    {
        Estilo e;
        e.logo = 0.9; // disparatado: se recorta
        CHECK(!svg("hola", e).empty(), "un logo enorme no rompe");
    }

    // --- 4) colores raros ---------------------------------------------------------
    {
        Estilo e;
        e.primario = "'><script>alert(1)</script>";
        const std::string s = svg("hola", e);
        CHECK(!tiene(s, "<script"), "un color con html no se cuela en el SVG");
        CHECK(tiene(s, "#000000"), "y se cae al color de fabrica");
        Estilo c;
        c.primario = "#abc"; // forma corta, valida
        CHECK(tiene(svg("hola", c), "#abc"), "acepta #rgb");
        Estilo nom;
        nom.primario = "rebeccapurple";
        CHECK(tiene(svg("hola", nom), "rebeccapurple"), "acepta un nombre de color");
    }

    // --- 5) lo que no entra --------------------------------------------------------
    {
        std::string err;
        CHECK(svg("", Estilo{}, &err).empty() && !err.empty(), "vacio avisa");
        const std::string enorme(8000, 'x');
        err.clear();
        CHECK(svg(enorme, Estilo{}, &err).empty() && !err.empty(), "demasiado largo avisa");
        CHECK(lado(enorme, Correccion::Media) == 0, "y lado() dice 0");
    }

    // --- 6) las cargas ---------------------------------------------------------------
    {
        CHECK(wifi("MiRed", "clave123") == "WIFI:S:MiRed;T:WPA;P:clave123;;", "wifi");
        CHECK(wifi("Libre", "", "nopass") == "WIFI:S:Libre;T:nopass;;", "wifi sin clave: sin P");
        CHECK(tiene(wifi("Red", "x", "WPA", true), "H:true;"), "wifi oculta");
        // Un ";" en el nombre partiria el codigo en dos campos.
        CHECK(wifi("Sala;1", "a:b") == "WIFI:S:Sala\\;1;T:WPA;P:a\\:b;;",
              "escapa ; y : en la red y la clave (si no, partirian el codigo en campos)");
    }
    {
        const std::string v = contacto("Ana Perez", "+5491100000000", "ana@ejemplo.com", "Estudio", "Prod");
        CHECK(v.rfind("BEGIN:VCARD", 0) == 0 && tiene(v, "END:VCARD"), "vCard entera");
        CHECK(tiene(v, "FN:Ana Perez") && tiene(v, "N:Perez;Ana;;;"), "nombre y apellido");
        CHECK(tiene(v, "TEL;TYPE=CELL:+5491100000000") && tiene(v, "EMAIL:ana@ejemplo.com"), "datos");
        CHECK(!tiene(contacto("Ana", "", "", ""), "TEL"), "lo vacio no se escribe");
    }
    {
        CHECK(reunion("meet", "abc-defg-hij") == "https://meet.google.com/abc-defg-hij", "meet");
        CHECK(reunion("zoom", "123456789", "xyz") == "https://zoom.us/j/123456789?pwd=xyz", "zoom");
        // Si ya pegaron el enlace entero, se respeta: es lo que hace la gente.
        CHECK(reunion("meet", "https://meet.google.com/x") == "https://meet.google.com/x", "enlace pegado");
    }
    {
        CHECK(telefono("+541100") == "tel:+541100", "tel");
        CHECK(sms("+541100", "hola") == "sms:+541100?body=hola", "sms con texto");
        CHECK(sms("+541100") == "sms:+541100", "sms sin texto");
        CHECK(correo("a@b.c", "Hola") == "mailto:a@b.c?subject=Hola", "mail con asunto");
        CHECK(correo("a@b.c") == "mailto:a@b.c", "mail pelado");
    }

    // --- El estilo tiene que sobrevivir el viaje por la URL ------------------
    // Un televisor por Chromecast no corre nuestro código: recibe una
    // dirección y dibuja lo que le devuelvan. Si el estilo no llega entero,
    // sale un QR VALIDO pero distinto al que se diseñó, y eso no falla en
    // ningún lado: se descubre mirando el televisor. Por eso se prueba el
    // viaje completo y no solo que el texto quepa.
    {
        Estilo e;
        e.primario = "#7c3aed";
        e.secundario = "#00ff88";
        e.fondo = "#101010";
        e.puntos = Puntos::MuyRedondeados;
        e.ojos = Ojos::Redondeados;
        e.marco = Marco::Redondeado;
        e.correccion = Correccion::Maxima;
        e.zonaSilenciosa = 6;
        e.logoIcono = "wifi";
        e.logoColor = "#ffffff";
        e.logo = 0.22;

        const Estilo v = desdeConsulta(aConsulta(e));
        CHECK(v.primario == e.primario && v.secundario == e.secundario && v.fondo == e.fondo,
              "los colores vuelven");
        CHECK(v.puntos == e.puntos && v.ojos == e.ojos && v.marco == e.marco, "las formas vuelven");
        CHECK(v.correccion == e.correccion && v.zonaSilenciosa == e.zonaSilenciosa, "correccion y zona");
        CHECK(v.logoIcono == e.logoIcono && v.logoColor == e.logoColor, "el icono vuelve");

        // El '#' de un color, sin escapar, corta la dirección y se lleva puesto
        // todo lo que venga después sin que nada falle.
        CHECK(aConsulta(e).find('#') == std::string::npos, "el # va escapado");

        // Y lo que importa de verdad: el dibujo es el MISMO de los dos lados.
        CHECK(svg("https://ejemplo.com", e) == svg("https://ejemplo.com", v),
              "el QR que sale es el que se diseño");
    }
    {
        // Una dirección sin estilo (vieja, o escrita a mano) tiene que dar un
        // código legible, no uno roto.
        const Estilo v = desdeConsulta("");
        CHECK(v.primario == "#000000" && v.fondo == "#FFFFFF", "sin estilo, el de fabrica");
        CHECK(!svg("hola", v).empty(), "y dibuja igual");
        // Un icono pedido sin hueco se veria tapado por el codigo.
        CHECK(desdeConsulta("icono=wifi").logo > 0, "un icono abre su hueco solo");
        CHECK(desdeConsulta("zona=-3").zonaSilenciosa == 4, "una zona absurda no pasa");
        // El logo del evento: viaja el pedido, no la imagen (la pone quien dibuja).
        Estilo ev;
        ev.logoDelEvento = true;
        ev.logoSrc = "data:image/png;base64,AAAA";
        const std::string q = aConsulta(ev);
        CHECK(q.find("logoEvento=1") != std::string::npos && q.find("base64") == std::string::npos,
              "logoEvento va en la dirección, la imagen no");
        CHECK(desdeConsulta(q).logoDelEvento && !desdeConsulta("").logoDelEvento, "y vuelve");
    }
    {
        // El numero del logo NO puede llevar punto decimal: el separador
        // depende del idioma del proceso, y la app corre en español. Con
        // "%.3f" mandaba "0,220", el gateway leia hasta la coma —cero— y el
        // hueco del icono desaparecia. Dos procesos del mismo programa sin
        // entenderse, y sin que nada falle.
        Estilo e;
        e.logo = 0.22;
        const std::string q = aConsulta(e);
        CHECK(q.find("logo=220") != std::string::npos, "el logo va en milesimas enteras");
        CHECK(q.find(',') == std::string::npos && q.find("logo=0.") == std::string::npos,
              "sin separador decimal, que depende del idioma");

        // Y con el idioma puesto en uno que usa coma, tiene que dar lo mismo.
        const char* antes = std::setlocale(LC_NUMERIC, nullptr);
        if (std::setlocale(LC_NUMERIC, "es_AR.UTF-8") != nullptr ||
            std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr) {
            CHECK(aConsulta(e) == q, "el mismo estilo da la misma direccion en cualquier idioma");
            std::setlocale(LC_NUMERIC, antes != nullptr ? antes : "C");
        }
        const Estilo v = desdeConsulta(q);
        CHECK(v.logo > 0.21 && v.logo < 0.23, "y vuelve entero");
    }

    if (g_fallos == 0) {
        std::printf("test_qr: OK\n");
        return 0;
    }
    std::printf("test_qr: %d fallo(s)\n", g_fallos);
    return 1;
}
