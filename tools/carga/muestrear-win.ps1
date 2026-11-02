# tools/carga/muestrear-win.ps1 — las muestras de la máquina Windows para medir-sala.py --remoto
#
# Una línea JSON por muestra (cada -Cada segundos, durante -Segundos) en la salida estándar:
#   t            hora unix (s) de la muestra (la de la PC)
#   procesos     "ba-app:<pid>" → cpu_pct (% de UN núcleo, como ps), rss_mb (working set),
#                privados_mb, hilos, csw_s (cambios de contexto/s, suma de sus hilos)
#   gpu          de los procesos ba-*: 3d_pct, video_decode_pct, video_encode_pct, compute_pct,
#                copy_pct (el motor más ocupado de cada tipo, como el Administrador de tareas),
#                dedicada_mb, compartida_mb; y total_3d_pct (todos los procesos)
#   red          entra_mbps / sale_mbps de los adaptadores (sin los virtuales ni el loopback)
#   memoria      disponible_mb, comprometida_mb
#   cpu_maquina  user/sys/idle de toda la máquina (como top)
#   hilos        "ba-gateway:<pid>" → {nombre: [cpu_pct, csw_s, n]}: cada hilo por su NOMBRE
#                (GetThreadDescription, el que pone ba::platform::nombrarHilo; los dígitos van
#                como «#» y `n` dice cuántos hilos tienen ese nombre); CPU en % de un núcleo y
#                cambios de contexto/s (NtQuerySystemInformation: rápido; WMI tardaba 15-20 s)
#   nucleos      por núcleo lógico (Processor Information): [ocupado_pct, dpc_pct, irq_pct,
#                interrupciones_s, dpcs_s, mhz] (mhz: la frecuencia efectiva, ActualFrequency)
# La PRIMERA línea es {"t", "rss": […], "adaptadores": […]}: a qué núcleos manda la placa de
# red sus colas (Get-NetAdapterRss) y la velocidad de cada adaptador.
# Todo por WMI con las clases «Raw» (los contadores de rendimiento tienen nombres en castellano;
# WMI no) y las diferencias las saca este script. Lo lanza medir-sala.py por ssh:
#   ssh windows powershell -NoProfile -ExecutionPolicy Bypass -File C:\ba-pruebas\muestrear-win.ps1 -Segundos 300
param([int]$Segundos = 300, [double]$Cada = 1.0, [string[]]$Nombres = @("ba-app", "ba-gateway", "ba-web"))
$ErrorActionPreference = "SilentlyContinue"
$Nombres = @($Nombres | ForEach-Object { $_ -split "," } | Where-Object { $_ })  # por -File llega "a,b" entero
[Console]::OutputEncoding = [Text.Encoding]::UTF8

