// ============================================================================
// src/app/tests/test_registro_conversacion.cpp — el guion hablado del tótem
//
//   1. Los normalizadores, uno por uno, con lo que de verdad devuelve una voz
//      a texto en castellano rioplatense: correos dichos («punto», «arroba»,
//      «guion bajo»), teléfonos en palabras o en cifras, números, sí/no con las
//      variantes de acá, nombres con «me llamo» y mayúsculas, opciones.
//   2. Cómo se repite y se muestra cada dato.
//   3. La conversación entera: saludo (con y sin cara conocida), confirmar,
//      corregir, reintentos y salteo, lo tipeado, silencio, cancelar, ya
//      registrado, sin confirmar, sin campos.
// ============================================================================
#include <cstdio>
#include <string>

#include "registro/Conversacion.hpp"

using namespace ba::registro;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

std::string correo(const std::string& dicho) {
    std::string out;
    return entenderCorreo(dicho, out) ? out : "<no>";
}
std::string telefono(const std::string& dicho) {
    std::string out;
    return entenderTelefono(dicho, out) ? out : "<no>";
}
long long numero(const std::string& dicho) {
    long long out = -1;
    return entenderNumero(dicho, out) ? out : -1;
}
std::string nombre(const std::string& dicho) {
    std::string out;
    return entenderNombre(dicho, out) ? out : "<no>";
}
bool contiene(const std::string& s, const std::string& x) { return s.find(x) != std::string::npos; }

void normalizadores() {
    // --- Correo
    CHECK(correo("jorge punto ortega arroba gmail punto com") == "jorge.ortega@gmail.com", "correo dicho");
    CHECK(correo("Jorge punto Ortega arroba Gmail punto com.") == "jorge.ortega@gmail.com", "mayúsculas y punto final");
    CHECK(correo("mi correo es ana guion bajo perez arroba hotmail punto com punto ar") == "ana_perez@hotmail.com.ar",
          "con «mi correo es», guion bajo y .com.ar");
    CHECK(correo("jorge.ortega@gmail.com") == "jorge.ortega@gmail.com", "ya escrito");
    CHECK(correo("  Jorge.Ortega@Gmail.COM ") == "jorge.ortega@gmail.com", "tipeado con mayúsculas");
    CHECK(correo("juan arroba gmail") == "juan@gmail.com", "gmail sin punto com se completa");
    CHECK(correo("juan cinco arroba yahoo punto com") == "juan5@yahoo.com", "dígito en palabra");
    CHECK(correo("juan 85 arroba outlook punto com") == "juan85@outlook.com", "dígitos en cifras");
    CHECK(correo("maria guion lopez arroba empresa punto com punto ar gracias") == "maria-lopez@empresa.com.ar",
          "guion y «gracias» al final");
    CHECK(correo("el mail es pepe arroba fibertel") == "pepe@fibertel.com", "fibertel");
    CHECK(correo("j o r g e arroba gmail punto com") == "jorge@gmail.com", "deletreado letra por letra");
    CHECK(correo("jorge ortega gmail punto com") == "<no>", "sin arroba no es un correo");
    CHECK(correo("arroba gmail punto com") == "<no>", "sin nada antes de la arroba");
    CHECK(correo("juan arroba arroba gmail punto com") == "<no>", "dos arrobas");
    CHECK(correo("juan arroba empresa") == "<no>", "dominio desconocido sin punto");
    CHECK(correo("juan arroba empresa punto c") == "<no>", "terminación de una letra");
    CHECK(correo("no sé") == "<no>", "una respuesta cualquiera");

    // --- Teléfono
    CHECK(telefono("once cuatro cinco seis siete ocho nueve cero uno") == "1145678901", "en palabras");
    CHECK(telefono("11 4567 8901") == "1145678901", "en cifras con espacios");
    CHECK(telefono("11-4567-8901.") == "1145678901", "con guiones y punto");
    CHECK(telefono("mi número es once cuarenta y cinco sesenta y siete ochenta y nueve cero uno") == "1145678901",
          "de a dos, con «y»");
    CHECK(telefono("más cincuenta y cuatro nueve once cuatro cinco seis siete ocho nueve cero uno") == "+5491145678901",
          "con «más» y código de país");
    CHECK(telefono("+54 9 11 4567 8901") == "+5491145678901", "con + escrito");
    CHECK(telefono("doble cinco cuatro tres dos uno cero nueve") == "55432109", "doble cinco");
    CHECK(telefono("cuatro quinientos cuarenta y dos treinta") == "454230", "centenas");
    CHECK(telefono("(0341) 15-555-1234") == "0341155551234", "con paréntesis y el 15");
    CHECK(telefono("cero once") == "<no>", "el cero al principio, pero muy corto");
    CHECK(telefono("cero once cuatro cinco seis siete") == "0114567", "el cero al principio se conserva");
    CHECK(telefono("dale") == "<no>", "sin dígitos");
    CHECK(telefono("uno dos tres") == "<no>", "muy corto");
    CHECK(telefono("1234567890123456") == "<no>", "muy largo");
}

