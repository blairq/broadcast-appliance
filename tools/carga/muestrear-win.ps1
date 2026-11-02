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
# Todo por WMI con las clases «Raw» (los contadores de rendimiento tienen nombres en castellano;
# WMI no) y las diferencias las saca este script. Lo lanza medir-sala.py por ssh:
#   ssh windows powershell -NoProfile -ExecutionPolicy Bypass -File C:\ba-pruebas\muestrear-win.ps1 -Segundos 300
param([int]$Segundos = 300, [double]$Cada = 1.0, [string[]]$Nombres = @("ba-app", "ba-gateway", "ba-web"))
$ErrorActionPreference = "SilentlyContinue"
$Nombres = @($Nombres | ForEach-Object { $_ -split "," } | Where-Object { $_ })  # por -File llega "a,b" entero
[Console]::OutputEncoding = [Text.Encoding]::UTF8

function EsNuestro([string]$n) { foreach ($x in $Nombres) { if ($n -eq $x -or $n -like "$x#*") { return $true } }; return $false }

function Leer {
    $s = @{ t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0 }
    $s.procs = @(Get-CimInstance Win32_PerfRawData_PerfProc_Process | Where-Object { EsNuestro $_.Name } |
        Select-Object Name, IDProcess, PercentProcessorTime, Timestamp_Sys100NS, ThreadCount, WorkingSet, PrivateBytes)
    $s.hilos = @{}
    foreach ($p in $s.procs) {
        $base = ($p.Name -split "#")[0]
        $s.hilos[[int]$p.IDProcess] = (Get-CimInstance Win32_PerfRawData_PerfProc_Thread -Filter "IDProcess = $($p.IDProcess)" |
            Measure-Object ContextSwitchesPerSec -Sum).Sum
    }
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
