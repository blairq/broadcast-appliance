# tools/carga/etw-win.ps1 — qué corre en cada núcleo de la PC Windows (ETW con WPR, a pedido)
#
# Graba -Segundos con WPR (Windows Performance Recorder, viene con Windows: wpr.exe) y el
# perfil ba-nucleos.wprp (procesos e hilos, DPC, ISR y el muestreo de CPU cada 1 ms, sin
# pilas), lo pasa a CSV con tracerpt (también viene con Windows) y lo resume con un
# analizador en C# (Add-Type: nada que instalar). Escribe UNA línea JSON en la salida:
#   segundos, nucleos: [{nucleo, ocupado_pct, dpc: {n, ms, pct}, isr: {n, ms, pct, vectores},
#                        hilos: [{proceso, pid, nombre, pct}]}]   (los 8 que más corrieron ahí)
#   hilos: [{proceso, nombre, pct, nucleos: {núcleo: pct}}]      (los 40 que más corrieron, de los
#                        procesos de -Nombres: en qué núcleos corrió cada uno)
#   herramientas: qué se usó (y qué falta para más: ver abajo)
# El nombre de cada hilo sale del evento Thread/SetName y, para los que se nombraron antes de
# grabar, de GetThreadDescription al terminar (lo que pone ba::platform::nombrarHilo).
#
# Lo que NO da: los cambios de contexto por núcleo (tracerpt no decodifica CSwitch; por hilo
# salen de muestrear-win.ps1) ni los contadores del procesador (IPC, fallos de L3). Para eso
# hace falta el Windows Performance Toolkit (xperf/wpaexporter, del Windows ADK) o AMD uProf;
# no se instala nada solo (ver docs/hardware).
#
# Necesita una consola con permisos de administrador (WPR). Lo lanza medir-sala.py --etw 20:
#   ssh windows powershell -NoProfile -ExecutionPolicy Bypass -File C:\ba-pruebas\etw-win.ps1 -Segundos 20
param([int]$Segundos = 20, [string]$Dir = "C:\ba-pruebas\etw", [string[]]$Nombres = @("ba-app", "ba-gateway", "ba-web"),
      [switch]$Conservar)
