// test_device_match.cpp
//
// Pruebas de unidad del matching con puntaje (spec 9.4) y del walk del arbol
// de devnodes (spec 9.2). C++ plano, sin framework externo: un macro minimo de
// CHECK y un main() que devuelve 0 en exito y 1 en fallo. El build cablea
// src/core/tests/test_*.cpp a CTest.
//
// Casos:
//   1. ExactEndpoint gana sobre todo (un known comparte endpointId, otro
//      comparte serial; se elige el de endpoint con el puntaje mas alto).
//   2. Serial match cuando el endpoint difiere.
//   3. VidPidLocation match (locationPath compartido).
//   4. UniqueVidPid: dos known comparten vid+pid pero solo uno no asignado ->
//      matcha el no asignado; si AMBOS no asignados comparten vid+pid ->
//      AskOperator (ambiguo).
//   5. alreadyAssigned se respeta (un exact-endpoint asignado se saltea).
//   6. resolver: arbol endpoint->hub->usb(raiz) devuelve el nodo usb; un arbol
//      sin nodo usb devuelve el tope; id desconocido -> false.

#include "hw/DeviceIdentity.hpp"
#include "hw/DeviceMatcher.hpp"
#include "hw/DeviceIdentityResolver.hpp"

#include <cstdio>

using namespace ba::hw;

// Rutas de ubicacion de ejemplo (DEVPKEY_Device_LocationPaths): strings
// estables para probar el match por locationPath sin depender de Windows.
static constexpr const char* PCI_LOC = "PCIROOT(0)#PCI(3000)#USB(1)";

// --- Macro minimo de assert ------------------------------------------------

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// --- 1. ExactEndpoint gana sobre todo --------------------------------------

static void testExactEndpointWins() {
    std::printf("== testExactEndpointWins ==\n");
    DeviceIdentity observed;
    observed.endpointId = "ep-A";
    observed.serial = "SER-1";

    // known[0] comparte endpointId (nivel 1, puntaje 100).
    DeviceIdentity k0;
    k0.endpointId = "ep-A";
    k0.serial = "OTRO"; // serial distinto para que no coincida por serial

    // known[1] comparte serial (nivel 2, puntaje 80) pero endpoint distinto.
    DeviceIdentity k1;
    k1.endpointId = "ep-B";
    k1.serial = "SER-1";

    std::vector<DeviceIdentity> known = {k0, k1};
    std::vector<int> assigned;

    DeviceMatcher m;
    MatchResult r = m.match(observed, known, assigned);
    std::printf("  kind=%d index=%d score=%d\n", (int)r.kind, r.index, r.score);
    CHECK(r.kind == MatchKind::ExactEndpoint, "gana ExactEndpoint");
    CHECK(r.index == 0, "elige el known que comparte endpointId");
    CHECK(r.score == DeviceMatcher::kScoreExactEndpoint, "puntaje maximo");
}

// --- 2. Serial match cuando el endpoint difiere ----------------------------

static void testSerialMatch() {
    std::printf("== testSerialMatch ==\n");
    DeviceIdentity observed;
    observed.endpointId = "ep-NUEVO"; // endpoint distinto
    observed.serial = "SER-42";

    DeviceIdentity k0;
    k0.endpointId = "ep-VIEJO"; // endpoint distinto
    k0.serial = "SER-42"; // mismo serial

    std::vector<DeviceIdentity> known = {k0};
    std::vector<int> assigned;

    DeviceMatcher m;
    MatchResult r = m.match(observed, known, assigned);
    std::printf("  kind=%d index=%d score=%d\n", (int)r.kind, r.index, r.score);
    CHECK(r.kind == MatchKind::Serial, "match por serial");
    CHECK(r.index == 0, "elige el known con el serial");
    CHECK(r.score == DeviceMatcher::kScoreSerial, "puntaje de serial");
}

// --- 3. VidPidLocation match (locationPath compartido) ---------------------

static void testVidPidLocation() {
    std::printf("== testVidPidLocation ==\n");
    DeviceIdentity observed;
    observed.vid = 0x046D;
    observed.pid = 0x0825;
    observed.locationPaths = {PCI_LOC};

    DeviceIdentity k0;
    k0.vid = 0x046D;
    k0.pid = 0x0825;
    k0.locationPaths = {PCI_LOC}; // comparte locationPath

    std::vector<DeviceIdentity> known = {k0};
    std::vector<int> assigned;

    DeviceMatcher m;
    MatchResult r = m.match(observed, known, assigned);
    std::printf("  kind=%d index=%d score=%d\n", (int)r.kind, r.index, r.score);
    CHECK(r.kind == MatchKind::VidPidLocation, "match por vid+pid+location");
    CHECK(r.index == 0, "elige el known con locationPath compartido");
    CHECK(r.score == DeviceMatcher::kScoreVidPidLocation, "puntaje vid+pid+loc");
}

// --- 4. UniqueVidPid --------------------------------------------------------

