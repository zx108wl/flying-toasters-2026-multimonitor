# Builds the wrapper .scr from src/wrapper.c using Zig.
# Output: build/multisaver.scr

param(
  [string]$ZigPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
$outFile  = Join-Path $buildDir "multisaver.scr"

# Locate Zig
$zig = if ($ZigPath) { Get-Item -LiteralPath $ZigPath } else { Get-Command zig -ErrorAction SilentlyContinue }
if (-not $zig) {
  $zig = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter "zig.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $zig) {
  Write-Error "Zig not found. Install with:  winget install zig.zig"
}
$zigPath = if ($zig -is [System.IO.FileInfo]) { $zig.FullName } else { $zig.Source }
Write-Host "Using Zig: $zigPath" -ForegroundColor Cyan

if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }

$src = Join-Path $repoRoot "src"
$args = @(
  "cc",
  (Join-Path $src "wrapper.c"),
  (Join-Path $src "wrapper.rc"),
  "-o", $outFile,
  "-Oz", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
  "-target", "x86_64-windows-gnu",
  "-municode",
  "-Wl,--subsystem,windows",
  "-Wl,--gc-sections",
  "-Wl,-s",
  "-ffunction-sections",
  "-fdata-sections",
  "-fno-unwind-tables",
  "-fno-asynchronous-unwind-tables",
  "-fno-stack-protector",
  "-lkernel32", "-luser32", "-lshell32", "-lgdi32", "-ladvapi32"
)

Push-Location $src
try {
  & $zigPath @args
  if ($LASTEXITCODE -ne 0) { throw "zig cc failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

$size = (Get-Item $outFile).Length
Write-Host ("OK: {0}  ({1:N0} bytes / {2:N1} KB)" -f $outFile, $size, ($size/1KB)) -ForegroundColor Green