# Los nombres de los hilos (GetThreadDescription, Windows 10 1607+): se cachean por id de hilo.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class BaHilos {
    [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint acceso, bool hereda, uint tid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr h, out IntPtr texto);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
    public static string Nombre(uint tid) {
        IntPtr h = OpenThread(0x0800, false, tid); // THREAD_QUERY_LIMITED_INFORMATION
        if (h == IntPtr.Zero) return "";
        try {
            IntPtr p;
            if (GetThreadDescription(h, out p) < 0 || p == IntPtr.Zero) return "";
            string s = Marshal.PtrToStringUni(p);
            LocalFree(p);
            return s ?? "";
        } finally { CloseHandle(h); }
    }

    // Los hilos de los procesos pedidos, de UNA llamada a NtQuerySystemInformation
    // (SystemProcessInformation, lo que usa el Administrador de tareas): tiempo de CPU (100 ns) y
    // cambios de contexto acumulados por hilo. WMI (Win32_PerfRawData_PerfProc_Thread) tardaba
    // 15-20 s por muestra con 300 hilos en el gateway y gastaba ~85 % de un núcleo en WmiPrvSE.
    public class Hilo { public int IDProcess; public uint IDThread; public long PercentProcessorTime; public long ContextSwitchesPerSec; public long Timestamp_Sys100NS; }
    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int clase, IntPtr buf, int largo, out int necesita);
    public static Hilo[] Hilos(int[] pids) {
        var quiero = new System.Collections.Generic.HashSet<long>();
        foreach (var x in pids) quiero.Add(x);
        int largo = 1 << 22; IntPtr buf = IntPtr.Zero;
        try {
            for (;;) {
                buf = Marshal.AllocHGlobal(largo);
                int nec;
                int st = NtQuerySystemInformation(5, buf, largo, out nec); // SystemProcessInformation
                if (st == 0) break;
                Marshal.FreeHGlobal(buf); buf = IntPtr.Zero;
                if ((uint)st != 0xC0000004u) return new Hilo[0];           // STATUS_INFO_LENGTH_MISMATCH: más grande
                largo = Math.Max(largo * 2, nec + (1 << 20));
            }
            long ahora = DateTime.UtcNow.Ticks;
            var out_ = new System.Collections.Generic.List<Hilo>();
            int off = 0;
            for (;;) {
                IntPtr p = IntPtr.Add(buf, off);
                int sig = Marshal.ReadInt32(p, 0);
                int nh = Marshal.ReadInt32(p, 4);
                long pid = Marshal.ReadInt64(p, 80);                         // UniqueProcessId (x64)
                if (quiero.Contains(pid)) {
                    for (int i = 0; i < nh; i++) {
                        IntPtr h = IntPtr.Add(p, 0x100 + i * 0x50);           // SYSTEM_THREAD_INFORMATION (x64)
                        out_.Add(new Hilo { IDProcess = (int)pid, IDThread = (uint)Marshal.ReadInt64(h, 48),
                            PercentProcessorTime = Marshal.ReadInt64(h, 0) + Marshal.ReadInt64(h, 8),
                            ContextSwitchesPerSec = (uint)Marshal.ReadInt32(h, 64), Timestamp_Sys100NS = ahora });
                    }
                }
                if (sig == 0) break;
                off += sig;
            }
            return out_.ToArray();
        } finally { if (buf != IntPtr.Zero) Marshal.FreeHGlobal(buf); }
    }
}
"@
$NombresHilo = @{}
function NombreHilo([uint32]$tid) {
    if (-not $NombresHilo.ContainsKey($tid)) {
        $n = [BaHilos]::Nombre($tid)
        if (-not $n) { $n = "(sin nombre)" }
        $NombresHilo[$tid] = ($n -replace "\d+", "#")
    }
    return $NombresHilo[$tid]
}

function EsNuestro([string]$n) { foreach ($x in $Nombres) { if ($n -eq $x -or $n -like "$x#*") { return $true } }; return $false }

