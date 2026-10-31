// ============================================================================
// src/app/escritorio/control_uia.cpp — UI Automation: ver y tocar los
// elementos de las ventanas (ver control.hpp)
//
// En vez de adivinar coordenadas en una captura, el asistente LEE la ventana:
// los botones, campos, casillas, enlaces, pestañas… con su nombre, su rol, su
// valor y dónde están (Windows los expone a los lectores de pantalla; las
// páginas de Chrome y Edge también). Después les hace clic o les escribe POR
// NÚMERO (el de la última lectura) o POR NOMBRE, con la acción del propio
// elemento (invocar, marcar, elegir, desplegar, poner el valor): anda aunque
// la ventana esté tapada o en otro monitor, y si el elemento no la tiene, un
// clic de mouse en su centro.
//
//   system_ui_leer      los elementos de una ventana (los que se pueden tocar,
//                        o todos), numerados
//   system_ui_clic      clic en uno (número o nombre)
//   system_ui_escribir  le pone texto a un campo
//   system_ui_texto     el texto de la ventana (una página, un documento)
//   system_ui_esperar   hasta que aparezca un elemento con ese nombre
// ============================================================================
#include "escritorio/control.hpp"
#include "platform/Hilo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_WIN32)
// Con WIN32_LEAN_AND_MEAN (control.hpp) no viene OLE: hace falta antes de UI Automation.
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#endif

