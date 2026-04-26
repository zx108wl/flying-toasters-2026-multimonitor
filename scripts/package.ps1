# Builds the wrapper, then assembles:
#   build/output/FlyingToasters2026-MultiMonitor-1.0.0-Setup.exe
#   build/output/FlyingToasters2026-MultiMonitor-1.0.0-Portable.zip
#   build/output/SHA256SUMS.txt
#
# Requires: Zig (build.ps1) + Inno Setup 6 (winget install JRSoftware.InnoSetup)

param(
  [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$repoRoot   = Split-Path -Parent $PSScriptRoot
$buildDir   = Join-Path $repoRoot "build"
$stagingDir = Join-Path $buildDir "staging"
$outputDir  = Join-Path $buildDir "output"

# Step 1: build the wrapper
& (Join-Path $PSScriptRoot "build.ps1")

# Step 2: locate Inno Setup
$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $iscc) {
  $candidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
  )
  foreach ($c in $candidates) {
    if (Test-Path -LiteralPath $c) { $iscc = $c; break }
  }
}
if (-not $iscc) {
  Write-Error "Inno Setup 6 not found. Install with:  winget install JRSoftware.InnoSetup"
}
$isccPath = if ($iscc -is [System.IO.FileInfo]) { $iscc.FullName } elseif ($iscc -is [string]) { $iscc } else { $iscc.Source }
Write-Host "Using ISCC: $isccPath" -ForegroundColor Cyan

# Step 3: stage files for installer
if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stagingDir "source") -Force | Out-Null

Copy-Item (Join-Path $buildDir "flyingtoasters2026.scr")                 (Join-Path $stagingDir "flyingtoasters2026.scr") -Force
Copy-Item (Join-Path $repoRoot "engine\flyingtoasters2026_engine.scr")   (Join-Path $stagingDir "flyingtoasters2026_engine.scr") -Force
Copy-Item (Join-Path $repoRoot "README.md")                              (Join-Path $stagingDir "README.md") -Force
Copy-Item (Join-Path $repoRoot "LICENSE")                                (Join-Path $stagingDir "LICENSE.txt") -Force
Copy-Item (Join-Path $repoRoot "CREDITS.md")                             (Join-Path $stagingDir "CREDITS.txt") -Force
Copy-Item (Join-Path $repoRoot "src\wrapper.c")                          (Join-Path $stagingDir "source\wrapper.c") -Force
Copy-Item (Join-Path $repoRoot "src\wrapper.rc")                         (Join-Path $stagingDir "source\wrapper.rc") -Force
Copy-Item (Join-Path $repoRoot "src\app.manifest")                       (Join-Path $stagingDir "source\app.manifest") -Force
Copy-Item (Join-Path $PSScriptRoot "install.ps1")                        (Join-Path $stagingDir "install.ps1") -Force
Copy-Item (Join-Path $PSScriptRoot "uninstall.ps1")                      (Join-Path $stagingDir "uninstall.ps1") -Force
Copy-Item (Join-Path $PSScriptRoot "installer.iss")                      (Join-Path $stagingDir "installer.iss") -Force

if (-not (Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir -Force | Out-Null }

# Step 4: compile installer
Push-Location $stagingDir
try {
  & $isccPath "/Q" "/O$outputDir" "installer.iss"
  if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

# Step 5: build portable zip
$portableName = "FlyingToasters2026-MultiMonitor-$Version-Portable.zip"
$portableZip  = Join-Path $outputDir $portableName
if (Test-Path $portableZip) { Remove-Item $portableZip -Force }

# Use a clean staging tree without the .iss
$portableStaging = Join-Path $env:TEMP "ftms_portable_$(Get-Random)"
$portableInner   = Join-Path $portableStaging "FlyingToasters2026-MultiMonitor"
New-Item -ItemType Directory -Path $portableInner -Force | Out-Null
Get-ChildItem $stagingDir -Exclude "installer.iss" -Force | Copy-Item -Destination $portableInner -Recurse -Force
Compress-Archive -Path $portableInner -DestinationPath $portableZip -CompressionLevel Optimal
Remove-Item $portableStaging -Recurse -Force

# Step 6: hashes
$sumsFile = Join-Path $outputDir "SHA256SUMS.txt"
Get-ChildItem $outputDir -Filter "*.exe" | ForEach-Object {
  "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash, $_.Name
} | Set-Content $sumsFile -Encoding ASCII
Get-ChildItem $outputDir -Filter "*.zip" | ForEach-Object {
  "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash, $_.Name
} | Add-Content $sumsFile -Encoding ASCII

Write-Host ""
Write-Host "Output:" -ForegroundColor Green
Get-ChildItem $outputDir | Format-Table @{l='Name';e={$_.Name}}, @{l='Size';e={ if($_.PSIsContainer){''} else { '{0,10:N0} bytes  ({1:N2} MB)' -f $_.Length,($_.Length/1MB) } }} -AutoSize