void numerosYSiNo() {
    CHECK(numero("35") == 35, "cifra");
    CHECK(numero("tengo 35 años") == 35, "cifra con relleno");
    CHECK(numero("treinta y cinco") == 35, "en palabras");
    CHECK(numero("treinta y cinco.") == 35, "con punto final");
    CHECK(numero("dos mil veinticinco") == 2025, "miles");
    CHECK(numero("mil quinientos") == 1500, "mil solo");
    CHECK(numero("un millón doscientos mil") == 1200000, "millones");
    CHECK(numero("2.500") == 2500, "separador de miles");
    CHECK(numero("ninguno") == 0, "ninguno es cero");
    CHECK(numero("somos tres") == 3, "somos tres");
    CHECK(numero("ciento veinte") == 120, "ciento veinte");
    CHECK(numero("no sé") == -1, "sin número");

    CHECK(entenderSiNo("sí") == 1, "sí");
    CHECK(entenderSiNo("Sí.") == 1, "Sí con punto");
    CHECK(entenderSiNo("dale") == 1, "dale");
    CHECK(entenderSiNo("de una") == 1, "de una");
    CHECK(entenderSiNo("joya") == 1, "joya");
    CHECK(entenderSiNo("obvio") == 1, "obvio");
    CHECK(entenderSiNo("sí, está bien") == 1, "sí, está bien");
    CHECK(entenderSiNo("perfecto, gracias") == 1, "perfecto");
    CHECK(entenderSiNo("no") == 0, "no");
    CHECK(entenderSiNo("No.") == 0, "No con punto");
    CHECK(entenderSiNo("ni ahí") == 0, "ni ahí");
    CHECK(entenderSiNo("para nada") == 0, "para nada");
    CHECK(entenderSiNo("no, no está bien") == 0, "no está bien");
    CHECK(entenderSiNo("está mal") == 0, "está mal");
    CHECK(entenderSiNo("no es así") == 0, "no es así");
    CHECK(entenderSiNo("no sé") == -1, "no sé no es un no");
    CHECK(entenderSiNo("no, sí") == -1, "contradictorio");
    CHECK(entenderSiNo("la verdad que el martes") == -1, "otra cosa");
    CHECK(entenderSiNo("") == -1, "vacío");
}

void nombresYOpciones() {
    CHECK(nombre("me llamo jorge ortega") == "Jorge Ortega", "me llamo");
    CHECK(nombre("Hola, soy Ana María de la Fuente.") == "Ana María de la Fuente", "hola soy, partículas");
    CHECK(nombre("mi nombre es JOSÉ MARTÍNEZ") == "José Martínez", "mayúsculas con acento");
    CHECK(nombre("ángela del valle") == "Ángela del Valle", "acento en la inicial");
    CHECK(nombre("Pedro") == "Pedro", "solo el nombre");
    CHECK(nombre("bueno, me llamo Lucía Gómez gracias") == "Lucía Gómez", "bueno ... gracias");
    CHECK(nombre("Ana Bueno") == "Ana Bueno", "un apellido que es palabra común");
    CHECK(nombre("sí") == "<no>", "un sí no es un nombre");
    CHECK(nombre("dale") == "<no>", "dale no es un nombre");
    CHECK(nombre("no quiero") == "<no>", "no quiero no es un nombre");
    CHECK(nombre("1234") == "<no>", "números no");
    CHECK(nombre("cinco") == "<no>", "un número dicho no");
    CHECK(nombre("") == "<no>", "vacío");
    CHECK(nombre("me llamo") == "<no>", "solo el prefijo");

    const std::vector<std::string> ops{"Sala grande", "Sala chica", "Online"};
    std::string o;
    CHECK(entenderOpcion("la sala grande", ops, o) && o == "Sala grande", "por texto entero");
    CHECK(entenderOpcion("online", ops, o) && o == "Online", "una palabra");
    CHECK(entenderOpcion("la chica", ops, o) && o == "Sala chica", "un pedazo que está en una sola");
    CHECK(entenderOpcion("la segunda", ops, o) && o == "Sala chica", "por orden");
    CHECK(entenderOpcion("la última", ops, o) && o == "Online", "la última");
    CHECK(!entenderOpcion("sala", ops, o), "ambiguo: está en dos");
    CHECK(!entenderOpcion("el martes", ops, o), "nada que ver");

    std::string t;
    CHECK(entenderTexto("trabajo en una ONG.", t) && t == "Trabajo en una ONG", "texto: mayúscula y sin punto");
    CHECK(entenderTexto("ONG del barrio", t) && t == "ONG del barrio", "texto: sigla al principio");
    CHECK(!entenderTexto("  . ", t), "texto vacío");
}

