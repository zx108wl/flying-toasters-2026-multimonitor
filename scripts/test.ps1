param([string]$ZigPath = 'zig')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testDir = Join-Path $repoRoot 'build/test-work'
New-Item -ItemType Directory $testDir -Force | Out-Null
$testExe = Join-Path $testDir 'launcher-test.exe'
& $ZigPath cc (Join-Path $repoRoot 'tests/launcher_test.c') -o $testExe `
    -target x86_64-windows-gnu -municode -O0 -Wall -Wextra -Werror `
    -Wno-missing-field-initializers -lkernel32 -luser32 -lshell32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw 'Test harness compilation failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw "Native tests failed: $LASTEXITCODE" }