namespace bactl {

json herramientasUia() {
    json t = json::array();
    const json ventana = {{"type", "integer"}, {"description", ba::i18n::traducir("mcp", "id de la ventana (system_ventanas); sin esto, la que tiene el foco")}};
    const json titulo = {{"type", "string"}, {"description", ba::i18n::traducir("mcp", "o parte del título de la ventana")}};
    t.push_back(herramienta(
        "system_ui_leer",
        ba::i18n::traducir("mcp", "LEE lo que hay en una ventana, como un lector de pantalla (UI Automation): los elementos que se pueden "
        "tocar —botones, campos, casillas, desplegables, menús, enlaces, pestañas, ítems— con número, rol, nombre, "
        "valor, estado (foco, marcado, desplegado, elegido), atajo y el centro de lo que se ve; las zonas que se "
        "desplazan; y en un navegador (Chrome, Edge) la página con su texto. Un diálogo modal: solo lo del diálogo. "
        "Mejor que adivinar en una captura: después system_ui_clic / system_ui_escribir con el número. Sin "
        "`ventana`, la del foco; `todas`: todas las visibles (la del foco primero). `todos`: también textos e "
        "imágenes con nombre; `texto` (true): el texto de la página; `filtro`: solo lo que tenga eso en el "
        "nombre; `max` (500). Permiso: pantalla."),
        esquema({{"ventana", ventana},
                 {"titulo", titulo},
                 {"todas", {{"type", "boolean"}}},
                 {"todos", {{"type", "boolean"}}},
                 {"texto", {{"type", "boolean"}}},
                 {"filtro", {{"type", "string"}}},
                 {"max", {{"type", "integer"}, {"minimum", 1}, {"maximum", 3000}}}})));
    t.push_back(herramienta(
        "system_ui_clic",
        ba::i18n::traducir("mcp", "Clic en un elemento: `n` (el número de la última system_ui_leer) o `nombre` (parte del nombre; `rol` para "
        "desempatar: boton, enlace, casilla, item, pestaña…) en la ventana. Usa la acción del elemento (invocar, "
        "marcar, elegir, desplegar), que anda aunque la ventana esté tapada; si no tiene, un clic de mouse en su "
        "centro. `con_mouse`: true fuerza el clic de mouse (`boton`: izq, der; `doble`). Permiso: mouse."),
        esquema({{"n", {{"type", "integer"}}},
                 {"nombre", {{"type", "string"}}},
                 {"rol", {{"type", "string"}}},
                 {"ventana", ventana},
                 {"titulo", titulo},
                 {"con_mouse", {{"type", "boolean"}}},
                 {"boton", {{"type", "string"}, {"enum", {"izq", "der"}}}},
                 {"doble", {{"type", "boolean"}}}})));
    t.push_back(herramienta(
        "system_ui_escribir",
        ba::i18n::traducir("mcp", "Le pone `texto` a un campo: `n` (de system_ui_leer) o `nombre`. Pone el valor directo si el campo lo deja; "
        "si no, le da el foco y lo escribe. `reemplazar` (true): borra lo que había; `enter`: aprieta Enter al final. "
        "Permiso: teclado."),
        esquema({{"n", {{"type", "integer"}}},
                 {"nombre", {{"type", "string"}}},
                 {"rol", {{"type", "string"}}},
                 {"texto", {{"type", "string"}}},
                 {"reemplazar", {{"type", "boolean"}}},
                 {"enter", {{"type", "boolean"}}},
                 {"ventana", ventana},
                 {"titulo", titulo}},
                {"texto"})));
    t.push_back(herramienta(
        "system_ui_texto",
        ba::i18n::traducir("mcp", "El TEXTO de una ventana: lo que dice una página web (Chrome, Edge), un documento, un diálogo. `max` "
        "caracteres (20000). Permiso: pantalla."),
        esquema({{"ventana", ventana}, {"titulo", titulo}, {"max", {{"type", "integer"}, {"minimum", 100}, {"maximum", 200000}}}})));
    t.push_back(herramienta(
        "system_ui_esperar",
        ba::i18n::traducir("mcp", "Espera hasta que en la ventana aparezca un elemento cuyo nombre contenga `nombre` (y `rol` si se da): un "
        "botón que se habilita, un mensaje, una página que terminó de cargar. `ms` lo más que espera (10000, hasta "
        "30000). Permiso: pantalla."),
        esquema({{"nombre", {{"type", "string"}}},
                 {"rol", {{"type", "string"}}},
                 {"ventana", ventana},
                 {"titulo", titulo},
                 {"ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 30000}}}},
                {"nombre"})));
    return t;
}

#if defined(_WIN32)

namespace {

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    explicit Com(T* x) : p(x) {}
    Com(const Com& o) : p(o.p) {
        if (p) p->AddRef();
    }
    Com& operator=(const Com& o) {
        if (this != &o) {
            if (o.p) o.p->AddRef();
            if (p) p->Release();
            p = o.p;
        }
        return *this;
    }
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() {
        if (p) p->Release();
        p = nullptr;
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

IUIAutomation* uia() {
    static IUIAutomation* u = []() {
        IUIAutomation* x = nullptr;
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                         reinterpret_cast<void**>(&x));
        return x;
    }();
    return u;
}

std::string bstr(BSTR b) {
    if (b == nullptr) return {};
    std::string s = aU(std::wstring(b, SysStringLen(b)));
    SysFreeString(b);
    return s;
}

std::string minus(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* rolDe(CONTROLTYPEID t) {
    switch (t) {
        case UIA_ButtonControlTypeId: return "boton";
        case UIA_SplitButtonControlTypeId: return "boton";
        case UIA_CalendarControlTypeId: return "calendario";
        case UIA_CheckBoxControlTypeId: return "casilla";
        case UIA_ComboBoxControlTypeId: return "desplegable";
        case UIA_EditControlTypeId: return "campo";
        case UIA_HyperlinkControlTypeId: return "enlace";
        case UIA_ImageControlTypeId: return "imagen";
        case UIA_ListItemControlTypeId: return "item";
        case UIA_ListControlTypeId: return "lista";
        case UIA_MenuControlTypeId: return "menu";
        case UIA_MenuBarControlTypeId: return "barra_menu";
        case UIA_MenuItemControlTypeId: return "opcion_menu";
        case UIA_ProgressBarControlTypeId: return "progreso";
        case UIA_RadioButtonControlTypeId: return "opcion";
        case UIA_ScrollBarControlTypeId: return "barra_desplazamiento";
        case UIA_SliderControlTypeId: return "deslizador";
        case UIA_SpinnerControlTypeId: return "numero";
        case UIA_StatusBarControlTypeId: return "barra_estado";
        case UIA_TabControlTypeId: return "pestañas";
        case UIA_TabItemControlTypeId: return "pestaña";
        case UIA_TextControlTypeId: return "texto";
        case UIA_ToolBarControlTypeId: return "barra_herramientas";
        case UIA_ToolTipControlTypeId: return "ayuda";
        case UIA_TreeControlTypeId: return "arbol";
        case UIA_TreeItemControlTypeId: return "item_arbol";
        case UIA_GroupControlTypeId: return "grupo";
        case UIA_DataGridControlTypeId: return "tabla";
        case UIA_TableControlTypeId: return "tabla";
        case UIA_DataItemControlTypeId: return "celda";
        case UIA_DocumentControlTypeId: return "documento";
        case UIA_WindowControlTypeId: return "ventana";
        case UIA_PaneControlTypeId: return "panel";
        case UIA_HeaderControlTypeId: return "encabezados";
        case UIA_HeaderItemControlTypeId: return "encabezado";
        case UIA_TitleBarControlTypeId: return "barra_titulo";
        case UIA_SeparatorControlTypeId: return "separador";
        default: return "otro";
    }
}

/// Lo que se puede tocar (lo que lee system_ui_leer sin `todos`).
bool tocable(CONTROLTYPEID t) {
    switch (t) {
        case UIA_ButtonControlTypeId:
        case UIA_SplitButtonControlTypeId:
        case UIA_CheckBoxControlTypeId:
        case UIA_ComboBoxControlTypeId:
        case UIA_EditControlTypeId:
        case UIA_HyperlinkControlTypeId:
        case UIA_ListItemControlTypeId:
        case UIA_MenuItemControlTypeId:
        case UIA_RadioButtonControlTypeId:
        case UIA_SliderControlTypeId:
        case UIA_SpinnerControlTypeId:
        case UIA_TabItemControlTypeId:
        case UIA_TreeItemControlTypeId:
        case UIA_DataItemControlTypeId:
        case UIA_HeaderItemControlTypeId: return true;
        default: return false;
    }
}

/// Lo que se sabe de un elemento (leído del caché de FindAllBuildCache).
struct Info {
    std::string nombre, rol, valor, idAuto;
    RECT r{};
    bool habilitado = true;
    CONTROLTYPEID tipo = 0;
};

Info infoDe(IUIAutomationElement* e, bool delCache) {
    Info i;
    BSTR b = nullptr;
    if (SUCCEEDED(delCache ? e->get_CachedName(&b) : e->get_CurrentName(&b))) i.nombre = bstr(b);
    b = nullptr;
    if (SUCCEEDED(delCache ? e->get_CachedAutomationId(&b) : e->get_CurrentAutomationId(&b))) i.idAuto = bstr(b);
    if (delCache) {
        e->get_CachedControlType(&i.tipo);
        e->get_CachedBoundingRectangle(&i.r);
    } else {
        e->get_CurrentControlType(&i.tipo);
        e->get_CurrentBoundingRectangle(&i.r);
    }
    BOOL h = TRUE;
    if (SUCCEEDED(delCache ? e->get_CachedIsEnabled(&h) : e->get_CurrentIsEnabled(&h))) i.habilitado = h != FALSE;
    i.rol = rolDe(i.tipo);
    VARIANT v;
    VariantInit(&v);
    const HRESULT hv = delCache ? e->GetCachedPropertyValue(UIA_ValueValuePropertyId, &v)
                                : e->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &v);
    if (SUCCEEDED(hv) && v.vt == VT_BSTR && v.bstrVal != nullptr) i.valor = aU(std::wstring(v.bstrVal, SysStringLen(v.bstrVal)));
    VariantClear(&v);
    if (i.nombre.size() > 160) i.nombre = i.nombre.substr(0, 157) + "…";
    if (i.valor.size() > 300) i.valor = i.valor.substr(0, 297) + "…";
    return i;
}

/// La última lectura: el número de cada elemento es su posición + 1.
std::mutex gMu;
std::vector<Com<IUIAutomationElement>> gLeidos;

/// La ventana de los argumentos (`ventana` o `titulo`), o la del foco.
HWND ventanaDe(const json& a) {
    json b = json::object();
    if (a.contains("ventana")) b["id"] = a["ventana"];
    if (a.contains("titulo")) b["titulo"] = a["titulo"];
    if (!b.empty()) return buscarVentana(b);
    return GetForegroundWindow();
}

/// Los elementos (en la vista de controles, no fuera de pantalla) de la ventana, con
/// nombre, rol, posición, habilitado y valor ya leídos (una sola ida y vuelta).
bool elementos(HWND h, std::vector<Com<IUIAutomationElement>>& out, std::string& error) {
    IUIAutomation* u = uia();
    if (u == nullptr) {
        error = ba::i18n::traducir("mcp", "UI Automation no está disponible");
        return false;
    }
    Com<IUIAutomationElement> raiz;
    if (FAILED(u->ElementFromHandle(h, &raiz)) || !raiz) {
        error = ba::i18n::traducir("mcp", "no pude leer esa ventana");
        return false;
    }
    Com<IUIAutomationCacheRequest> cache;
    u->CreateCacheRequest(&cache);
    for (PROPERTYID pid : {UIA_NamePropertyId, UIA_ControlTypePropertyId, UIA_BoundingRectanglePropertyId, UIA_IsEnabledPropertyId,
                           UIA_AutomationIdPropertyId, UIA_ValueValuePropertyId})
        cache->AddProperty(pid);
    Com<IUIAutomationCondition> vista, visible, ambas;
    u->get_ControlViewCondition(&vista);
    VARIANT f;
    VariantInit(&f);
    f.vt = VT_BOOL;
    f.boolVal = VARIANT_FALSE;
    u->CreatePropertyCondition(UIA_IsOffscreenPropertyId, f, &visible);
    u->CreateAndCondition(vista.p, visible.p, &ambas);
    Com<IUIAutomationElementArray> arr;
    if (FAILED(raiz->FindAllBuildCache(TreeScope_Descendants, ambas.p, cache.p, &arr)) || !arr) {
        error = ba::i18n::traducir("mcp", "no pude recorrer los elementos de esa ventana");
        return false;
    }
    int n = 0;
    arr->get_Length(&n);
    out.clear();
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        Com<IUIAutomationElement> e;
        if (SUCCEEDED(arr->GetElement(i, &e)) && e) out.push_back(e);
    }
    return true;
}

// --- El recorrido de una ventana ---------------------------------------------------------
//
// Como lo leería un lector de pantalla, elemento por elemento (no un FindAll de todo):
//  - de cada elemento se piden de una vez sus propiedades y las de sus hijos (un caché
//    con alcance elemento + hijos), así cada nivel es una sola ida y vuelta;
//  - VISIBLE: tiene área, no está fuera de pantalla (salvo un campo, o un ítem de lista
//    en un navegador) y es un control;
//  - TOCABLE: su tipo es de los que se tocan Y su rol de accesibilidad (MSAA) también
//    (descarta los «botones» de adorno); una imagen solo si toma el foco; en un
//    navegador, un grupo que se despliega o tiene acción de clic;
//  - el recuadro se recorta a la ventana y a la pantalla: el centro es el de lo que se ve;
//  - en un navegador, la PÁGINA (RootWebArea) se recorre en orden de lectura y además
//    se junta su texto;
//  - un diálogo modal: solo cuenta lo del diálogo;
//  - las zonas que se desplazan, con su porcentaje;
//  - un tope de elementos que corta el recorrido (listas de miles de filas), y se corta
//    un elemento que repite el RuntimeId de un ancestro (proveedores con ciclos).

struct Leido {
    Com<IUIAutomationElement> el;
    json j; ///< sin "n" (se numera al final)
};

struct Recorrido {
    bool navegador = false;
    bool conTexto = false;   ///< juntar los textos (de la página, o de todo con `todos`)
    bool todos = false;      ///< también lo que no se toca (textos, imágenes con nombre)
    RECT ventana{}, pantalla{}, pagina{};
    bool hayPagina = false;
    int limite = 500, usados = 0;
    bool cortado = false;
    std::string titulo;
    std::vector<Leido> elementos;
    std::vector<std::string> textos;
    json desplazables = json::array();
    Com<IUIAutomationCacheRequest> cache;
};

VARIANT valorCache(IUIAutomationElement* e, PROPERTYID id) {
    VARIANT v;
    VariantInit(&v);
    if (FAILED(e->GetCachedPropertyValue(id, &v))) VariantInit(&v);
    return v;
}
std::string textoCache(IUIAutomationElement* e, PROPERTYID id) {
    VARIANT v = valorCache(e, id);
    std::string s = v.vt == VT_BSTR && v.bstrVal ? aU(std::wstring(v.bstrVal, SysStringLen(v.bstrVal))) : std::string();
    VariantClear(&v);
    return s;
}
bool boolCache(IUIAutomationElement* e, PROPERTYID id, bool def = false) {
    VARIANT v = valorCache(e, id);
    const bool b = v.vt == VT_BOOL ? v.boolVal != VARIANT_FALSE : def;
    VariantClear(&v);
    return b;
}
int intCache(IUIAutomationElement* e, PROPERTYID id, int def = -1) {
    VARIANT v = valorCache(e, id);
    const int i = v.vt == VT_I4 ? v.lVal : def;
    VariantClear(&v);
    return i;
}
double dobleCache(IUIAutomationElement* e, PROPERTYID id, double def = -1) {
    VARIANT v = valorCache(e, id);
    const double d = v.vt == VT_R8 ? v.dblVal : v.vt == VT_I4 ? v.lVal : def;
    VariantClear(&v);
    return d;
}
std::vector<int> runtimeId(IUIAutomationElement* e) {
    std::vector<int> out;
    VARIANT v = valorCache(e, UIA_RuntimeIdPropertyId);
    if ((v.vt & VT_ARRAY) && v.parray) {
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(v.parray, 1, &lo);
        SafeArrayGetUBound(v.parray, 1, &hi);
        for (LONG i = lo; i <= hi; ++i) {
            int x = 0;
            SafeArrayGetElement(v.parray, &i, &x);
            out.push_back(x);
        }
    }
    VariantClear(&v);
    return out;
}

RECT recortar(RECT r, const RECT& a, const RECT& b) {
    r.left = std::max({r.left, a.left, b.left});
    r.top = std::max({r.top, a.top, b.top});
    r.right = std::min({r.right, a.right, b.right});
    r.bottom = std::min({r.bottom, a.bottom, b.bottom});
    if (r.right <= r.left || r.bottom <= r.top) return RECT{0, 0, 0, 0};
    return r;
}

/// Los roles de accesibilidad (MSAA) que se tocan.
bool rolTocable(int r) {
    switch (r) {
        case 0x2B: /* PUSHBUTTON */ case 0x3E: /* SPLITBUTTON */ case 0x38: /* BUTTONDROPDOWN */
        case 0x39: /* BUTTONMENU */ case 0x3A: /* BUTTONDROPDOWNGRID */ case 0x40: /* OUTLINEBUTTON */
        case 0x1E: /* LINK */ case 0x2A: /* TEXT (campo) */ case 0x3F: /* IPADDRESS */ case 0x32: /* HOTKEYFIELD */
        case 0x2E: /* COMBOBOX */ case 0x2F: /* DROPLIST */ case 0x2C: /* CHECKBUTTON */ case 0x2D: /* RADIOBUTTON */
        case 0x0C: /* MENUITEM */ case 0x22: /* LISTITEM */ case 0x25: /* PAGETAB */ case 0x24: /* OUTLINEITEM */
        case 0x33: /* SLIDER */ case 0x34: /* SPINBUTTON */ case 0x31: /* DIAL */ case 0x03: /* SCROLLBAR */
        case 0x04: /* GRIP */ case 0x19: /* COLUMNHEADER */ case 0x1A: /* ROWHEADER */ case 0x1D: /* CELL */ return true;
        default: return false;
    }
}

bool tipoTocable(CONTROLTYPEID t) { return tocable(t) || t == UIA_ScrollBarControlTypeId; }
bool tipoInformativo(CONTROLTYPEID t) {
    return t == UIA_TextControlTypeId || t == UIA_ImageControlTypeId || t == UIA_StatusBarControlTypeId;
}

void anotar(Recorrido& r, IUIAutomationElement* e, CONTROLTYPEID tipo, const RECT& visto, bool enPagina) {
    const std::string nombre = [&] {
        std::string n = textoCache(e, UIA_NamePropertyId);
        while (!n.empty() && isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
        size_t i = 0;
        while (i < n.size() && isspace(static_cast<unsigned char>(n[i]))) ++i;
        n = n.substr(i);
        return n.size() > 160 ? n.substr(0, 157) + "…" : n;
    }();
    json j{{"rol", rolDe(tipo)}, {"nombre", nombre}};
    if (boolCache(e, UIA_HasKeyboardFocusPropertyId)) j["foco"] = true;
    if (const std::string k = textoCache(e, UIA_AcceleratorKeyPropertyId); !k.empty()) j["atajo"] = k;
    if (const std::string h = textoCache(e, UIA_HelpTextPropertyId); !h.empty() && h != nombre) j["ayuda"] = h.substr(0, 200);
    const bool clave = boolCache(e, UIA_IsPasswordPropertyId);
    if (clave) j["clave"] = true;
    if (tipo == UIA_EditControlTypeId || tipo == UIA_DocumentControlTypeId || tipo == UIA_ComboBoxControlTypeId ||
        tipo == UIA_ImageControlTypeId) {
        std::string v = textoCache(e, UIA_LegacyIAccessibleValuePropertyId);
        if (v.empty()) v = textoCache(e, UIA_ValueValuePropertyId);
        if (!clave && tipo != UIA_DocumentControlTypeId) j["valor"] = v.empty() ? ba::i18n::traducir("mcp", "(vacío)") : (v.size() > 300 ? v.substr(0, 297) + "…" : v);
    }
    if (tipo == UIA_ButtonControlTypeId || tipo == UIA_CheckBoxControlTypeId) {
        const int t = intCache(e, UIA_ToggleToggleStatePropertyId);
        if (t == ToggleState_On) j["marcado"] = true;
        else if (t == ToggleState_Off && tipo == UIA_CheckBoxControlTypeId) j["marcado"] = false;
    }
    const int ec = intCache(e, UIA_ExpandCollapseExpandCollapseStatePropertyId);
    if (ec == ExpandCollapseState_Expanded) j["desplegado"] = true;
    else if (ec == ExpandCollapseState_Collapsed && (tipo == UIA_ComboBoxControlTypeId || tipo == UIA_MenuItemControlTypeId || tipo == UIA_TreeItemControlTypeId))
        j["desplegado"] = false;
    if (boolCache(e, UIA_SelectionItemIsSelectedPropertyId)) j["elegido"] = true;
    if (tipo == UIA_SliderControlTypeId || tipo == UIA_SpinnerControlTypeId) {
        const double v = dobleCache(e, UIA_RangeValueValuePropertyId, std::nan("")), mn = dobleCache(e, UIA_RangeValueMinimumPropertyId, std::nan("")),
                     mx = dobleCache(e, UIA_RangeValueMaximumPropertyId, std::nan(""));
        if (!std::isnan(v)) j["valor"] = std::round(v * 100) / 100;
        if (!std::isnan(mn)) j["min"] = std::round(mn * 100) / 100;
        if (!std::isnan(mx)) j["max"] = std::round(mx * 100) / 100;
    }
    if (enPagina) j["pagina"] = true;
    // El centro de lo que se VE (recortado a la ventana y la pantalla): donde hacer clic.
    j["x"] = (visto.left + visto.right) / 2;
    j["y"] = (visto.top + visto.bottom) / 2;
    j["ancho"] = visto.right - visto.left;
    j["alto"] = visto.bottom - visto.top;
    ++r.usados;
    r.elementos.push_back({Com<IUIAutomationElement>([&] { e->AddRef(); return e; }()), std::move(j)});
}

void recorrer(Recorrido& r, IUIAutomationElement* sinCache, bool enPagina, std::vector<std::vector<int>>& ancestros, int prof) {
    if (r.usados >= r.limite) {
        r.cortado = true;
        return;
    }
    if (prof > 60) return;
    Com<IUIAutomationElement> e;
    if (FAILED(sinCache->BuildUpdatedCache(r.cache.p, &e)) || !e) return; // un elemento que ya no está
    // Ciclos: un hijo con el RuntimeId de un ancestro.
    const std::vector<int> rid = runtimeId(e.p);
    if (!rid.empty()) {
        if (std::find(ancestros.begin(), ancestros.end(), rid) != ancestros.end()) return;
        ancestros.push_back(rid);
    }
    CONTROLTYPEID tipo = 0;
    e->get_CachedControlType(&tipo);
    const bool fuera = boolCache(e.p, UIA_IsOffscreenPropertyId);
    RECT caja{};
    e->get_CachedBoundingRectangle(&caja);
    const long area = std::max(0L, caja.right - caja.left) * std::max(0L, caja.bottom - caja.top);
    const RECT& marco = enPagina && r.hayPagina ? r.pagina : r.ventana;

    // Lo que se desplaza.
    if (!tipoTocable(tipo) && !tipoInformativo(tipo) && !fuera && boolCache(e.p, UIA_ScrollVerticallyScrollablePropertyId)) {
        const RECT v = recortar(caja, marco, r.pantalla);
        if (v.right > v.left) {
            std::string n = textoCache(e.p, UIA_NamePropertyId);
            if (n.empty()) n = rolDe(tipo);
            r.desplazables.push_back({{"nombre", n.substr(0, 80)}, {"porcentaje", std::round(dobleCache(e.p, UIA_ScrollVerticalScrollPercentPropertyId, 0) * 10) / 10},
                                      {"x", (v.left + v.right) / 2}, {"y", (v.top + v.bottom) / 2}});
        }
    }

    const bool control = boolCache(e.p, UIA_IsControlElementPropertyId, true);
    const bool visible = area > 0 && control &&
                         (!fuera || tipo == UIA_EditControlTypeId || (tipo == UIA_ListItemControlTypeId && r.navegador));
    if (visible && boolCache(e.p, UIA_IsEnabledPropertyId, true)) {
        const bool focusable = tipo == UIA_EditControlTypeId || tipo == UIA_ButtonControlTypeId || tipo == UIA_CheckBoxControlTypeId ||
                               tipo == UIA_RadioButtonControlTypeId || tipo == UIA_TabItemControlTypeId || tipo == UIA_ListItemControlTypeId ||
                               boolCache(e.p, UIA_IsKeyboardFocusablePropertyId);
        bool tocar = false;
        const int rol = intCache(e.p, UIA_LegacyIAccessibleRolePropertyId);
        if (r.navegador && tipo == UIA_DataItemControlTypeId && !focusable) {
            tocar = false;
        } else if (!r.navegador && tipo == UIA_ImageControlTypeId && focusable) {
            tocar = true;
        } else if (tipoTocable(tipo)) {
            // El rol de accesibilidad también tiene que ser de los que se tocan.
            tocar = rolTocable(rol) || rol < 0;
        } else if (tipo == UIA_GroupControlTypeId && r.navegador) {
            const int ec = intCache(e.p, UIA_ExpandCollapseExpandCollapseStatePropertyId);
            std::string accion = textoCache(e.p, UIA_LegacyIAccessibleDefaultActionPropertyId);
            for (auto& c : accion) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            const bool deClic = accion == "click" || accion == "press" || accion == "jump" || accion == "check" || accion == "uncheck" ||
                                accion == "double click";
            tocar = (ec >= 0 && ec <= 3) || (rolTocable(rol) && (deClic || focusable));
        }
        if (tipo == UIA_DocumentControlTypeId) tocar = true;
        const RECT visto = recortar(caja, marco, r.pantalla);
        if (visto.right > visto.left) {
            const std::string nombre = textoCache(e.p, UIA_NamePropertyId);
            const bool conNombre = nombre.find_first_not_of(" \t\r\n") != std::string::npos;
            if (tocar && (conNombre || tipo == UIA_EditControlTypeId || tipo == UIA_ComboBoxControlTypeId || tipo == UIA_DocumentControlTypeId))
                anotar(r, e.p, tipo, visto, enPagina);
            else if (r.todos && conNombre && tipoInformativo(tipo))
                anotar(r, e.p, tipo, visto, enPagina);
            // El texto de la página (o de todo).
            if (r.conTexto && (enPagina || r.todos) && tipo == UIA_TextControlTypeId && conNombre) r.textos.push_back(nombre);
        }
    }

    // Los hijos (ya con sus propiedades en el caché).
    Com<IUIAutomationElementArray> hijos;
    if (SUCCEEDED(e->GetCachedChildren(&hijos)) && hijos) {
        int n = 0;
        hijos->get_Length(&n);
        for (int i = 0; i < n && r.usados < r.limite; ++i) {
            Com<IUIAutomationElement> h;
            if (FAILED(hijos->GetElement(i, &h)) || !h) continue;
            // La página de un navegador: su recuadro y se entra en orden de lectura.
            if (r.navegador && textoCache(h.p, UIA_AutomationIdPropertyId) == "RootWebArea") {
                h->get_CachedBoundingRectangle(&r.pagina);
                r.hayPagina = true;
                recorrer(r, h.p, true, ancestros, prof + 1);
                continue;
            }
            CONTROLTYPEID th = 0;
            h->get_CachedControlType(&th);
            // Un diálogo modal: lo de antes no se puede tocar mientras esté.
            if (th == UIA_WindowControlTypeId && !enPagina && boolCache(h.p, UIA_WindowIsModalPropertyId) &&
                !boolCache(h.p, UIA_IsOffscreenPropertyId)) {
                r.elementos.clear();
                r.usados = 0;
            }
            recorrer(r, h.p, enPagina, ancestros, prof + 1);
        }
        if (r.usados >= r.limite && n > 0) r.cortado = true;
    }
    if (!rid.empty()) ancestros.pop_back();
}

bool esNavegador(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    std::string app = ejecutableDe(pid);
    for (auto& c : app) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return app == "chrome.exe" || app == "msedge.exe" || app == "brave.exe" || app == "firefox.exe" || app == "opera.exe" ||
           app == "vivaldi.exe";
}

/// Recorre la ventana `h` y deja en `r` lo que se ve.
bool leerVentana(HWND h, Recorrido& r, std::string& error) {
    IUIAutomation* u = uia();
    Com<IUIAutomationElement> raiz;
    if (u == nullptr || FAILED(u->ElementFromHandle(h, &raiz)) || !raiz) {
        error = ba::i18n::traducir("mcp", "no pude leer esa ventana");
        return false;
    }
    u->CreateCacheRequest(&r.cache);
    r.cache->put_TreeScope(static_cast<TreeScope>(TreeScope_Element | TreeScope_Children));
    for (PROPERTYID pid :
         {UIA_NamePropertyId, UIA_AutomationIdPropertyId, UIA_ControlTypePropertyId, UIA_AcceleratorKeyPropertyId, UIA_RuntimeIdPropertyId,
          UIA_IsEnabledPropertyId, UIA_IsOffscreenPropertyId, UIA_IsControlElementPropertyId, UIA_HasKeyboardFocusPropertyId,
          UIA_IsKeyboardFocusablePropertyId, UIA_IsPasswordPropertyId, UIA_BoundingRectanglePropertyId, UIA_HelpTextPropertyId,
          UIA_LegacyIAccessibleRolePropertyId, UIA_LegacyIAccessibleValuePropertyId, UIA_LegacyIAccessibleDefaultActionPropertyId,
          UIA_ValueValuePropertyId, UIA_ScrollVerticallyScrollablePropertyId, UIA_ScrollVerticalScrollPercentPropertyId,
          UIA_ExpandCollapseExpandCollapseStatePropertyId, UIA_SelectionItemIsSelectedPropertyId, UIA_WindowIsModalPropertyId,
          UIA_ToggleToggleStatePropertyId, UIA_RangeValueValuePropertyId, UIA_RangeValueMinimumPropertyId, UIA_RangeValueMaximumPropertyId})
        r.cache->AddProperty(pid);
    raiz->get_CurrentBoundingRectangle(&r.ventana);
    r.pantalla = RECT{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                      GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                      GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    r.navegador = esNavegador(h);
    r.titulo = tituloDe(h);
    std::vector<std::vector<int>> ancestros;
    recorrer(r, raiz.p, false, ancestros, 0);
    return true;
}

json aJson(int n, const Info& i) {
    json j{{"n", n}, {"rol", i.rol}, {"nombre", i.nombre}};
    if (!i.valor.empty()) j["valor"] = i.valor;
    if (!i.idAuto.empty() && i.idAuto.size() < 60) j["id_auto"] = i.idAuto;
    if (!i.habilitado) j["habilitado"] = false;
    // El centro, en píxeles del escritorio (para system_mouse si hiciera falta).
    j["x"] = (i.r.left + i.r.right) / 2;
    j["y"] = (i.r.top + i.r.bottom) / 2;
    j["ancho"] = i.r.right - i.r.left;
    j["alto"] = i.r.bottom - i.r.top;
    return j;
}

/// El elemento de los argumentos: `n` de la última lectura, o el primero cuyo nombre
/// contiene `nombre` (y es del `rol`, si se dio) en la ventana; preferidos los tocables.
bool elementoDe(const json& a, Com<IUIAutomationElement>& el, Info& info, std::string& error) {
    if (a.contains("n")) {
        std::lock_guard<std::mutex> l(gMu);
        const int n = a.value("n", 0);
        if (n < 1 || n > static_cast<int>(gLeidos.size())) {
            error = gLeidos.empty() ? ba::i18n::traducir("mcp", "no hay una lectura: system_ui_leer primero")
                                    : ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay un elemento %1 en la última lectura"),
                                                       {std::to_string(n)});
            return false;
        }
        el = gLeidos[static_cast<size_t>(n - 1)];
        info = infoDe(el.p, false);
        return true;
    }
    const std::string buscado = minus(a.value("nombre", std::string()));
    if (buscado.empty()) {
        error = ba::i18n::traducir("mcp", "falta `n` o `nombre`");
        return false;
    }
    const std::string rol = minus(a.value("rol", std::string()));
    HWND h = ventanaDe(a);
    if (h == nullptr) {
        error = ba::i18n::traducir("mcp", "no encontré esa ventana");
        return false;
    }
    Recorrido r;
    r.todos = true; // también un texto con ese nombre (un mensaje que aparece)
    r.limite = 2000;
    if (!leerVentana(h, r, error)) return false;
    int mejor = -1, puntos = -1;
    for (size_t k = 0; k < r.elementos.size(); ++k) {
        const json& j = r.elementos[k].j;
        const std::string nm = minus(j.value("nombre", std::string()));
        const std::string rl = j.value("rol", std::string());
        if (nm.find(buscado) == std::string::npos) continue;
        if (!rol.empty() && rl != rol) continue;
        // Nombre exacto > se toca > lo demás.
        const int p = (nm == buscado ? 4 : 0) + (rl != "texto" && rl != "imagen" ? 2 : 0);
        if (p > puntos) {
            puntos = p;
            mejor = static_cast<int>(k);
        }
    }
    if (mejor < 0) {
        error = rol.empty()
                    ? ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay ningún elemento con «%1» en «%2» (system_ui_leer "
                                                                 "muestra lo que hay)"),
                                       {a.value("nombre", std::string()), tituloDe(h)})
                    : ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay ningún elemento con «%1» (%3) en «%2» "
                                                                 "(system_ui_leer muestra lo que hay)"),
                                       {a.value("nombre", std::string()), tituloDe(h), rol});
        return false;
    }
    el = r.elementos[static_cast<size_t>(mejor)].el;
    info = infoDe(el.p, false);
    return true;
}

/// Un patrón del elemento, si lo tiene.
template <class P>
Com<P> patron(IUIAutomationElement* e, PATTERNID id) {
    Com<P> p;
    e->GetCurrentPatternAs(id, __uuidof(P), reinterpret_cast<void**>(&p));
    return p;
}

/// Hace `f` en otro hilo y espera hasta `ms`: invocar un botón que abre un diálogo
/// modal puede no volver hasta que se cierre.
bool conTiempo(std::function<void()> f, int ms) {
    auto hecho = std::make_shared<std::promise<void>>();
    std::future<void> fu = hecho->get_future();
    std::thread([f = std::move(f), hecho]() {
        ba::platform::nombrarHilo("control-uia");
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        f();
        hecho->set_value();
        CoUninitialize();
    }).detach();
    return fu.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready;
}

std::string describir(const Info& i) { return i.rol + " «" + i.nombre + "»"; }

json clic(const json& id, const json& a, const Permisos& p) {
    if (!p.mouse) return sinPermiso(id, "mouse");
    Com<IUIAutomationElement> el;
    Info info;
    std::string error;
    if (!elementoDe(a, el, info, error)) return resultadoTexto(id, error, true);
    if (!info.habilitado) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 está deshabilitado"), {describir(info)}), true);
    const bool conMouse = a.value("con_mouse", false) || a.value("doble", false) || a.value("boton", std::string("izq")) == "der";
    std::string como;
    if (!conMouse) {
        // La acción del elemento: anda tapado, en otro monitor, sin mover el mouse.
        if (auto inv = patron<IUIAutomationInvokePattern>(el.p, UIA_InvokePatternId)) {
            const bool volvio = conTiempo([inv]() { inv->Invoke(); }, 3000);
            como = volvio ? ba::i18n::traducir("mcp", "invocado")
                           : ba::i18n::traducir("mcp", "invocado (sigue: seguramente abrió un diálogo)");
        } else if (auto tg = patron<IUIAutomationTogglePattern>(el.p, UIA_TogglePatternId)) {
            tg->Toggle();
            ToggleState st = ToggleState_Off;
            tg->get_CurrentToggleState(&st);
            como = st == ToggleState_On    ? ba::i18n::traducir("mcp", "marcado")
                   : st == ToggleState_Off ? ba::i18n::traducir("mcp", "desmarcado")
                                           : ba::i18n::traducir("mcp", "a medias");
        } else if (auto sel = patron<IUIAutomationSelectionItemPattern>(el.p, UIA_SelectionItemPatternId)) {
            sel->Select();
            como = ba::i18n::traducir("mcp", "elegido");
        } else if (auto ec = patron<IUIAutomationExpandCollapsePattern>(el.p, UIA_ExpandCollapsePatternId)) {
            ExpandCollapseState st = ExpandCollapseState_Collapsed;
            ec->get_CurrentExpandCollapseState(&st);
            if (st == ExpandCollapseState_Expanded) ec->Collapse(), como = ba::i18n::traducir("mcp", "plegado");
            else ec->Expand(), como = ba::i18n::traducir("mcp", "desplegado");
        } else if (auto acc = patron<IUIAutomationLegacyIAccessiblePattern>(el.p, UIA_LegacyIAccessiblePatternId)) {
            if (SUCCEEDED(acc->DoDefaultAction())) como = ba::i18n::traducir("mcp", "acción por defecto");
        }
    }
    if (como.empty()) {
        // Sin acción propia (o pedido): un clic de mouse en el centro.
        const int x = (info.r.left + info.r.right) / 2, y = (info.r.top + info.r.bottom) / 2;
        if (info.r.right <= info.r.left) return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "%1 no tiene lugar en la pantalla"), {describir(info)}), true);
        HWND raiz = WindowFromPoint(POINT{x, y});
        if (raiz != nullptr) traerAlFrente(GetAncestor(raiz, GA_ROOT));
        mouseAbsoluto(x, y);
        const std::string b = a.value("boton", std::string("izq"));
        for (int k = 0; k < (a.value("doble", false) ? 2 : 1); ++k) {
            mouseBoton(b, true);
            mouseBoton(b, false);
        }
        como = ba::i18n::llenar(ba::i18n::traducir("mcp", "clic de mouse en %1,%2"), {std::to_string(x), std::to_string(y)});
    }
    return resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho: %1 — %2."), {describir(info), como}));
}

