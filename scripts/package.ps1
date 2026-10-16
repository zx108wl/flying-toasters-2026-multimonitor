# Build a portable launcher-only ZIP. No third-party binaries.
param([string]$ZigPath = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1') -ZigPath $ZigPath
$output = Join-Path $repoRoot 'build/output'
New-Item -ItemType Directory $output -Force | Out-Null
$files = @('build/multisaver.scr', 'multisaver.example.ini', 'README.md', 'LICENSE', 'CREDITS.md', 'docs/CONFIGURATION.md', 'docs/BUILD.md') | ForEach-Object { Join-Path $repoRoot $_ }
$zip = Join-Path $output 'MultiSaver-Portable.zip'
Compress-Archive -LiteralPath $files -DestinationPath $zip -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  MultiSaver-Portable.zip" | Set-Content (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Portable package: $zip"
