$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $taskRoot
try {
    cmake --preset dev
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    cmake --build --preset dev
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    ctest --preset dev
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
} finally {
    Pop-Location
}