json escribir(const json& id, const json& a, const Permisos& p) {
    if (!p.teclado) return sinPermiso(id, "teclado");
    Com<IUIAutomationElement> el;
    Info info;
    std::string error;
    if (!elementoDe(a, el, info, error)) return resultadoTexto(id, error, true);
    const std::string texto = a.value("texto", std::string());
    const bool reemplazar = a.value("reemplazar", true);
    std::string como;
    if (auto v = patron<IUIAutomationValuePattern>(el.p, UIA_ValuePatternId)) {
        BOOL soloLectura = FALSE;
        v->get_CurrentIsReadOnly(&soloLectura);
        if (!soloLectura) {
            std::wstring w = aW(texto);
            if (!reemplazar) w = aW(info.valor) + w;
            BSTR b = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
            const HRESULT hr = v->SetValue(b);
            SysFreeString(b);
            if (SUCCEEDED(hr)) como = ba::i18n::traducir("mcp", "valor puesto");
        }
    }
    if (como.empty()) {
        // Sin valor directo: el foco y a escribir.
        el->SetFocus();
        Sleep(60);
        if (reemplazar) mandarTeclas("ctrl+a");
        escribirTexto(aW(texto));
        como = ba::i18n::traducir("mcp", "escrito con el teclado");
    }
    if (a.value("enter", false)) {
        el->SetFocus();
        Sleep(40);
        mandarTeclas("enter");
        como += " + Enter";
    }
    const Info despues = infoDe(el.p, false);
    std::string res = ba::i18n::llenar(ba::i18n::traducir("mcp", "Hecho: %1 — %2."), {describir(info), como});
    if (!despues.valor.empty()) res += ba::i18n::llenar(ba::i18n::traducir("mcp", " Ahora dice: «%1»."), {despues.valor});
    return resultadoTexto(id, res);
}

