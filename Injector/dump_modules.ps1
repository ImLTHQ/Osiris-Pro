# Developer tool: dump the loaded DLL list of cs2.exe (or a specific pid).
# Used to verify the client.dll injection-timing gate:
#   at the region-select dialog client.dll is NOT loaded;
#   inside the real game it IS loaded.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Injector\dump_modules.ps1
#   powershell -ExecutionPolicy Bypass -File Injector\dump_modules.ps1 -Pid 1234

param([int]$TargetPid = 0)

if ($TargetPid -eq 0) {
    $proc = Get-Process -Name cs2 -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        Write-Output 'cs2.exe is not running.'
        exit 1
    }
    $TargetPid = $proc.Id
}

$proc = Get-Process -Id $TargetPid -ErrorAction SilentlyContinue
if (-not $proc) {
    Write-Output "process $TargetPid not found"
    exit 1
}

Write-Output ("PID: $TargetPid  (" + $proc.ProcessName + ")")
Write-Output ("TOTAL MODULES: " + $proc.Modules.Count)
Write-Output '=== engine-related modules ==='
$proc.Modules |
    Where-Object { $_.ModuleName -match '^(client|engine2|schemasystem|tier0|materialsystem2|worldrenderer|soundsystem|scenesystem|panorama|gameui)\.dll$' } |
    Select-Object ModuleName, FileName | Format-Table -AutoSize -Wrap
Write-Output '=== all modules ==='
$proc.Modules | Select-Object ModuleName, FileName | Format-Table -AutoSize -Wrap