$ErrorActionPreference = "Continue"
$Nombres = @($Nombres | ForEach-Object { $_ -split "," } | Where-Object { $_ })
[Console]::OutputEncoding = [Text.Encoding]::UTF8
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$perfil = Join-Path $PSScriptRoot "ba-nucleos.wprp"
$etl = Join-Path $Dir "nucleos.etl"
$csv = Join-Path $Dir "nucleos.csv"
Remove-Item -Force -ErrorAction SilentlyContinue $etl, $csv

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
public static class BaEtw {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint acceso, bool hereda, uint tid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr h, out IntPtr texto);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
    public static string NombreVivo(uint tid) {
        IntPtr h = OpenThread(0x0800, false, tid);
        if (h == IntPtr.Zero) return "";
        try {
            IntPtr p;
            if (GetThreadDescription(h, out p) < 0 || p == IntPtr.Zero) return "";
            string s = Marshal.PtrToStringUni(p); LocalFree(p); return s ?? "";
        } finally { CloseHandle(h); }
    }
    // Campos de una línea de tracerpt (CSV con comillas).
    static List<string> Partir(string l) {
        var r = new List<string>(); var sb = new StringBuilder(); bool q = false;
        foreach (char c in l) {
            if (c == '"') { q = !q; continue; }
            if (c == ',' && !q) { r.Add(sb.ToString().Trim()); sb.Clear(); continue; }
            sb.Append(c);
        }
        r.Add(sb.ToString().Trim());
        return r;
    }
    static long Num(string s) {
        s = s.Trim();
        if (s.StartsWith("0x")) return Convert.ToInt64(s.Substring(2), 16);
        long v; return long.TryParse(s, out v) ? v : -1;
    }
    static string Utf16Hex(string h) {
        if (!h.StartsWith("0x") || h.Length < 6) return "";
        var b = new List<byte>();
        for (int i = 2; i + 1 < h.Length; i += 2) b.Add(Convert.ToByte(h.Substring(i, 2), 16));
        return Encoding.Unicode.GetString(b.ToArray()).TrimEnd('\0');
    }
    public class Hilo { public long pid; public string nombre = ""; }
    public static Dictionary<long, Hilo> hilos = new Dictionary<long, Hilo>();
    public static Dictionary<long, string> procesos = new Dictionary<long, string>();
    // núcleo → hilo → muestras; núcleo → dpc/isr (n, 100 ns); vectores de ISR por núcleo.
    public static Dictionary<int, Dictionary<long, long>> muestras = new Dictionary<int, Dictionary<long, long>>();
    public static Dictionary<int, long[]> dpc = new Dictionary<int, long[]>(), isr = new Dictionary<int, long[]>();
    public static Dictionary<int, Dictionary<long, long>> vectores = new Dictionary<int, Dictionary<long, long>>();
    public static long t0 = long.MaxValue, t1 = 0;
    static T Dar<K, T>(Dictionary<K, T> d, K k) where T : new() { T v; if (!d.TryGetValue(k, out v)) { v = new T(); d[k] = v; } return v; }
    public static void Leer(string ruta) {
        int iCpu = 11, iT = 16, iDatos = 19;
        foreach (string l in File.ReadLines(ruta)) {
            if (l.Length < 40) continue;
            var f = Partir(l);
            if (f.Count <= iDatos) continue;
            string ev = f[0], tipo = f[1];
            int cpu; if (!int.TryParse(f[iCpu], out cpu)) continue;
            long t = Num(f[iT]);
            if (ev == "PerfInfo" && tipo == "SampleProf") {
                long tid = Num(f[iDatos + 1]);
                var m = Dar(muestras, cpu); long c; m.TryGetValue(tid, out c); m[tid] = c + 1;
                if (t > 0) { t0 = Math.Min(t0, t); t1 = Math.Max(t1, t); }
            } else if (ev == "PerfInfo" && (tipo == "DPC" || tipo == "ThreadDPC" || tipo == "TimerDPC")) {
                long ini = Num(f[iDatos]); long[] a; if (!dpc.TryGetValue(cpu, out a)) { a = new long[2]; dpc[cpu] = a; }
                if (a.Length == 0) { a = new long[2]; dpc[cpu] = a; }
                a[0]++; if (ini > 0 && t >= ini && t - ini < 10000000) a[1] += t - ini;
            } else if (ev == "PerfInfo" && tipo.StartsWith("ISR")) {
                long ini = Num(f[iDatos]); long[] a; if (!isr.TryGetValue(cpu, out a)) { a = new long[2]; isr[cpu] = a; }
                if (a.Length == 0) { a = new long[2]; isr[cpu] = a; }
                a[0]++; if (ini > 0 && t >= ini && t - ini < 10000000) a[1] += t - ini;
                if (f.Count > iDatos + 3) { var v = Dar(vectores, cpu); long vec = Num(f[iDatos + 3]); long c; v.TryGetValue(vec, out c); v[vec] = c + 1; }
            } else if (ev == "Thread" && (tipo == "DCStart" || tipo == "Start")) {
                long pid = Num(f[iDatos]), tid = Num(f[iDatos + 1]);
                var h = Dar(hilos, tid); h.pid = pid;
                string n = Utf16Hex(f[f.Count - 1]); if (n.Length > 0) h.nombre = n;
            } else if (ev == "Thread" && tipo == "SetName") {
                long pid = Num(f[iDatos]), tid = Num(f[iDatos + 1]);
                var h = Dar(hilos, tid); h.pid = pid; h.nombre = f[iDatos + 2];
            } else if (ev == "Process" && (tipo == "DCStart" || tipo == "Start")) {
                long pid = Num(f[iDatos + 1]);
                // ImageFileName: el segundo texto entre comillas (después del SID).
                int k = 0; string img = "";
                var partes = l.Split('"');
                for (int i = 1; i < partes.Length; i += 2) { k++; if (k == 2) { img = partes[i]; break; } }
                if (pid >= 0 && img.Length > 0) procesos[pid] = img;
            }
        }
    }
}
"@

$herr = [ordered]@{ wpr = (Get-Command wpr -ErrorAction SilentlyContinue).Source; tracerpt = (Get-Command tracerpt -ErrorAction SilentlyContinue).Source;
    xperf = (Get-Command xperf -ErrorAction SilentlyContinue).Source; wpaexporter = (Get-Command wpaexporter -ErrorAction SilentlyContinue).Source }
if (-not $herr.wpr -or -not $herr.tracerpt) {
    [Console]::Out.WriteLine((@{ error = "faltan wpr.exe o tracerpt.exe (vienen con Windows)"; herramientas = $herr } | ConvertTo-Json -Compress))
    exit 1
}
cmd /c "wpr -cancel >nul 2>&1"
& wpr -start "$perfil!BA.Nucleos" -filemode | Out-Null
if ($LASTEXITCODE -ne 0) {
    [Console]::Out.WriteLine((@{ error = "wpr -start falló ($LASTEXITCODE): ¿consola sin permisos de administrador?"; herramientas = $herr } | ConvertTo-Json -Compress))
    exit 1
}
Start-Sleep -Seconds $Segundos
# Los nombres de los hilos vivos (los que se nombraron antes de grabar no tienen SetName en la traza).
$vivos = @{}
foreach ($p in (Get-Process | Where-Object { $Nombres -contains $_.ProcessName })) {
    foreach ($t in $p.Threads) { $n = [BaEtw]::NombreVivo([uint32]$t.Id); if ($n) { $vivos[[long]$t.Id] = $n } }
}
& wpr -stop $etl | Out-Null
& tracerpt $etl -o $csv -of CSV -y | Out-Null
[BaEtw]::Leer($csv)