json texto(const json& id, const json& a) {
    HWND h = ventanaDe(a);
    if (h == nullptr) return resultadoTexto(id, ba::i18n::traducir("mcp", "no encontré esa ventana"), true);
    const size_t max = static_cast<size_t>(std::clamp(a.value("max", 20000), 100, 200000));
    std::string out;
    std::vector<Com<IUIAutomationElement>> es;
    std::string error;
    if (!elementos(h, es, error)) return resultadoTexto(id, error, true);
    // Un documento con TextPattern (una página web, un editor): su texto entero.
    for (auto& e : es) {
        CONTROLTYPEID t = 0;
        e->get_CachedControlType(&t);
        if (t != UIA_DocumentControlTypeId) continue;
        if (auto tp = patron<IUIAutomationTextPattern>(e.p, UIA_TextPatternId)) {
            Com<IUIAutomationTextRange> rango;
            if (SUCCEEDED(tp->get_DocumentRange(&rango)) && rango) {
                BSTR b = nullptr;
                if (SUCCEEDED(rango->GetText(static_cast<int>(max), &b))) out = bstr(b);
            }
        }
        if (!out.empty()) break;
    }
    // Si no: los textos y valores de los elementos, en orden.
    if (out.empty())
        for (auto& e : es) {
            const Info i = infoDe(e.p, true);
            const std::string& t = i.tipo == UIA_EditControlTypeId && !i.valor.empty() ? i.valor : i.nombre;
            if (t.empty()) continue;
            if (!out.empty()) out += '\n';
            out += t;
            if (out.size() >= max) break;
        }
    if (out.size() > max) out = out.substr(0, max) + "…";
    return resultadoDatos(id, {{"ventana", tituloDe(h)}, {"texto", out}, {"caracteres", out.size()}});
}

} // namespace

