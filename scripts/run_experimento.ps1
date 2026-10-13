# Reproduz o protocolo experimental do artigo (Sec. 4.4):
#   imagens {montanha_4k, montanha2_4k} x raios {3,5,7} x configs {seq,1,2,4,8,16}
#   Reps repeticoes por configuracao, sob o Intel PCM (tools/pcm.exe) quando disponivel.
#
# Cada configuracao gera um log em dados/resultados/<imagem>/<imagem>_K<raio>_<cfg>.txt
# com as repeticoes concatenadas (mesmo formato dos logs originais do artigo).
# Consolide depois com: python scripts/analisar.py
#
# Uso:
#   scripts\run_experimento.ps1
#   scripts\run_experimento.ps1 -Reps 2 -Radii 3 -Threads 4,8 -NoPcm   # teste rapido
param(
    [int]$Reps = 10,
    [int[]]$Radii = @(3, 5, 7),
    [int[]]$Threads = @(1, 2, 4, 8, 16),
    [string[]]$Images = @("dados/entrada/montanha_4k.ppm", "dados/entrada/montanha2_4k.ppm"),
    [double]$Sigma = 25,
    [switch]$NoPcm
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root   # os binarios leem caminhos relativos ao repositorio

$seq = "bilateral_seq.exe"
$omp = "bilateral_omp.exe"
$rui = "ruido.exe"
if (-not (Test-Path $seq) -or -not (Test-Path $omp) -or -not (Test-Path $rui)) {
    Write-Host "Binarios ausentes; compilando..."
    & (Join-Path $PSScriptRoot "build.ps1")
}

$pcm = Join-Path $root "tools/pcm.exe"
$usePcm = (-not $NoPcm) -and (Test-Path $pcm)
if ($usePcm) {
    Write-Host "Energia: Intel PCM ($pcm)"
} else {
    Write-Host "AVISO: sem PCM; serao coletados apenas tempos (sem energia)."
}

function Get-NoisyPath([string]$image) {
    $base = $image.Substring(0, $image.LastIndexOf('.'))
    return "${base}_ruido.ppm"
}

function Invoke-Run([string]$exe, [int]$threads, [int]$radius, [string]$image) {
    if ($usePcm) {
        & $pcm -- $exe $threads $radius $image $Sigma 2>&1
    } else {
        & $exe $threads $radius $image $Sigma 2>&1
    }
}

foreach ($image in $Images) {
    if (-not (Test-Path $image)) { throw "imagem nao encontrada: $image" }

    $noisy = Get-NoisyPath $image
    if (-not (Test-Path $noisy)) {
        Write-Host "Gerando imagem ruidosa: $noisy"
        & $rui $image $noisy $Sigma 20260920 | Out-Null
    }

    $base = [System.IO.Path]::GetFileNameWithoutExtension($image)
    $outdir = Join-Path $root "dados/resultados/$base"
    New-Item -ItemType Directory -Force -Path $outdir | Out-Null
    Write-Host ""
    Write-Host "Imagem: $image"

    foreach ($radius in $Radii) {
        $configs = @(, @("seq", $seq, 1))
        foreach ($t in $Threads) { $configs += , @("$t", $omp, $t) }

        foreach ($cfg in $configs) {
            $label = $cfg[0]; $exe = $cfg[1]; $th = $cfg[2]
            $log = Join-Path $outdir "${base}_K${radius}_${label}.txt"
            Set-Content -Path $log -Value "# protocolo: imagem=$image raio=$radius config=$label sigma=$Sigma reps=$Reps" -Encoding utf8
            Write-Host ("  raio={0}  cfg={1}" -f $radius, $label)

            Write-Host "    aquecimento (descartado)"
            [void](Invoke-Run $exe $th $radius $image)

            for ($r = 1; $r -le $Reps; $r++) {
                Write-Host ("    rep {0}/{1}" -f $r, $Reps)
                if ($usePcm) {
                    Add-Content -Path $log -Value "`n.\pcm.exe -- $exe $th $radius $image $Sigma" -Encoding utf8
                } else {
                    Add-Content -Path $log -Value "`n$exe $th $radius $image $Sigma" -Encoding utf8
                }
                (Invoke-Run $exe $th $radius $image) | Out-File -Append -Encoding utf8 $log
            }
        }
    }
}

Write-Host ""
Write-Host "Logs em dados/resultados/. Consolide com: python scripts/analisar.py"
