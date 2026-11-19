# Two-copy co-op QA run on one PC. Starts G2A-1 as server and G2A-2 as client (both -nomenu),
# lets the autotest bot in GothicCoop.dll drive them, stops only the two processes it started,
# then checks the logs with check_pair.py.
# -Root: folder that holds two copies of the game, G2A-1 (server) and G2A-2 (client), each with
#        Union and System\Autorun\ set up (see docs/TESTING.md).
param([Parameter(Mandatory = $true)][string]$Root, [int]$Seconds = 120,
      [string]$Dll = (Join-Path $PSScriptRoot "..\Bin\GothicCoop.dll"), [int]$ClientPort = 1234)
$ErrorActionPreference = "Stop"
$root = $Root
$qa = $PSScriptRoot

function Write-Config($dir, $role, $nick, $startAfter, $teleports, $port = 1234) {
    $cfg = [ordered]@{
        server = "127.0.0.1"; port = $port; nickname = $nick
        playersDamageMultipler = 100; npcsDamageMultipler = 100
        toggleGameLogKey = "KEY_P"; toggleGameStatsKey = "KEY_O"
        startServerKey = "KEY_F1"; startConnectionKey = "KEY_F2"
        reinitPlayersKey = "KEY_F3"; revivePlayerKey = "KEY_F4"
        autotest = [ordered]@{ role = $role; allowMultiInstance = $true; startAfterMs = $startAfter; reportEveryMs = 500; teleports = $teleports }
    }
    [IO.File]::WriteAllText("$dir\System\GothicCoopConfig.json", ($cfg | ConvertTo-Json -Depth 5))
}

# Leftovers from an earlier run: stop only our own copies (by saved PID and path).
if (Test-Path "$root\running_pids.txt") {
    foreach ($id in Get-Content "$root\running_pids.txt") {
        $old = Get-Process -Id ([int]$id) -ErrorAction SilentlyContinue
        if ($old -and $old.Path -like "$root\*") { Stop-Process -Id $old.Id -Force; $old.WaitForExit(10000) | Out-Null }
    }
}

foreach ($n in 1, 2) {
    $sys = "$root\G2A-$n\System"
    Copy-Item $Dll "$sys\Autorun\GothicCoop.dll" -Force
    Remove-Item "$sys\GothicCoopTest.log", "$sys\GothicCoopLog.log" -ErrorAction SilentlyContinue
}
# Host moves at fixed times; the client must see each move.
Write-Config "$root\G2A-1" "server" "Host" 5000 @(@(45000, 600, 0, 0), @(65000, 0, 0, 600), @(85000, -600, 0, -600))
# The client also goes far away (past 1.5 x BROADCAST_DISTANCE = 6750) and back: that makes
# its synced NPCs "destroyed", which exercises the SyncNpcs erase path.
Write-Config "$root\G2A-2" "client" "Friend" 15000 @(@(55000, -500, 0, 0), @(75000, 500, 0, 0), @(95000, 0, 7500, 0, 1), @(110000, 0, 0, 0)) $ClientPort

$procs = @()
$procs += Start-Process "$root\G2A-1\System\Gothic2.exe" -ArgumentList "-nomenu" -WorkingDirectory "$root\G2A-1\System" -PassThru
Start-Sleep 12
$procs += Start-Process "$root\G2A-2\System\Gothic2.exe" -ArgumentList "-nomenu" -WorkingDirectory "$root\G2A-2\System" -PassThru
$procs | ForEach-Object { $_.Id } | Set-Content "$root\running_pids.txt"

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline -and -not ($procs | Where-Object { $_.HasExited })) { Start-Sleep 2 }

$i = 0
$early = @()
foreach ($p in $procs) {
    $i++
    $p.Refresh()
    if ($p.HasExited) { "copy $i exited early, code $($p.ExitCode)"; $early += "$i" }
    else { "copy $i handles: $($p.HandleCount)"; Stop-Process -Id $p.Id -Force; $p.WaitForExit(10000) | Out-Null }
}
python -I "$qa\check_pair.py" "$root\G2A-1\System\GothicCoopTest.log" "$root\G2A-2\System\GothicCoopTest.log" ($early -join ",")
