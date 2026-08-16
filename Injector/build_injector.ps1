# Silent build script: Osiris.dll (embedded) -> EmbeddedDll.h -> Injector.exe (x64)
# No window is shown during the build. Output goes to build_*.log next to this script.
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root

# locate MSBuild: $env:MSBUILD_PATH > msbuild in PATH > default VS install
$msbuild = $env:MSBUILD_PATH
if (-not $msbuild) {
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) { $msbuild = $cmd.Source }
    else { $msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' }
}
if (-not (Test-Path $msbuild)) {
    throw "MSBuild not found. Set MSBUILD_PATH or install VS Build Tools."
}

function Invoke-Build([string]$Project, [string]$LogBase) {
    $stdout = Join-Path $root "$LogBase.log"
    $stderr = Join-Path $root "$LogBase.err.log"
    $p = Start-Process -FilePath $msbuild `
        -ArgumentList @($Project, "/p:Configuration=$Configuration", '/p:Platform=x64', '/m', '/v:m', '/nologo') `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($p.ExitCode -ne 0) {
        throw "MSBuild failed for $Project (exit $($p.ExitCode)). See $stdout / $stderr"
    }
    Write-Output "OK: $Project -> $stdout"
}

# 1) build Osiris.dll (Release x64) so it can be embedded
Invoke-Build (Join-Path $repo 'Osiris.sln') 'build_osiris'

# 2) generate EmbeddedDll.h from the freshly built DLL
& (Join-Path $root 'generate_embedded.ps1') -Configuration $Configuration

# 3) build the injector
Invoke-Build (Join-Path $root 'Injector.vcxproj') 'build_injector'

$exe = Join-Path $root "x64\$Configuration\Injector.exe"
Write-Output "DONE: $exe"