function Leer {
    $s = @{ t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0 }
    $s.procs = @(Get-CimInstance Win32_PerfRawData_PerfProc_Process | Where-Object { EsNuestro $_.Name } |
        Select-Object Name, IDProcess, PercentProcessorTime, Timestamp_Sys100NS, ThreadCount, WorkingSet, PrivateBytes)
    $s.hilos = @{}
    $s.porHilo = @{}
    $todos = [BaHilos]::Hilos([int[]]@($s.procs | ForEach-Object { [int]$_.IDProcess }))
    foreach ($p in $s.procs) {
        $hs = @($todos | Where-Object { $_.IDProcess -eq [int]$p.IDProcess })
        $s.hilos[[int]$p.IDProcess] = ($hs | Measure-Object ContextSwitchesPerSec -Sum).Sum
        $s.porHilo[[int]$p.IDProcess] = $hs
    }
    $s.nucleos = @(Get-CimInstance Win32_PerfRawData_Counters_ProcessorInformation | Where-Object { $_.Name -notmatch "_Total" } |
        Select-Object Name, PercentProcessorTime, PercentDPCTime, PercentInterruptTime, InterruptsPersec, DPCsQueuedPersec,
            ActualFrequency, ActualFrequency_Base, Timestamp_Sys100NS)
    $s.gpu = @(Get-CimInstance Win32_PerfRawData_GPUPerformanceCounters_GPUEngine | Select-Object Name, RunningTime)
    $s.gpumem = @(Get-CimInstance Win32_PerfRawData_GPUPerformanceCounters_GPUProcessMemory | Select-Object Name, DedicatedUsage, SharedUsage)
    $s.red = @(Get-CimInstance Win32_PerfRawData_Tcpip_NetworkInterface | Where-Object { $_.Name -notmatch "isatap|Loopback|Teredo|WAN Miniport|Virtual|vEthernet" } |
        Select-Object Name, BytesReceivedPersec, BytesSentPersec)
    $s.cpu = Get-CimInstance Win32_PerfRawData_PerfOS_Processor -Filter "Name = '_Total'" | Select-Object PercentProcessorTime, PercentUserTime, PercentPrivilegedTime, Timestamp_Sys100NS
    $s.os = Get-CimInstance Win32_OperatingSystem | Select-Object FreePhysicalMemory, TotalVirtualMemorySize, FreeVirtualMemorySize
    $s.mem = Get-CimInstance Win32_PerfRawData_PerfOS_Memory | Select-Object AvailableMBytes, CommittedBytes
    return $s
}

function PidDe([string]$n) { if ($n -match "pid_(\d+)_") { return [int]$Matches[1] }; return -1 }
function TipoDe([string]$n) { if ($n -match "engtype_(.+)$") { return $Matches[1] }; return "?" }
function MotorDe([string]$n) { if ($n -match "(luid_.*_eng_\d+)") { return $Matches[1] }; return $n }

# Una vez al empezar: la placa de red (RSS: qué núcleos atienden sus colas) y los adaptadores.
$rss = @(Get-NetAdapterRss | Select-Object Name, InterfaceDescription, Enabled, NumberOfReceiveQueues, Profile,
    BaseProcessorNumber, MaxProcessorNumber, MaxProcessors,
    @{ n = "Procesadores"; e = { ($_.RssProcessorArray | ForEach-Object { "$($_.ProcessorGroup):$($_.ProcessorNumber)" }) -join " " } })
$adap = @(Get-NetAdapter | Where-Object Status -eq "Up" | Select-Object Name, InterfaceDescription, LinkSpeed)
# Algunas placas (la I225-V de la .177) no salen en Get-NetAdapterRss: lo de RSS en sus propiedades avanzadas.
$rssAv = @(Get-NetAdapterAdvancedProperty -Name ($adap | ForEach-Object Name) -ErrorAction SilentlyContinue |
    Where-Object { $_.RegistryKeyword -match "RSS|Rss|Interrupt|Moderation" } | Select-Object Name, DisplayName, DisplayValue)
[Console]::Out.WriteLine((@{ t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0; rss = $rss; rss_avanzado = $rssAv; adaptadores = $adap } | ConvertTo-Json -Compress -Depth 5))
[Console]::Out.Flush()

