# Collect repeatable per-process CPU and memory samples on a ThinkPad.
# Compare Build #11 and the optimization build with identical conditions.
param(
    [ValidateRange(10, 900)][int]$DurationSeconds = 90,
    [ValidateRange(1, 10)][int]$IntervalSeconds = 2,
    [string]$OutputCsv = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$stats = @{}
$clock = [System.Diagnostics.Stopwatch]::StartNew()

while ($clock.Elapsed.TotalSeconds -lt $DurationSeconds) {
    $elapsed = $clock.Elapsed.TotalSeconds
    $processes = @(Get-Process -Name TPFanControl -ErrorAction SilentlyContinue)
    foreach ($p in $processes) {
        try {
            $idKey = [string]$p.Id
            $cpuSeconds = $p.TotalProcessorTime.TotalSeconds
            $privateMiB = $p.PrivateMemorySize64 / 1MB
            $workingMiB = $p.WorkingSet64 / 1MB
            if (-not $stats.ContainsKey($idKey)) {
                $stats[$idKey] = [pscustomobject]@{
                    Pid = $p.Id
                    Session = $p.SessionId
                    Samples = 0
                    FirstAt = $elapsed
                    LastAt = $elapsed
                    FirstCpu = $cpuSeconds
                    LastCpu = $cpuSeconds
                    PrivateTotal = 0.0
                    WorkingTotal = 0.0
                    HandlesTotal = 0.0
                }
            }
            $s = $stats[$idKey]
            $s.Samples++
            $s.LastAt = $elapsed
            $s.LastCpu = $cpuSeconds
            $s.PrivateTotal += $privateMiB
            $s.WorkingTotal += $workingMiB
            $s.HandlesTotal += $p.HandleCount
        } catch {
            # The process might exit or a session might become inaccessible.
        }
    }
    Start-Sleep -Seconds $IntervalSeconds
}

if ($stats.Count -eq 0) {
    Write-Warning "No TPFanControl.exe process was found. Start the application first."
    exit 1
}

$report = foreach ($item in $stats.Values) {
    $duration = [math]::Max(0.01, $item.LastAt - $item.FirstAt)
    [pscustomobject]@{
        PID = $item.Pid
        Session = $item.Session
        Samples = $item.Samples
        CpuPctOneCore = [math]::Round(
            [math]::Max(0, $item.LastCpu - $item.FirstCpu) * 100 / $duration, 2)
        AvgPrivateMiB = [math]::Round($item.PrivateTotal / $item.Samples, 2)
        AvgWorkingSetMiB = [math]::Round($item.WorkingTotal / $item.Samples, 2)
        AvgHandles = [math]::Round($item.HandlesTotal / $item.Samples, 0)
    }
}
$report = @($report | Sort-Object Session, PID)
$report | Format-Table -AutoSize
if ($OutputCsv) {
    $report | Export-Csv -Path $OutputCsv -Encoding UTF8 -NoTypeInformation
    Write-Host "Saved: $OutputCsv"
}
Write-Host "CPU percentage is relative to ONE logical core. Results are estimates."
Write-Host "Compare foreground vs tray, same temperature/load, similar duration."