$seg = [math]::Max(0.001, ([BaEtw]::t1 - [BaEtw]::t0) / 1e7)
# Un núcleo ocioso (en C-state) no recibe la interrupción de muestreo: lo ocupado sale de
# las muestras de cada hilo × 1 ms (el intervalo de WPR) sobre lo grabado, no de la proporción.
$MsMuestra = 1.0
function NombreDe([long]$tid) {
    $h = $null
    if ([BaEtw]::hilos.ContainsKey($tid)) { $h = [BaEtw]::hilos[$tid] }
    $n = if ($vivos.ContainsKey($tid)) { $vivos[$tid] } elseif ($h -and $h.nombre) { $h.nombre } else { "(sin nombre)" }
    $pid_ = if ($h) { $h.pid } else { -1 }
    $proc = if ([BaEtw]::procesos.ContainsKey($pid_)) { [BaEtw]::procesos[$pid_] -replace "\.exe$", "" } elseif ($tid -eq 0) { "Idle" } else { "?" }
    return @($proc, $pid_, ($n -replace "\d+", "#"))
}
$nucleos = @()
$porHilo = @{}
foreach ($cpu in ([BaEtw]::muestras.Keys | Sort-Object)) {
    $m = [BaEtw]::muestras[$cpu]
    $tot = 0; foreach ($v in $m.Values) { $tot += $v }
    $idle = 0; if ($m.ContainsKey(0)) { $idle = $m[0] }
    $lista = @()
    foreach ($tid in $m.Keys) {
        if ($tid -eq 0) { continue }
        $pnn = NombreDe $tid
        $pct = 100.0 * $m[$tid] * $MsMuestra / 1000.0 / $seg  # muestras de 1 ms: tiempo en el núcleo / tiempo grabado
        $lista += [pscustomobject]@{ proceso = $pnn[0]; pid = $pnn[1]; nombre = $pnn[2]; pct = [math]::Round($pct, 2) }
        if ($Nombres -contains $pnn[0]) {
            $k = "$($pnn[0])|$($pnn[2])"
            if (-not $porHilo.ContainsKey($k)) { $porHilo[$k] = @{ proceso = $pnn[0]; nombre = $pnn[2]; pct = 0.0; nucleos = @{} } }
            $porHilo[$k].pct += $pct
            $porHilo[$k].nucleos["$cpu"] = [math]::Round(($porHilo[$k].nucleos["$cpu"] + $pct), 2)
        }
    }
    $d = [BaEtw]::dpc[$cpu]; $i = [BaEtw]::isr[$cpu]
    $vec = @()
    if ([BaEtw]::vectores.ContainsKey($cpu)) {
        $v = [BaEtw]::vectores[$cpu]
        $vec = @($v.Keys | Sort-Object { - $v[$_] } | Select-Object -First 3 | ForEach-Object { @{ vector = $_; n = $v[$_] } })
    }
    $nucleos += [ordered]@{
        nucleo = $cpu; ocupado_pct = [math]::Round([math]::Min(100.0, 100.0 * ($tot - $idle) * $MsMuestra / 1000.0 / $seg), 1)
        dpc = if ($d) { @{ n = $d[0]; ms = [math]::Round($d[1] / 1e4, 1); pct = [math]::Round(100.0 * $d[1] / 1e7 / $seg, 2) } } else { $null }
        isr = if ($i) { @{ n = $i[0]; ms = [math]::Round($i[1] / 1e4, 1); pct = [math]::Round(100.0 * $i[1] / 1e7 / $seg, 2); vectores = $vec } } else { $null }
        hilos = @($lista | Sort-Object pct -Descending | Select-Object -First 8)
    }
}
$hilos = @($porHilo.Values | Sort-Object { - $_.pct } | Select-Object -First 40 | ForEach-Object {
    [ordered]@{ proceso = $_.proceso; nombre = $_.nombre; pct = [math]::Round($_.pct, 2); nucleos = $_.nucleos } })
$out = [ordered]@{ segundos = [math]::Round($seg, 1); nota = "pct de cada núcleo (muestreo de 1 ms); el de un hilo en «hilos» es la suma sobre los núcleos (100 = un núcleo entero)";
    nucleos = $nucleos; hilos = $hilos; herramientas = $herr }
[Console]::Out.WriteLine(($out | ConvertTo-Json -Compress -Depth 7))
if (-not $Conservar) { Remove-Item -Force -ErrorAction SilentlyContinue $etl, $csv; Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "$etl.NGENPDB" }
