// ============================================================================
// src/app/medios/Medios.hpp — los archivos del operador
//
// Musica, videos, imagenes, presentaciones: lo que se pone en una escena, lo
// que suena de fondo, lo que manda un orador. Viven en las carpetas del sistema
// portable —`Media/`, `Shares/`, `Recordings/`— y hasta ahora SOLO los veia el
// asistente: `medios` era una de las dos unicas categorias del MCP sin una sola
// linea de interfaz. Para poner un video de fondo habia que pedirselo al
// asistente o saberse la ruta de memoria.
//
// ESTO ESTA ACA Y NO ADENTRO DEL MCP porque ahora lo usan los dos: las
// herramientas y la pestaña. Estaba escrito una vez adentro de McpMediaTools;
// repetirlo para la interfaz es la forma de que dentro de tres meses la
// pestaña no vea un tipo de archivo que el asistente si.
//
// LOS DOCUMENTOS SON NUEVOS. `pdf` y `pptx` no estaban en ninguna lista, asi
// que `media_list` ni los veia: un orador mandaba su presentacion y para el
// aparato ese archivo no existia. Ahora son un tipo mas, porque son de donde
// sale un carrusel (ver docs/funciones/CARRUSEL.md).
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace ba::app::medios {

/// Que es cada archivo. Se decide por la extension y nada mas: abrir cada uno
/// para preguntarle costaria segundos en una carpeta con doscientos.
enum class Tipo { Desconocido, Imagen, Video, Audio, Documento };

const char* nombreDeTipo(Tipo t);
/// El tipo de un archivo por su nombre. `Desconocido` para lo que no sabemos
/// mostrar ni reproducir.
Tipo tipoDe(const QString& archivo);
/// true si de ese archivo se puede sacar un carrusel (hoy: pdf y pptx).
bool sePuedeDesarmar(const QString& archivo);

/// Las carpetas de medios del sistema, en el orden en que se muestran.
const QStringList& carpetas();

/// Un archivo encontrado.
struct Archivo {
    QString ruta;     ///< absoluta
    QString relativa; ///< como se la nombra (Media/logos/x.png)
    QString nombre;   ///< solo el nombre, para mostrar
    QString carpeta;  ///< Media, Shares, Recordings…
    Tipo tipo = Tipo::Desconocido;
    qint64 bytes = 0;
    qint64 modificadoMs = 0;
};

/// Sin tildes, en minusculas y con los separadores hechos espacios: buscar
/// «logo camion» encuentra «Logo_Camión.png». Es lo que hace que el filtro se
/// sienta como uno espera y no como una comparacion de texto.
QString aplanar(const QString& s);

/// Todo lo que hay debajo de `raiz`, recorriendo las carpetas de medios.
/// `soloCarpeta` vacio = todas. Ordenado por carpeta y nombre.
QVector<Archivo> listar(const QString& raiz, const QString& soloCarpeta = {});

/// true si `texto` tiene TODAS las palabras de `consulta`, aplanadas. Consulta
/// vacia coincide con todo (no es un error pedir todo). Esta aparte para que el
/// buscador de laminas de un carrusel filtre EXACTAMENTE igual que la grilla de
/// medios: dos criterios parecidos pero distintos se notan enseguida y no hay
/// forma de saber cual esta bien.
bool coincide(const QString& texto, const QString& consulta);

/// Los que coinciden con `consulta`, comparando aplanado contra el nombre y la
/// ruta. Consulta vacia = todos (no es un error pedir todo).
QVector<Archivo> filtrar(const QVector<Archivo>& todos, const QString& consulta);

/// Una ruta relativa o absoluta -> absoluta DENTRO de `raiz`, o vacio si se
/// sale. Es lo que evita que un `../../etc/passwd` escrito en cualquier lado
/// termine siendo una ruta valida.
QString resolverAdentro(const QString& raiz, const QString& p);

// --- El arbol de carpetas ---------------------------------------------------
//
// Con doscientos archivos en una grilla plana no se encuentra nada aunque haya
// buscador: buscar sirve cuando uno SABE que busca, y la mitad de las veces se
// esta mirando a ver que hay. Para eso esta el arbol.
//
// SE CAMINA EL DISCO, no se deducen las carpetas de las rutas de los archivos.
// Una carpeta vacia tiene que aparecer: si alguien la crea para ordenar y no la
// ve hasta poner algo adentro, no puede arrastrar nada hacia ella.

/// Una carpeta del arbol. Viene en una LISTA en orden de dibujo (cada una
/// despues de su padre) con su profundidad: un arbol de verdad en QML obliga a
/// un modelo anidado y no se gana nada, porque igual se dibuja una fila por
/// carpeta.
struct Carpeta {
    QString relativa;  ///< Media/logos/sponsors — la identidad
    QString nombre;    ///< sponsors
    int nivel = 0;     ///< 0 = Media, Shares, Recordings
    int propios = 0;   ///< archivos directamente adentro
    int total = 0;     ///< contando las subcarpetas
    bool raiz = false; ///< una de las carpetas del sistema (no se puede mover ni borrar)
};

/// El arbol entero. `todos` es la lista de archivos ya leida: las cuentas
/// salen de ahi en vez de volver a recorrer el disco por cada carpeta.
QVector<Carpeta> arbol(const QString& raiz, const QVector<Archivo>& todos);

/// Ordena la lista. `criterio`: nombre | fecha | tamano | tipo. Cualquier otra
/// cosa deja el orden como estaba, que es el de `listar` (carpeta y nombre).
void ordenar(QVector<Archivo>& lista, const QString& criterio, bool descendente = false);

/// Mueve un archivo o una carpeta adentro de `destino` (una carpeta relativa).
/// Si ya hay algo con ese nombre, numera en vez de pisar: traer dos veces el
/// mismo logo corregido es normal, perder el anterior no.
/// :returns: false y deja el motivo en `error`.
bool mover(const QString& raiz, const QString& queRel, const QString& destinoRel, QString* error);

/// Crea una carpeta adentro de otra.
bool crearCarpeta(const QString& raiz, const QString& padreRel, const QString& nombre, QString* error);

/// Le cambia el nombre a un archivo o carpeta, sin moverlo de lugar.
bool renombrar(const QString& raiz, const QString& queRel, const QString& nombre, QString* error);

/// A la papelera (`Papelera/` en la raiz del sistema), NO se borra. Un archivo
/// puede estar en una escena, o ser el unico que quedaba del evento pasado:
/// deshacer tiene que ser sacarlo de una carpeta, no volver a conseguirlo.
bool aPapelera(const QString& raiz, const QString& queRel, QString* error);

} // namespace ba::app::medios
