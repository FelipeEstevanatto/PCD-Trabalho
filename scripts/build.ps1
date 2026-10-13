# Compila os binarios do filtro bilateral (Windows / MinGW-w64).
# Equivalente ao Makefile, para quem nao tem "make" no PATH.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

function Invoke-Gcc([string[]]$gccArgs) {
    & gcc @gccArgs
    if ($LASTEXITCODE -ne 0) { throw "gcc falhou: gcc $($gccArgs -join ' ')" }
}

Invoke-Gcc @("-O3", "-Wall", "-o", "bilateral_seq.exe", "src/bilateral_seq.c", "src/bilateral_common.c", "-lm")
Invoke-Gcc @("-O3", "-Wall", "-fopenmp", "-o", "bilateral_omp.exe", "src/bilateral_omp.c", "src/bilateral_common.c", "-lm")
Invoke-Gcc @("-O3", "-Wall", "-o", "ruido.exe", "src/ruido.c", "src/bilateral_common.c", "-lm")

Write-Host "OK: bilateral_seq.exe  bilateral_omp.exe  ruido.exe"
