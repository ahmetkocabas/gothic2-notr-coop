# Build, then N normal pair runs and 1 negative control (client on the wrong port must FAIL).
# -Root: folder with the G2A-1 / G2A-2 test copies. -MSBuild: path to MSBuild.exe (VS 2022 Build Tools).
param([Parameter(Mandatory = $true)][string]$Root, [int]$Runs = 3,
      [string]$MSBuild = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe")
$repo = Split-Path $PSScriptRoot
Push-Location $repo
& $MSBuild GothicCoop.vcxproj "/p:Configuration=G2A MT Release" /p:Platform=Win32 /p:PlatformToolset=v143 /p:PreBuildEventUseInBuild=false "/p:SolutionDir=$repo\" /m /v:minimal /nologo 2>&1 | Select-String -Pattern " error |->|LNK"
Pop-Location
$summary = @()
for ($r = 1; $r -le $Runs; $r++) {
    "===== run $r"
    $out = & "$PSScriptRoot\run_pair.ps1" -Root $Root -Seconds 150
    $out
    $summary += "run ${r}: " + ($out | Select-String "checks passed" | Select-Object -Last 1)
    foreach ($n in 1, 2) {
        $log = "$Root\G2A-$n\System\GothicCoopTest.log"
        $l = Get-Content $log
        "  G2A-$n slow_frames=$(($l | Select-String slow_frame).Count) stuck=$(($l | Select-String main_thread_stuck).Count) exit_process=$(($l | Select-String exit_process).Count)"
        Copy-Item $log "$Root\suite_run${r}_G2A-$n.log" -Force
    }
}
"===== negative control (client port 1299)"
$out = & "$PSScriptRoot\run_pair.ps1" -Root $Root -Seconds 90 -ClientPort 1299
$out
$summary += "negative: " + ($out | Select-String "checks passed" | Select-Object -Last 1) + " (must NOT be all passed)"
"===== SUMMARY"
$summary
