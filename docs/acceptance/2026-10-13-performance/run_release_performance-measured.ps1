$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location -LiteralPath $taskRoot
$taskBuild = Join-Path $taskRoot 'build\Desktop_Qt_6_11_1_MinGW_64_bit_Release'
$taskOutput = Join-Path $taskRoot 'docs\acceptance\2026-10-13-performance'
$taskFixtures = Join-Path $taskRoot 'build\performance-fixtures-20261013'
$env:PATH = 'C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QUICK_BACKEND = $null
$env:QSG_RHI_BACKEND = 'd3d11'
$env:QSG_INFO = '1'
$env:QTEST_FUNCTION_TIMEOUT = '600000'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskTracked = @('CMakeLists.txt','tests\acceptance\tst_qml_acceptance.cpp',
    'tests\acceptance\run_release_performance.ps1') | ForEach-Object { Join-Path $taskRoot $_ }
$taskTracked += @(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src'),(Join-Path $taskRoot 'resources') -Recurse -File |
    ForEach-Object FullName)
$taskTracked += Join-Path $taskBuild 'ClipboardHistory.exe'
$taskTracked += Join-Path $taskBuild 'tst_qml_acceptance.exe'
$taskTracked += @(10000,100000) | ForEach-Object { Join-Path $taskFixtures "$_\history.sqlite3" }
$taskBefore = $taskTracked | ForEach-Object {
    [pscustomobject]@{Path=$_; SHA256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}
}
$taskManifest = [ordered]@{StartedUtc=[DateTime]::UtcNow.ToString('o'); Hashes=$taskBefore; Runs=@();
    Graphics='Windows/D3D11; QSG_RHI_DISABLE_DISK_CACHE=1 inside test host';
    ClipboardAccess=$false; RealTrayAccess=$false; Data='QTemporaryDir copies of retained synthetic fixtures'}
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
foreach ($taskRows in @(10000,100000)) {
    foreach ($taskRound in 1..3) {
        $taskName = "native-$taskRows-run$taskRound"
        $taskReport = Join-Path $taskOutput "$taskName.json"
        $taskLog = Join-Path $taskOutput "$taskName.txt"
        if ((Test-Path -LiteralPath $taskReport) -or (Test-Path -LiteralPath $taskLog)) {
            throw "Existing evidence would be overwritten: $taskName"
        }
        $env:CLIPBOARDHISTORY_ACCEPTANCE_PERF_ROWS = "$taskRows"
        $env:CLIPBOARDHISTORY_ACCEPTANCE_PERF_QUICK = if ($taskRound -gt 1) { '1' } else { $null }
        $env:CLIPBOARDHISTORY_ACCEPTANCE_SYNTHETIC_DB = Join-Path $taskFixtures "$taskRows\history.sqlite3"
        $env:CLIPBOARDHISTORY_ACCEPTANCE_REPORT = $taskReport
        Write-Output "Starting $taskName"
        & (Join-Path $taskBuild 'tst_qml_acceptance.exe') optionalPerformance -o "$taskLog,txt"
        $taskExit = $LASTEXITCODE
        $taskManifest.Runs += [ordered]@{Name=$taskName; ExitCode=$taskExit; CompletedUtc=[DateTime]::UtcNow.ToString('o')}
        $taskManifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
        if ($taskExit -ne 0) { Get-Content -LiteralPath $taskLog -Tail 30; throw "Test failed: $taskName ($taskExit)" }
        if (-not (Test-Path -LiteralPath $taskReport)) { throw "Report missing: $taskName" }
        Write-Output "Passed $taskName"
    }
}
$taskManifest.FinishedUtc = [DateTime]::UtcNow.ToString('o')
$taskManifest.HashesUnchanged = @($taskBefore | Where-Object {
    (Get-FileHash -LiteralPath $_.Path -Algorithm SHA256).Hash -ne $_.SHA256
}).Count -eq 0
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
if (-not $taskManifest.HashesUnchanged) { throw 'Source, executable, or synthetic fixture changed during measurements.' }
