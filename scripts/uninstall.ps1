#Requires -Version 5.1
<#
.SYNOPSIS
  Uninstalls the Flying Toasters 2026 multi-monitor wrapper (portable variant).
  Restores the original screensaver from backup if one exists.
#>

param(
  [string]$InstallDir = "C:\Program Files (x86)\Flying Toasters 2026",
  [string]$SystemDir  = "C:\Windows\System32"
)

$ErrorActionPreference = "Stop"

$currentId = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin   = ([Security.Principal.WindowsPrincipal]$currentId).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  Write-Host "Requesting elevation (UAC)..." -ForegroundColor Yellow
  $args = @("-NoProfile","-ExecutionPolicy","Bypass","-File", ('"{0}"' -f $MyInvocation.MyCommand.Path))
  Start-Process -FilePath "powershell.exe" -ArgumentList $args -Verb RunAs
  exit
}

Get-Process -Name "flyingtoasters2026","flyingtoasters2026_engine" -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }

$sysScr    = Join-Path $SystemDir "flyingtoasters2026.scr"
$sysEngine = Join-Path $SystemDir "flyingtoasters2026_engine.scr"
$sysBackup = Join-Path $SystemDir "flyingtoasters2026.scr.original_backup"

if (Test-Path -LiteralPath $sysBackup) {
  Copy-Item -LiteralPath $sysBackup -Destination $sysScr -Force
  Remove-Item -LiteralPath $sysBackup -Force
  Write-Host "Restored original .scr in System32." -ForegroundColor Green
} else {
  Write-Host "No backup; removing wrapper from System32." -ForegroundColor Yellow
  Remove-Item -LiteralPath $sysScr -Force -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath $sysEngine -Force -ErrorAction SilentlyContinue

if (Test-Path -LiteralPath $InstallDir) {
  Remove-Item -LiteralPath $InstallDir -Recurse -Force
  Write-Host "Removed $InstallDir" -ForegroundColor Cyan
}

$cp = "HKCU:\Control Panel\Desktop"
$cur = (Get-ItemProperty -Path $cp -Name "SCRNSAVE.EXE" -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
if ($cur -and $cur -ieq $sysScr) {
  Set-ItemProperty -Path $cp -Name "SCRNSAVE.EXE" -Value "" -Force
  Set-ItemProperty -Path $cp -Name "ScreenSaveActive" -Value "0" -Force
  Write-Host "Active screensaver cleared in HKCU." -ForegroundColor Cyan
}

Write-Host ""
Write-Host "Uninstall complete." -ForegroundColor Green
Write-Host "Press Enter to close..."
[void][System.Console]::ReadLine()