void paraDecirYMostrar() {
    Campo c;
    c.tipo = TipoCampo::Correo;
    CHECK(paraDecir(c, "jorge.ortega@gmail.com") == "jorge punto ortega arroba gmail punto com", "decir correo");
    CHECK(paraDecir(c, "ana_p2@x.com") == "ana guion bajo p dos arroba x punto com", "decir correo con _ y dígito");
    c.tipo = TipoCampo::Telefono;
    CHECK(paraDecir(c, "1145678901") == "uno uno, cuatro cinco seis siete, ocho nueve cero uno", "decir teléfono");
    CHECK(paraDecir(c, "+5491145678901") == "más cinco, cuatro nueve uno uno, cuatro cinco seis siete, ocho nueve cero uno",
          "decir teléfono con +");
    CHECK(paraMostrar(c, "1145678901") == "11 4567 8901", "mostrar teléfono");
    CHECK(paraMostrar(c, "+5491145678901") == "+5 4911 4567 8901", "mostrar teléfono con +");
    c.tipo = TipoCampo::SiNo;
    CHECK(paraDecir(c, true) == "que sí", "decir sí");
    CHECK(paraMostrar(c, false) == "No", "mostrar no");
    c.tipo = TipoCampo::Numero;
    CHECK(paraMostrar(c, 3) == "3", "mostrar número");
    CHECK(paraMostrar(c, json()) == "", "sin dato");
}

Totem totemDePrueba() {
    Totem t = totemNuevo("entrada", "Entrada");
    Campo tel;
    tel.clave = "telefono";
    tel.etiqueta = "Teléfono";
    tel.tipo = TipoCampo::Telefono;
    t.campos.push_back(tel);
    return t; // nombre (obligatorio), correo, telefono
}

void conversacionCompleta() {
    Conversacion cv(totemDePrueba(), "Congreso de Riego");
    CHECK(cv.estado() == Conversacion::Estado::Esperando, "arranca esperando");
    CHECK(cv.oido("hola").decir.empty(), "lo oído sin empezar no hace nada");

    auto r = cv.empezar({});
    CHECK(contiene(r.decir, "Hola. Te damos la bienvenida a Congreso de Riego."), "saludo sin nombre");
    CHECK(contiene(r.decir, "¿Cómo te llamás?"), "y pregunta el nombre");
    CHECK(r.pedir == "nombre" && r.campos, "pide nombre y muestra los campos");
    CHECK(cv.paso() == "preguntando Nombre", "paso legible");
    CHECK(cv.pedido()["tipo"] == "nombre", "pedido con su tipo");
    CHECK(cv.empezar({}).decir.empty(), "empezar dos veces no hace nada");

    r = cv.oido("me llamo jorge ortega");
    CHECK(r.decir == "Anoté Jorge Ortega. ¿Está bien?", "confirma el nombre");
    CHECK(cv.estado() == Conversacion::Estado::Confirmando, "confirmando");
    r = cv.oido("dale");
    CHECK(contiene(r.decir, "Perfecto.") && contiene(r.decir, "correo"), "acepta y pide el correo");
    CHECK(r.pedir == "correo" && r.campos, "pide correo");
    CHECK(cv.datos()["nombre"] == "Jorge Ortega", "nombre anotado");

    r = cv.oido("jorge punto ortega arroba gmail punto com");
    CHECK(r.decir == "Anoté jorge punto ortega arroba gmail punto com. ¿Está bien?", "repite el correo dicho");
    r = cv.oido("no");
    CHECK(contiene(r.decir, "Decímelo de nuevo") && cv.estado() == Conversacion::Estado::Preguntando, "no -> de nuevo");
    r = cv.oido("jorge punto ortegas arroba gmail punto com");
    CHECK(contiene(r.decir, "ortegas"), "nueva propuesta");
    // Corrige diciendo directamente el dato, sin «no».
    r = cv.oido("jorge punto ortega arroba gmail punto com");
    CHECK(contiene(r.decir, "Anoté jorge punto ortega arroba"), "corrección directa en la confirmación");
    r = cv.oido("eh, el martes");
    CHECK(contiene(r.decir, "Decime sí o no"), "ni sí ni no: vuelve a preguntar");
    r = cv.oido("sí");
    CHECK(cv.datos()["correo"] == "jorge.ortega@gmail.com", "correo anotado");
    CHECK(r.pedir == "telefono", "pide teléfono");

    // Teléfono: lo tipea en la pantalla (no se confirma) y termina.
    r = cv.escrito("telefono", "11 4567-8901");
    CHECK(cv.datos()["telefono"] == "1145678901", "teléfono tipeado");
    CHECK(r.guardar && r.terminar, "con todo, guarda y termina");
    CHECK(contiene(r.decir, "Gracias, ya lo anoté.") && contiene(r.decir, "¡Que disfrutes Congreso de Riego!"),
          "despedida");
    CHECK(cv.estado() == Conversacion::Estado::Terminada, "terminada");
    CHECK(cv.nombre() == "Jorge Ortega", "nombre dicho");
    const json campos = cv.camposParaMostrar();
    CHECK(campos.size() == 3 && campos[2]["valor"] == "11 4567 8901" && campos[2]["listo"] == true, "campos para mostrar");
    CHECK(cv.oido("hola").decir.empty(), "terminada no contesta");
    CHECK(cv.pedido().is_null(), "sin pedido al terminar");
}

