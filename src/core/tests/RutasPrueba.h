/* src/core/tests/RutasPrueba.h — dónde encuentra una prueba lo que no es de ella
 *
 * Una prueba que necesita archivos del árbol fuente (assets/web/guest, docs/mcp.json) u
 * otro programa del build (ba-mcp, ba-web) no puede quedarse con la ruta que fijó CMake:
 * esa es la del árbol donde se compiló (en Docker, /trabajo/ba/...), y la misma prueba se
 * corre en la Mac, en Docker y copiada a bin\64bit de la PC con Windows. Se busca así:
 *
 *   - del árbol fuente, `ba_prueba_fuente("docs/mcp.json")`: bajo $BA_FUENTES (la raíz
 *     de un árbol, o de una copia de lo que hace falta) y, si no está, bajo la raíz que
 *     fijó CMake (BA_PRUEBA_FUENTES);
 *   - un programa, `ba_prueba_programa("BA_MCP_EXE", "ba-mcp", <ruta de CMake>)`: el de
 *     la variable de entorno, el que está al lado de la prueba (copiadas juntas a otra
 *     máquina) y el del build.
 *
 * Si no aparece, devuelve NULL y dice en la salida qué falta y qué variable ponerle: la
 * prueba lo cuenta como FALLO (nada de seguir con una ruta que no existe y caerse más
 * adelante). En C (lo usa test_ba_web) y en C++.
 */
#ifndef BA_RUTAS_PRUEBA_H
#define BA_RUTAS_PRUEBA_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define BA_PRUEBA_EXE_SUFIJO ".exe"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#define BA_PRUEBA_EXE_SUFIJO ""
#else
#include <unistd.h>
#define BA_PRUEBA_EXE_SUFIJO ""
#endif

#ifndef BA_PRUEBA_FUENTES
#define BA_PRUEBA_FUENTES ""
#endif

static inline int ba_prueba_existe(const char* ruta) {
    struct stat st;
    return ruta != NULL && *ruta != '\0' && stat(ruta, &st) == 0;
}

/* La carpeta de este ejecutable ("" si no se sabe). */
static inline const char* ba_prueba_carpeta_exe(void) {
    static char dir[4096];
    size_t n;
    dir[0] = '\0';
#if defined(_WIN32)
    {
        char* p = NULL;
        if (_get_pgmptr(&p) == 0 && p != NULL) snprintf(dir, sizeof dir, "%s", p);
    }
#elif defined(__APPLE__)
    {
        uint32_t cap = (uint32_t)sizeof dir;
        if (_NSGetExecutablePath(dir, &cap) != 0) dir[0] = '\0';
    }
#else
    {
        ssize_t r = readlink("/proc/self/exe", dir, sizeof dir - 1);
        dir[r > 0 ? r : 0] = '\0';
    }
#endif
    for (n = strlen(dir); n > 0 && dir[n - 1] != '/' && dir[n - 1] != '\\'; --n) {
    }
    dir[n > 0 ? n - 1 : 0] = '\0';
    return dir;
}

static inline const char* ba_prueba_var(const char* nombre) {
    const char* v = getenv(nombre);
    return v != NULL && *v != '\0' ? v : NULL;
}

/* `relativo` (con /) dentro del árbol fuente; NULL (y lo dice) si no está. La ruta vuelve
   en un búfer propio que se reusa a las ocho llamadas: copiala si la guardás. */
static inline const char* ba_prueba_fuente(const char* relativo) {
    static char rutas[8][4096];
    static int proxima = 0;
    char* out = rutas[proxima++ % 8];
    const char* raiz = ba_prueba_var("BA_FUENTES");
    if (raiz != NULL) {
        snprintf(out, 4096, "%s/%s", raiz, relativo);
        if (ba_prueba_existe(out)) return out;
        printf("FALLO: falta %s en BA_FUENTES=%s (la raíz del árbol fuente, o una copia de lo que "
               "usa la prueba)\n", relativo, raiz);
        return NULL;
    }
    snprintf(out, 4096, "%s/%s", BA_PRUEBA_FUENTES, relativo);
    if (ba_prueba_existe(out)) return out;
    printf("FALLO: falta %s del árbol fuente (se compiló en %s): poné BA_FUENTES=<raíz del árbol, "
           "o una copia con %s>\n", relativo, BA_PRUEBA_FUENTES, relativo);
    return NULL;
}

/* El programa `nombre` (sin .exe): $var, al lado de la prueba o `compilado` (la ruta del
   build que fijó CMake); NULL (y lo dice) si no está en ninguno. Búfer propio (cuatro
   vueltas), como el de arriba. */
static inline const char* ba_prueba_programa(const char* var, const char* nombre, const char* compilado) {
    static char rutas[4][4096];
    static int proxima = 0;
    char* out = rutas[proxima++ % 4];
    const char* v = ba_prueba_var(var);
    if (v != NULL) {
        snprintf(out, 4096, "%s", v);
        if (ba_prueba_existe(out)) return out;
        printf("FALLO: %s=%s no existe\n", var, v);
        return NULL;
    }
    snprintf(out, 4096, "%s/%s%s", ba_prueba_carpeta_exe(), nombre, BA_PRUEBA_EXE_SUFIJO);
    if (ba_prueba_existe(out)) return out;
    snprintf(out, 4096, "%s", compilado);
    if (ba_prueba_existe(out)) return out;
    printf("FALLO: falta %s%s (ni al lado de la prueba, en %s, ni en el build: %s): poné %s=<ruta>\n", nombre,
           BA_PRUEBA_EXE_SUFIJO, ba_prueba_carpeta_exe(), compilado, var);
    return NULL;
}

#endif /* BA_RUTAS_PRUEBA_H */
