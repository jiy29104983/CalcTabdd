# Execute the test-only probe in isolated official host distributions.
# Downloads are pinned by SHA256; nothing is installed or copied to a user host.
$ErrorActionPreference = 'Stop'
$releases = @(
    @{ Version = '3.8.3'; Sha256 = 'a24af4cd6f8b97fcc7313fd861386387903d452a20c8c539c251a3eef5287ff7' },
    @{ Version = '3.9.0'; Sha256 = '5e19f287a9a01035f8e7171462f6298949e3625432b38db3b6f91a80b578f40a' }
)
$root = Join-Path $PWD 'build/released-hosts'
New-Item -ItemType Directory -Force $root | Out-Null
foreach ($release in $releases) {
    $version = $release.Version
    $name = "Notepad--v$version-win10-portable"
    $zip = Join-Path $root "$name.zip"
    Invoke-WebRequest "https://github.com/cxasm/notepad--/releases/download/notepad-v$version/$name.zip" -OutFile $zip
    if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $release.Sha256) {
        throw "Host $version archive checksum mismatch"
    }
    $destination = Join-Path $root $version
    Expand-Archive $zip -DestinationPath $destination -Force
    $directory = Join-Path $destination $name
    # Only the two test subject plugins belong in this generated fixture.
    Get-ChildItem "$directory/plugin" -Filter '*.dll' | Remove-Item -Force
    Copy-Item 'build/windows/plugin/calctabdd.dll' "$directory/plugin/calctabdd.dll"
    Copy-Item 'build/windows/host-probe/released_host_probe.dll' "$directory/plugin/aa_calctabdd_probe.dll"
    $output = Join-Path $root "result-$version.json"
    $env:CALCTABDD_HOST_PROBE_OUTPUT = $output
    # The complete host uses native Windows APIs; only isolated Qt tests use offscreen.
    $env:QT_QPA_PLATFORM = 'windows'
    $process = Start-Process -FilePath "$directory/Notepad--.exe" -WorkingDirectory $directory -PassThru `
        -RedirectStandardOutput "$root/stdout-$version.log" -RedirectStandardError "$root/stderr-$version.log"
    if (-not $process.WaitForExit(45000)) {
        Stop-Process -Id $process.Id -Force
        throw "Host $version probe timed out; see diagnostics"
    }
    if (-not (Test-Path $output)) {
        foreach ($log in @("$output.progress.log", "$root/stdout-$version.log", "$root/stderr-$version.log")) {
            if (Test-Path $log) { Write-Host "Diagnostics: $log"; Get-Content $log }
        }
        Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000; StartTime=(Get-Date).AddMinutes(-3)} -ErrorAction SilentlyContinue |
            Select-Object -First 3 -ExpandProperty Message | Write-Host
        throw "Host $version did not produce a probe result; exit=$($process.ExitCode)"
    }
    $result = Get-Content -Raw $output | ConvertFrom-Json
    $result | ConvertTo-Json -Depth 6
    if ($process.ExitCode -ne 0 -or -not $result.passed) { throw "Host $version probe failed" }
}
Remove-Item Env:CALCTABDD_HOST_PROBE_OUTPUT
Remove-Item Env:QT_QPA_PLATFORM