void caraConocida() {
    Conversacion cv(totemDePrueba(), "Congreso");
    Conversacion::Cara ana{"p-ana", "Ana María Pérez", false};
    auto r = cv.empezar(ana);
    CHECK(contiene(r.decir, "Hola Ana."), "saluda por el nombre de pila");
    CHECK(r.pedir == "correo", "el nombre no lo pregunta: lo sabe por la cara");
    CHECK(cv.datos()["nombre"] == "Ana María Pérez", "nombre puesto desde la ficha");

    // Sin «reconocer», la saluda sin nombre y le pregunta el nombre.
    Totem t = totemDePrueba();
    t.reconocer = false;
    Conversacion cv2(t, "Congreso");
    r = cv2.empezar(ana);
    CHECK(contiene(r.decir, "Hola.") && !contiene(r.decir, "Ana"), "sin reconocer no la nombra");
    CHECK(r.pedir == "nombre", "y le pregunta el nombre");
    CHECK(cv2.cara().personaId == "p-ana", "pero la cara queda para vincularla");

    // Ya registrada en este evento.
    Conversacion cv3(totemDePrueba(), "Congreso");
    r = cv3.empezar({"p-ana", "Ana", true});
    CHECK(contiene(r.decir, "Ya tenés tu registro en Congreso"), "ya registrada");
    CHECK(r.terminar && !r.guardar, "termina sin guardar");

    // Se empezó con el toque y la cara se conoció después.
    Conversacion cv4(totemDePrueba(), "Congreso");
    cv4.empezar({});
    cv4.conocerCara({"p-luis", "Luis", false});
    CHECK(cv4.cara().personaId == "p-luis", "cara conocida después");
}

void reintentosYSalteo() {
    Conversacion cv(totemDePrueba(), "X");
    cv.empezar({"p", "Pepe", false}); // pide correo (no obligatorio)
    auto r = cv.oido("no me acuerdo bien cuál era");
    // «no me acuerdo» saltea: el correo no es obligatorio.
    CHECK(contiene(r.decir, "lo salteamos") && r.pedir == "telefono", "saltea el correo");
    r = cv.oido("el martes a la tarde");
    CHECK(contiene(r.decir, "No me quedó claro el número") && !contiene(r.decir, "escribilo"), "primer reintento");
    r = cv.oido("la verdad no sé");
    CHECK(contiene(r.decir, "Si preferís, escribilo en la pantalla."), "segundo reintento ofrece tipear");
    r = cv.oido("qué cosa");
    CHECK(contiene(r.decir, "lo dejamos en blanco") && r.terminar && r.guardar, "tercero: lo saltea y termina");
    CHECK(!cv.datos().contains("telefono") && !cv.datos().contains("correo"), "sin los salteados");

    // Un obligatorio no se saltea.
    Conversacion cv2(totemDePrueba(), "X");
    cv2.empezar({});
    r = cv2.oido("no quiero");
    CHECK(contiene(r.decir, "Este dato lo necesitamos"), "obligatorio no se saltea");
    cv2.oido("12345");
    cv2.oido("678");
    r = cv2.oido("999");
    CHECK(contiene(r.decir, "Escribilo en la pantalla") && cv2.estado() == Conversacion::Estado::Preguntando,
          "obligatorio: sigue pidiendo, por la pantalla");
    r = cv2.oido("¿cómo?");
    CHECK(contiene(r.decir, "¿Cómo te llamás?"), "«¿cómo?» repite la pregunta");
    r = cv2.escrito("nombre", "lucía gómez");
    CHECK(cv2.datos()["nombre"] == "Lucía Gómez" && r.pedir == "correo", "lo tipeado se anota prolijo");
    r = cv2.escrito("correo", "esto no es un correo");
    CHECK(contiene(r.decir, "no parece un correo") && r.pedir == "correo", "tipeado inválido");
    r = cv2.escrito("nada", "x");
    CHECK(r.decir.empty() && !r.campos, "una clave que no existe no hace nada");
}