static void testUniqueVidPid() {
    std::printf("== testUniqueVidPid ==\n");
    DeviceIdentity observed;
    observed.vid = 0x1234;
    observed.pid = 0x5678;
    // sin endpoint, sin serial, sin location: solo vid+pid.

    // known[0] y known[1] comparten vid+pid. known[0] esta asignado.
    DeviceIdentity k0;
    k0.vid = 0x1234;
    k0.pid = 0x5678;
    DeviceIdentity k1;
    k1.vid = 0x1234;
    k1.pid = 0x5678;
    std::vector<DeviceIdentity> known = {k0, k1};

    // Caso 4a: solo known[1] no asignado -> UniqueVidPid matcha known[1].
    {
        std::vector<int> assigned = {0};
        DeviceMatcher m;
        MatchResult r = m.match(observed, known, assigned);
        std::printf("  4a kind=%d index=%d score=%d\n", (int)r.kind, r.index,
                    r.score);
        CHECK(r.kind == MatchKind::UniqueVidPid, "4a: UniqueVidPid");
        CHECK(r.index == 1, "4a: elige el no asignado");
        CHECK(r.score == DeviceMatcher::kScoreUniqueVidPid, "4a: puntaje");
    }

    // Caso 4b: AMBOS no asignados comparten vid+pid -> ambiguo -> AskOperator.
    {
        std::vector<int> assigned; // ninguno asignado
        DeviceMatcher m;
        MatchResult r = m.match(observed, known, assigned);
        std::printf("  4b kind=%d index=%d score=%d\n", (int)r.kind, r.index,
                    r.score);
        CHECK(r.kind == MatchKind::AskOperator, "4b: ambiguo -> AskOperator");
        CHECK(r.index == -1, "4b: sin indice");
    }
}

// --- 5. alreadyAssigned se respeta -----------------------------------------

static void testAlreadyAssigned() {
    std::printf("== testAlreadyAssigned ==\n");
    DeviceIdentity observed;
    observed.endpointId = "ep-A";
    observed.serial = "SER-9";

    // known[0] es exact-endpoint PERO esta asignado -> se saltea.
    DeviceIdentity k0;
    k0.endpointId = "ep-A";
    k0.serial = "X";
    // known[1] matcha por serial (no asignado) -> es el mejor disponible.
    DeviceIdentity k1;
    k1.endpointId = "ep-B";
    k1.serial = "SER-9";
    std::vector<DeviceIdentity> known = {k0, k1};

    std::vector<int> assigned = {0}; // known[0] ya asignado

    DeviceMatcher m;
    MatchResult r = m.match(observed, known, assigned);
    std::printf("  kind=%d index=%d score=%d\n", (int)r.kind, r.index, r.score);
    CHECK(r.kind != MatchKind::ExactEndpoint, "el exact-endpoint asignado se saltea");
    CHECK(r.kind == MatchKind::Serial, "cae al mejor disponible (serial)");
    CHECK(r.index == 1, "elige el known no asignado");
}

// --- 6. resolver: walk del arbol -------------------------------------------

// Helper: un nodo USB raiz + hub + endpoint.
static void testResolverUsbAncestor() {
    std::printf("== testResolverUsbAncestor ==\n");
    DevNode usb;
    usb.id = "usb";
    usb.parent = ""; // raiz
    usb.isUsb = true;
    usb.vid = 0xAAAA;
    usb.pid = 0xBBBB;

    DevNode hub;
    hub.id = "hub";
    hub.parent = "usb";
    hub.isUsb = false;

    DevNode ep;
    ep.id = "ep";
    ep.parent = "hub";
    ep.isUsb = false;

    std::vector<DevNode> nodes = {usb, hub, ep};

    DeviceIdentityResolver r;
    DevNode out;
    bool ok = r.resolveUsbAncestor("ep", nodes, out);
    std::printf("  ok=%d out.id=%s out.isUsb=%d\n", (int)ok, out.id.c_str(),
                (int)out.isUsb);
    CHECK(ok, "resuelve el ancestro");
    CHECK(out.id == "usb", "devuelve el nodo USB");
    CHECK(out.isUsb, "el nodo devuelto es USB");
    CHECK(out.vid == 0xAAAA && out.pid == 0xBBBB, "vid/pid del nodo USB");
}

static void testResolverNoUsbReturnsTopmost() {
    std::printf("== testResolverNoUsbReturnsTopmost ==\n");
    // Arbol sin ningun nodo USB: a (raiz) <- b <- c.
    DevNode a;
    a.id = "a";
    a.parent = ""; // raiz
    a.isUsb = false;

    DevNode b;
    b.id = "b";
    b.parent = "a";
    b.isUsb = false;

    DevNode c;
    c.id = "c";
    c.parent = "b";
    c.isUsb = false;

    std::vector<DevNode> nodes = {a, b, c};

    DeviceIdentityResolver r;
    DevNode out;
    bool ok = r.resolveUsbAncestor("c", nodes, out);
    std::printf("  ok=%d out.id=%s\n", (int)ok, out.id.c_str());
    CHECK(ok, "resuelve aunque no haya USB");
    CHECK(out.id == "a", "devuelve el nodo tope (raiz)");
}

static void testResolverUnknownId() {
    std::printf("== testResolverUnknownId ==\n");
    DevNode a;
    a.id = "a";
    a.parent = "";
    a.isUsb = false;
    std::vector<DevNode> nodes = {a};

    DeviceIdentityResolver r;
    DevNode out;
    out.id = "sentinela";
    bool ok = r.resolveUsbAncestor("no-existe", nodes, out);
    std::printf("  ok=%d out.id=%s\n", (int)ok, out.id.c_str());
    CHECK(!ok, "id desconocido -> false");
    CHECK(out.id == "sentinela", "no modifica out en fallo");
}

static void testResolverEmptyTable() {
    std::printf("== testResolverEmptyTable ==\n");
    std::vector<DevNode> nodes;
    DeviceIdentityResolver r;
    DevNode out;
    bool ok = r.resolveUsbAncestor("cualquiera", nodes, out);
    std::printf("  ok=%d\n", (int)ok);
    CHECK(!ok, "tabla vacia -> false");
}

int main() {
    testExactEndpointWins();
    testSerialMatch();
    testVidPidLocation();
    testUniqueVidPid();
    testAlreadyAssigned();
    testResolverUsbAncestor();
    testResolverNoUsbReturnsTopmost();
    testResolverUnknownId();
    testResolverEmptyTable();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
