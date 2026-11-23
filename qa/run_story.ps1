# Shared-story research run: two copies on one PC, story probes on.
# Timeline (ms after each copy's world load; the client starts 12 s after the server):
#   server: session at 5 s, talks to Xardas at 20 s, teleports to the city market at 150 s
#   client: session at 15 s, talks to Xardas at 75 s, teleports to the city market at 100 s
# Then qa\analyze_story.py reads both logs and the two GothicCoopStory.json files.
param([Parameter(Mandatory = $true)][string]$Root, [int]$Seconds = 210, [switch]$ClientPausesForTalk,
      [string]$Dll = (Join-Path $PSScriptRoot "..\Bin\GothicCoop.dll"))
$ErrorActionPreference = "Stop"
$market = @(9682, 400, -1628)   # waypoint NW_CITY_MERCHANT_TEMPLE_PLACE_01, ~320 m from the tower

function Write-Config($dir, $role, $nick, $startAfter, $talks, $teleports, $pauseDuringTalk = $false) {
    $cfg = [ordered]@{
        server = "127.0.0.1"; port = 1234; nickname = $nick; upnp = $false
        playersDamageMultipler = 100; npcsDamageMultipler = 100
        toggleGameLogKey = "KEY_P"; toggleGameStatsKey = "KEY_O"
        startServerKey = "KEY_F1"; startConnectionKey = "KEY_F2"
        reinitPlayersKey = "KEY_F3"; revivePlayerKey = "KEY_F4"
        autotest = [ordered]@{ role = $role; allowMultiInstance = $true; startAfterMs = $startAfter; reportEveryMs = 1000
            storyEveryMs = 5000; nearRadius = 3000; talks = $talks; teleports = $teleports; pauseDuringTalk = $pauseDuringTalk }
    }
    [IO.File]::WriteAllText("$dir\System\GothicCoopConfig.json", ($cfg | ConvertTo-Json -Depth 6))
}

if (Test-Path "$Root\running_pids.txt") {
    foreach ($id in Get-Content "$Root\running_pids.txt") {
        $old = Get-Process -Id ([int]$id) -ErrorAction SilentlyContinue
        if ($old -and $old.Path -like "$Root\*") { Stop-Process -Id $old.Id -Force; $old.WaitForExit(10000) | Out-Null }
    }
}
foreach ($n in 1, 2) {
    $sys = "$Root\G2A-$n\System"
    Copy-Item $Dll "$sys\Autorun\GothicCoop.dll" -Force
    Remove-Item "$sys\GothicCoopTest.log", "$sys\GothicCoopStory.json", "$sys\GothicCoopNpcs.json" -ErrorAction SilentlyContinue
}
Write-Config "$Root\G2A-1" "server" "Host" 5000 @(, @(20000, "NONE_100_XARDAS", 3)) @(, @(150000, $market[0], $market[1], $market[2], 0, 1))
Write-Config "$Root\G2A-2" "client" "Friend" 15000 @(, @(75000, "NONE_100_XARDAS", 3)) @(, @(100000, $market[0], $market[1], $market[2], 0, 1)) $ClientPausesForTalk.IsPresent

$procs = @()
$procs += Start-Process "$Root\G2A-1\System\Gothic2.exe" -ArgumentList "-nomenu" -WorkingDirectory "$Root\G2A-1\System" -PassThru
Start-Sleep 12
$procs += Start-Process "$Root\G2A-2\System\Gothic2.exe" -ArgumentList "-nomenu" -WorkingDirectory "$Root\G2A-2\System" -PassThru
$procs | ForEach-Object { $_.Id } | Set-Content "$Root\running_pids.txt"

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not ($procs | Where-Object { $_.HasExited })) { Start-Sleep 2 }
$i = 0
foreach ($p in $procs) {
    $i++; $p.Refresh()
    if ($p.HasExited) { "copy $i exited early, code $($p.ExitCode)" }
    else { Stop-Process -Id $p.Id -Force; $p.WaitForExit(10000) | Out-Null }
}
python -I "$PSScriptRoot\analyze_story.py" "$Root\G2A-1\System" "$Root\G2A-2\System"