void silencioYCancelar() {
    Conversacion cv(totemDePrueba(), "X");
    cv.empezar({});
    auto r = cv.silencio();
    CHECK(contiene(r.decir, "¿Seguís ahí?") && contiene(r.decir, "¿Cómo te llamás?"), "primer silencio");
    cv.oido("Pedro Gómez"); // habló: el contador vuelve a cero
    r = cv.silencio();
    CHECK(contiene(r.decir, "¿Seguís ahí? ¿Está bien Pedro Gómez?"), "silencio confirmando");
    r = cv.silencio();
    CHECK(r.terminar && !r.guardar, "dos silencios seguidos: termina sin guardar");

    Conversacion cv2(totemDePrueba(), "X");
    cv2.empezar({});
    r = cv2.oido("cancelar");
    CHECK(contiene(r.decir, "cancelé") && r.terminar && !r.guardar, "cancelar hablado");
    CHECK(cv2.cancelar().decir.empty(), "cancelar terminada no hace nada");

    Conversacion cv3(totemDePrueba(), "X");
    CHECK(cv3.cancelar().decir.empty(), "cancelar sin empezar no hace nada");
    CHECK(cv3.silencio().decir.empty(), "silencio sin empezar no hace nada");
}

void sinConfirmarYTipos() {
    Totem t = totemNuevo("t", "T");
    t.confirmar = false;
    Campo n;
    n.clave = "acompanantes";
    n.etiqueta = "Acompañantes";
    n.tipo = TipoCampo::Numero;
    n.pregunta = "¿Cuántas personas vienen con vos?";
    Campo s;
    s.clave = "newsletter";
    s.tipo = TipoCampo::SiNo;
    s.pregunta = "¿Querés recibir novedades?";
    s.obligatorio = true;
    Campo o;
    o.clave = "sala";
    o.tipo = TipoCampo::Opcion;
    o.opciones = {"Grande", "Chica"};
    o.pregunta = "¿A qué sala vas?";
    t.campos = {n, s, o};
    Conversacion cv(t, "Recital");
    auto r = cv.empezar({});
    CHECK(contiene(r.decir, "¿Cuántas personas vienen con vos?"), "pregunta propia");
    r = cv.oido("ninguna");
    CHECK(cv.datos()["acompanantes"] == 0 && contiene(r.decir, "Perfecto. ¿Querés recibir novedades?"),
          "sin confirmar pasa derecho");
    r = cv.oido("no quiero");
    CHECK(cv.datos()["newsletter"] == false, "en un sí/no, «no quiero» es la respuesta");
    CHECK(contiene(r.decir, "Puede ser: Grande o Chica."), "las opciones se dicen");
    r = cv.oido("la chica");
    CHECK(cv.datos()["sala"] == "Chica" && r.guardar, "opción y termina");

    // Sin campos: saluda, despide y guarda (queda acreditado).
    Totem vacio = totemNuevo("v", "V");
    vacio.campos.clear();
    Conversacion cv2(vacio, "Feria");
    r = cv2.empezar({"p", "Ana", false});
    CHECK(contiene(r.decir, "Hola Ana") && contiene(r.decir, "¡Que disfrutes Feria!") && r.guardar && r.terminar,
          "sin campos guarda enseguida");

    // El sí/no se confirma solo, aunque el tótem confirme lo demás.
    Totem t2 = totemNuevo("t2", "T2");
    t2.campos = {s};
    Conversacion cv3(t2, "X");
    cv3.empezar({});
    r = cv3.oido("de una");
    CHECK(cv3.datos()["newsletter"] == true && r.guardar, "sí/no sin confirmar");
}

} // namespace

int main() {
    normalizadores();
    numerosYSiNo();
    nombresYOpciones();
    paraDecirYMostrar();
    conversacionCompleta();
    caraConocida();
    reintentosYSalteo();
    silencioYCancelar();
    sinConfirmarYTipos();
    if (g_fallos == 0) std::printf("test_registro_conversacion: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