$ant = Leer
$fin = (Get-Date).AddSeconds($Segundos)
while ((Get-Date) -lt $fin) {
    Start-Sleep -Milliseconds ([int]($Cada * 1000))
    $act = Leer
    $dt = [math]::Max(0.001, $act.t - $ant.t)
    $nuestros = @{}
    $o = [ordered]@{ t = $act.t; procesos = [ordered]@{} }
    foreach ($p in $act.procs) {
        $q = $ant.procs | Where-Object { $_.IDProcess -eq $p.IDProcess } | Select-Object -First 1
        $base = ($p.Name -split "#")[0]
        $nuestros[[int]$p.IDProcess] = $true
        $cpu = $null; $csw = $null
        if ($q) {
            $cpu = [math]::Round(($p.PercentProcessorTime - $q.PercentProcessorTime) / [math]::Max(1, ($p.Timestamp_Sys100NS - $q.Timestamp_Sys100NS)) * 100, 1)
            if ($ant.hilos.ContainsKey([int]$p.IDProcess)) { $csw = [math]::Round(($act.hilos[[int]$p.IDProcess] - $ant.hilos[[int]$p.IDProcess]) / $dt) }
        }
        $o.procesos["${base}:$($p.IDProcess)"] = [ordered]@{ pid = [int]$p.IDProcess; cpu_pct = $cpu; rss_mb = [math]::Round($p.WorkingSet / 1MB);
            privados_mb = [math]::Round($p.PrivateBytes / 1MB); hilos = [int]$p.ThreadCount; csw_s = $csw }
    }
    # Cada hilo por su nombre: CPU (% de un núcleo) y cambios de contexto/s, sumados por nombre.
    $o.hilos = [ordered]@{}
    foreach ($p in $act.procs) {
        $id = [int]$p.IDProcess
        if (-not $ant.porHilo.ContainsKey($id)) { continue }
        $prevH = @{}; foreach ($h in $ant.porHilo[$id]) { $prevH[[uint32]$h.IDThread] = $h }
        $porNombre = @{}
        foreach ($h in $act.porHilo[$id]) {
            $q = $prevH[[uint32]$h.IDThread]
            if (-not $q) { continue }
            $dth = [math]::Max(1, $h.Timestamp_Sys100NS - $q.Timestamp_Sys100NS)
            $cpuH = [math]::Max(0, ($h.PercentProcessorTime - $q.PercentProcessorTime) / $dth * 100)
            $cswH = [math]::Max(0, ($h.ContextSwitchesPerSec - $q.ContextSwitchesPerSec) / ($dth / 1e7))
            $nom = NombreHilo ([uint32]$h.IDThread)
            if (-not $porNombre.ContainsKey($nom)) { $porNombre[$nom] = @(0.0, 0.0, 0) }
            $porNombre[$nom][0] += $cpuH; $porNombre[$nom][1] += $cswH; $porNombre[$nom][2] += 1
        }
        $base = ($p.Name -split "#")[0]
        $m = [ordered]@{}
        foreach ($k in $porNombre.Keys) { $v = $porNombre[$k]; $m[$k] = @([math]::Round($v[0], 1), [math]::Round($v[1]), $v[2]) }
        $o.hilos["${base}:$id"] = $m
    }
    # Cada núcleo lógico: ocupado, DPC e interrupciones (% del núcleo), interrupciones/s, DPC/s y MHz.
    $prevN = @{}; foreach ($n in $ant.nucleos) { $prevN[$n.Name] = $n }
    $nuc = @()
    foreach ($n in ($act.nucleos | Sort-Object { $gc = $_.Name -split ","; [int]$gc[0] * 1000 + [int]$gc[1] })) {
        $q = $prevN[$n.Name]
        if (-not $q) { continue }
        $dtn = [math]::Max(1, $n.Timestamp_Sys100NS - $q.Timestamp_Sys100NS)
        $seg = $dtn / 1e7
        $libre = ($n.PercentProcessorTime - $q.PercentProcessorTime) / $dtn * 100  # contador invertido: el ocioso
        $fb = $n.ActualFrequency_Base - $q.ActualFrequency_Base
        $mhz = $null; if ($fb -gt 0) { $mhz = [math]::Round(($n.ActualFrequency - $q.ActualFrequency) / $fb) }
        $nuc += , @([math]::Round([math]::Max(0, 100 - $libre), 1), [math]::Round(($n.PercentDPCTime - $q.PercentDPCTime) / $dtn * 100, 2),
            [math]::Round(($n.PercentInterruptTime - $q.PercentInterruptTime) / $dtn * 100, 2),
            [math]::Round(($n.InterruptsPersec - $q.InterruptsPersec) / $seg), [math]::Round(($n.DPCsQueuedPersec - $q.DPCsQueuedPersec) / $seg), $mhz)
    }
    $o.nucleos = $nuc
    # GPU: por motor (la suma de lo nuestro y la de todos), y por tipo el motor más ocupado.
    $prev = @{}; foreach ($g in $ant.gpu) { $prev[$g.Name] = $g.RunningTime }
    $porMotor = @{}; $porMotorTodos = @{}
    foreach ($g in $act.gpu) {
        if (-not $prev.ContainsKey($g.Name)) { continue }
        $d = ($g.RunningTime - $prev[$g.Name]) / 1e7 / $dt * 100   # RunningTime: 100 ns
        if ($d -lt 0) { continue }
        $m = MotorDe $g.Name; $tipo = TipoDe $g.Name
        $porMotorTodos["$tipo|$m"] = $porMotorTodos["$tipo|$m"] + $d
        if ($nuestros.ContainsKey((PidDe $g.Name))) { $porMotor["$tipo|$m"] = $porMotor["$tipo|$m"] + $d }
    }
    $gpu = [ordered]@{}
    foreach ($k in $porMotor.Keys) {
        $tipo = ($k -split "\|")[0].ToLower() -replace "[^a-z0-9]+", "_"
        $c = "${tipo}_pct"; $v = [math]::Min(100, [math]::Round($porMotor[$k], 1))
        if (-not $gpu.Contains($c) -or $gpu[$c] -lt $v) { $gpu[$c] = $v }
    }
    $t3 = 0; foreach ($k in $porMotorTodos.Keys) { if ($k -like "3D|*") { $t3 = [math]::Max($t3, $porMotorTodos[$k]) } }
    $gpu["total_3d_pct"] = [math]::Min(100, [math]::Round($t3, 1))
    $ded = 0; $com = 0
    foreach ($g in $act.gpumem) { if ($nuestros.ContainsKey((PidDe $g.Name))) { $ded += $g.DedicatedUsage; $com += $g.SharedUsage } }
    $gpu["dedicada_mb"] = [math]::Round($ded / 1MB); $gpu["compartida_mb"] = [math]::Round($com / 1MB)
    $o.gpu = $gpu
    $ent = 0; $sal = 0
    foreach ($r in $act.red) {
        $q = $ant.red | Where-Object { $_.Name -eq $r.Name } | Select-Object -First 1
        if ($q) { $ent += $r.BytesReceivedPersec - $q.BytesReceivedPersec; $sal += $r.BytesSentPersec - $q.BytesSentPersec }
    }
    $o.red = [ordered]@{ entra_mbps = [math]::Round($ent * 8 / $dt / 1e6, 2); sale_mbps = [math]::Round($sal * 8 / $dt / 1e6, 2) }
    $o.memoria = [ordered]@{ disponible_mb = [int]$act.mem.AvailableMBytes; comprometida_mb = [math]::Round($act.mem.CommittedBytes / 1MB) }
    $dts = [math]::Max(1, $act.cpu.Timestamp_Sys100NS - $ant.cpu.Timestamp_Sys100NS)
    # PercentProcessorTime (raw) de _Total cuenta el tiempo OCIOSO (contador invertido).
    $idle = [math]::Round(($act.cpu.PercentProcessorTime - $ant.cpu.PercentProcessorTime) / $dts * 100, 1)
    $o.cpu_maquina = [ordered]@{ user = [math]::Round(($act.cpu.PercentUserTime - $ant.cpu.PercentUserTime) / $dts * 100, 1);
        sys = [math]::Round(($act.cpu.PercentPrivilegedTime - $ant.cpu.PercentPrivilegedTime) / $dts * 100, 1); idle = $idle }
    [Console]::Out.WriteLine(($o | ConvertTo-Json -Compress -Depth 5))
    [Console]::Out.Flush()
    $ant = $act
}