bool atenderUia(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out) {
    if (nombre.rfind("system_ui_", 0) != 0) return false;
    if (uia() == nullptr) {
        out = resultadoTexto(id, ba::i18n::traducir("mcp", "UI Automation no está disponible en esta máquina"), true);
        return true;
    }
    if (nombre == "system_ui_leer") {
        if (!p.pantalla) return out = sinPermiso(id, "pantalla"), true;
        // Las ventanas: la pedida, la del foco, o `todas` las visibles (la del foco primero).
        std::vector<HWND> hs;
        if (a.value("todas", false)) {
            HWND f = GetForegroundWindow();
            if (f) hs.push_back(f);
            for (const auto& w : ventanas())
                if (w.hwnd != f && !IsIconic(w.hwnd)) hs.push_back(w.hwnd);
        } else if (HWND h = ventanaDe(a)) {
            hs.push_back(h);
        }
        if (hs.empty()) return out = resultadoTexto(id, ba::i18n::traducir("mcp", "no encontré esa ventana"), true), true;
        const std::string filtro = minus(a.value("filtro", std::string()));
        int limite = std::clamp(a.value("max", 500), 1, 3000);
        std::vector<Com<IUIAutomationElement>> quedan;
        json vs = json::array();
        bool cortado = false;
        for (HWND h : hs) {
            if (limite <= 0) {
                cortado = true;
                break;
            }
            Recorrido r;
            r.todos = a.value("todos", false);
            r.conTexto = a.value("texto", true);
            r.limite = limite;
            std::string error;
            if (!leerVentana(h, r, error)) continue;
            json lista = json::array();
            for (auto& x : r.elementos) {
                if (!filtro.empty() && minus(x.j.value("nombre", std::string())).find(filtro) == std::string::npos &&
                    minus(x.j.value("valor", std::string())).find(filtro) == std::string::npos)
                    continue;
                quedan.push_back(x.el);
                json j = x.j;
                j["n"] = static_cast<int>(quedan.size());
                lista.push_back(j);
            }
            limite -= r.usados;
            cortado = cortado || r.cortado;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            json v{{"ventana", r.titulo}, {"id", reinterpret_cast<long long>(h)}, {"app", ejecutableDe(pid)}, {"elementos", lista}};
            if (r.navegador) v["navegador"] = true;
            if (!r.desplazables.empty()) v["desplazables"] = r.desplazables;
            if (!r.textos.empty()) {
                std::string t;
                for (const auto& x : r.textos) {
                    if (t.size() > 12000) {
                        t += "…";
                        break;
                    }
                    t += x + "\n";
                }
                v["texto"] = t;
            }
            if (lista.empty()) v["nota"] = ba::i18n::traducir("mcp", "nada para tocar (una app que no habla UI Automation: system_pantalla)");
            vs.push_back(v);
        }
        {
            std::lock_guard<std::mutex> l(gMu);
            gLeidos = std::move(quedan);
        }
        json d = vs.size() == 1 ? vs[0] : json{{"ventanas", vs}};
        if (cortado) d["cortado"] = ba::i18n::traducir("mcp", "se llegó al tope de elementos: `max`, `filtro` o una ventana sola");
        out = resultadoDatos(id, d);
        return true;
    }
    if (nombre == "system_ui_clic") return out = clic(id, a, p), true;
    if (nombre == "system_ui_escribir") return out = escribir(id, a, p), true;
    if (nombre == "system_ui_texto") {
        if (!p.pantalla) return out = sinPermiso(id, "pantalla"), true;
        return out = texto(id, a), true;
    }
    if (nombre == "system_ui_esperar") {
        if (!p.pantalla) return out = sinPermiso(id, "pantalla"), true;
        const int ms = std::clamp(a.value("ms", 10000), 0, 30000);
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        do {
            Com<IUIAutomationElement> el;
            Info info;
            std::string error;
            json b = a;
            b.erase("n");
            if (elementoDe(b, el, info, error)) {
                out = resultadoDatos(id, {{"aparecio", true}, {"elemento", aJson(0, info)}});
                return true;
            }
            Sleep(300);
        } while (std::chrono::steady_clock::now() < fin);
        out = resultadoTexto(id, ba::i18n::llenar(ba::i18n::traducir("mcp", "No apareció «%1» en %2 ms."),
                                                {a.value("nombre", std::string()), std::to_string(ms)}), true);
        return true;
    }
    return false;
}

#elif !defined(__APPLE__) && !defined(__linux__) // macOS: control_mac.mm; Linux: control_linux.cpp

bool atenderUia(const json& id, const std::string& nombre, const json&, const Permisos&, json& out) {
    if (nombre.rfind("system_ui_", 0) != 0) return false;
    out = resultadoTexto(id, todaviaNo(nombre), true);
    return true;
}

#endif

} // namespace bactl
