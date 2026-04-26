#Requires -Version 5.1
<#
.SYNOPSIS
  Installs the Flying Toasters 2026 multi-monitor wrapper (portable variant).

.DESCRIPTION
  Copies the wrapper + engine into:
    C:\Program Files (x86)\Flying Toasters 2026\
    C:\Windows\System32\
  Backs up any pre-existing System32 .scr to .original_backup once.
  Self-elevates with UAC if needed.

.EXAMPLE
  Right-click this file -> Run with PowerShell
  - or -
  powershell -ExecutionPolicy Bypass -File .\install.ps1
#>

param(
  [string]$InstallDir = "C:\Program Files (x86)\Flying Toasters 2026",
  [string]$SystemDir  = "C:\Windows\System32"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- Re-elevate if needed ---
$currentId = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin   = ([Security.Principal.WindowsPrincipal]$currentId).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  Write-Host "Requesting elevation (UAC)..." -ForegroundColor Yellow
  $args = @("-NoProfile","-ExecutionPolicy","Bypass","-File", ('"{0}"' -f $MyInvocation.MyCommand.Path))
  Start-Process -FilePath "powershell.exe" -ArgumentList $args -Verb RunAs
  exit
}

$wrapperSrc = Join-Path $here "flyingtoasters2026.scr"
$engineSrc  = Join-Path $here "flyingtoasters2026_engine.scr"
if (-not (Test-Path -LiteralPath $wrapperSrc)) { throw "Missing $wrapperSrc in source folder." }
if (-not (Test-Path -LiteralPath $engineSrc))  { throw "Missing $engineSrc in source folder." }

if (-not (Test-Path -LiteralPath $InstallDir)) {
  New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
  Write-Host "Created: $InstallDir" -ForegroundColor Green
}

$sysScr    = Join-Path $SystemDir "flyingtoasters2026.scr"
$sysBackup = Join-Path $SystemDir "flyingtoasters2026.scr.original_backup"
if ((Test-Path -LiteralPath $sysScr) -and -not (Test-Path -LiteralPath $sysBackup)) {
  Copy-Item -LiteralPath $sysScr -Destination $sysBackup -Force
  Write-Host "Backed up existing .scr -> $sysBackup" -ForegroundColor Cyan
}

Get-Process -Name "flyingtoasters2026","flyingtoasters2026_engine" -ErrorAction SilentlyContinue | ForEach-Object {
  Write-Host ("Killing in-use process: PID {0}" -f $_.Id) -ForegroundColor Yellow
  try { $_.Kill() } catch {}
}

function Copy-Pair($srcWrapper, $srcEngine, $destDir) {
  Copy-Item -LiteralPath $srcWrapper -Destination (Join-Path $destDir "flyingtoasters2026.scr")        -Force
  Copy-Item -LiteralPath $srcEngine  -Destination (Join-Path $destDir "flyingtoasters2026_engine.scr") -Force
  Write-Host ("Installed to: {0}" -f $destDir) -ForegroundColor Green
}

Copy-Pair $wrapperSrc $engineSrc $InstallDir
Copy-Pair $wrapperSrc $engineSrc $SystemDir

foreach ($f in "README.md","LICENSE.txt","CREDITS.txt") {
  $p = Join-Path $here $f
  if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $InstallDir -Force }
}
$srcDir = Join-Path $here "source"
if (Test-Path -LiteralPath $srcDir) {
  $destSrc = Join-Path $InstallDir "source"
  if (Test-Path -LiteralPath $destSrc) { Remove-Item -LiteralPath $destSrc -Recurse -Force }
  Copy-Item -LiteralPath $srcDir -Destination $destSrc -Recurse -Force
}

$cp = "HKCU:\Control Panel\Desktop"
Set-ItemProperty -Path $cp -Name "SCRNSAVE.EXE"     -Value $sysScr -Force
Set-ItemProperty -Path $cp -Name "ScreenSaveActive" -Value "1"     -Force
if (-not (Get-ItemProperty -Path $cp -Name "ScreenSaveTimeOut" -ErrorAction SilentlyContinue)) {
  Set-ItemProperty -Path $cp -Name "ScreenSaveTimeOut" -Value "600" -Force
}

Write-Host ""
Write-Host "Done. Quick tests:" -ForegroundColor Green
Write-Host "  1) Settings -> Personalization -> Lock screen -> Screen saver"
Write-Host "     Pick 'flyingtoasters2026' and click Preview."
Write-Host "  2) Or run directly:"
Write-Host "       & '$sysScr' /s"
Write-Host ""
Write-Host "Press Enter to close..."
[void][System.Console]::ReadLine()
