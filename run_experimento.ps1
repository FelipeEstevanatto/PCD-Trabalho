# Protocolo intercalado: seq e omp no mesmo ciclo, sem pausa entre configs.
# Assim o CPU nao esfria entre o baseline sequencial e as configs paralelas.
#
# Uso:
#   .\run_experimento.ps1
#   .\run_experimento.ps1 -Reps 10 -Radius 9 -Image imagens/montanha_4k.ppm -Sigma 25
#   .\run_experimento.ps1 -Quick   # 1 rep, soh para testar

param(
    [int]$Reps = 10,
    [int]$Radius = 9,
    [string]$Image = "imagens/montanha_4k.ppm",
    [double]$Sigma = 25,
    [switch]$Quick
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if ($Quick) { $Reps = 1 }

$seqExe = Join-Path $PSScriptRoot "bilateral_seq.exe"
$ompExe = Join-Path $PSScriptRoot "bilateral_omp.exe"

if (-not (Test-Path $seqExe) -or -not (Test-Path $ompExe)) {
    Write-Host "Compilando..."
    gcc -O3 -o bilateral_seq.exe bilateral_seq.c bilateral_common.c -lm
    if ($LASTEXITCODE -ne 0) { throw "falha ao compilar bilateral_seq" }
    gcc -O3 -fopenmp -o bilateral_omp.exe bilateral_omp.c bilateral_common.c -lm
    if ($LASTEXITCODE -ne 0) { throw "falha ao compilar bilateral_omp" }
}

function Get-TempoS {
    param([string]$Exe, [string[]]$ArgList)
    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    try {
        $p = Start-Process -FilePath $Exe -ArgumentList $ArgList `
            -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $outFile `
            -RedirectStandardError $errFile
        if ($p.ExitCode -ne 0) {
            $errText = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
            throw "falhou ($($p.ExitCode)): $Exe $($ArgList -join ' ')`n$errText"
        }
        $line = Select-String -Path $outFile -Pattern '^tempo_s=([0-9.]+)' | Select-Object -First 1
        if (-not $line) {
            throw "nao achei tempo_s= na saida de $Exe"
        }
        return [double]$line.Matches[0].Groups[1].Value
    }
    finally {
        Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    }
}

$threadCounts = @(1, 2, 4, 8, 16, 32)
$commonArgs = @("$Radius", $Image, "$Sigma")

Write-Host "Protocolo intercalado: aquecimento + $Reps reps (seq,1,2,4,8,16,32 por volta)"
Write-Host "Imagem: $Image  raio=$Radius  sigma=$Sigma"
Write-Host "Sem pausa entre configs (CPU nao esfria de proposito)."
Write-Host ""

Write-Host "Aquecimento (descartado)..."
[void](Get-TempoS $seqExe ($commonArgs + @("--save")))
[void](Get-TempoS $ompExe (@("8") + $commonArgs + @("--save")))

$times = @{}
$times["seq"] = New-Object double[] $Reps
foreach ($t in $threadCounts) {
    $times["$t"] = New-Object double[] $Reps
}

$csvPath = Join-Path $PSScriptRoot "imagens\tempos_10reps.csv"
$csv = New-Object System.Collections.Generic.List[string]
$csv.Add("rep,config,threads,tempo_s")

for ($r = 0; $r -lt $Reps; $r++) {
    Write-Host ("  rep {0,2}/{1}  seq ..." -f ($r + 1), $Reps)
    $ts = Get-TempoS $seqExe $commonArgs
    $times["seq"][$r] = $ts
    $csv.Add(("{0},seq,0,{1:F6}" -f ($r + 1), $ts))

    foreach ($t in $threadCounts) {
        Write-Host ("  rep {0,2}/{1}  {2} threads ..." -f ($r + 1), $Reps, $t)
        $tp = Get-TempoS $ompExe (@("$t") + $commonArgs)
        $times["$t"][$r] = $tp
        $csv.Add(("{0},omp,{1},{2:F6}" -f ($r + 1), $t, $tp))
    }
}

# Salva saidas finais (seq + omp 8) para validacao visual / erro
[void](Get-TempoS $seqExe ($commonArgs + @("--save")))
[void](Get-TempoS $ompExe (@("8") + $commonArgs + @("--save")))

Set-Content -Path $csvPath -Value $csv -Encoding utf8

function Mean([double[]]$v) {
    ($v | Measure-Object -Average).Average
}
function Stdev([double[]]$v) {
    if ($v.Length -lt 2) { return 0.0 }
    $m = Mean $v
    $s = 0.0
    foreach ($x in $v) { $s += ($x - $m) * ($x - $m) }
    return [math]::Sqrt($s / ($v.Length - 1))
}

$tSeq = Mean $times["seq"]
Write-Host ""
Write-Host "Protocolo: 1 aquecimento + $Reps repeticoes (ordem seq,1,2,4,8,16,32 em cada volta)"
Write-Host "Binarios: bilateral_seq.exe + bilateral_omp.exe (intercalados, sem sleep)"
Write-Host ""
Write-Host ("{0,-10} {1,12} {2,12} {3,10} {4,12} {5,14}" -f `
    "threads", "media(s)", "desvio(s)", "speedup", "eficiencia", "proxy E (s*th)")

$sdSeq = Stdev $times["seq"]
Write-Host ("{0,-10} {1,12:F4} {2,12:F4} {3,10} {4,12} {5,14}" -f `
    "seq", $tSeq, $sdSeq, "-", "-", "-")

foreach ($t in $threadCounts) {
    $m = Mean $times["$t"]
    $sd = Stdev $times["$t"]
    $sp = $tSeq / $m
    $eff = $sp / $t
    $proxy = $m * $t
    Write-Host ("{0,-10} {1,12:F4} {2,12:F4} {3,10:F3} {4,12:F3} {5,14:F3}" -f `
        "$t", $m, $sd, $sp, $eff, $proxy)
}

Write-Host ""
Write-Host "CSV: $csvPath"
Write-Host "Saidas: imagens/saida_sequencial.ppm  imagens/saida_paralela.ppm"
Write-Host "(proxy E = tempo * threads; RAPL ainda nao medido)"
