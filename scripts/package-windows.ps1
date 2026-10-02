$ErrorActionPreference = 'Stop'
$version = [regex]::Match((Get-Content -Raw CMakeLists.txt), 'project\(CalcTabdd VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'Project version missing' }
$sha = $env:GITHUB_SHA
if (-not $sha -or $sha -notmatch '^[0-9a-f]{40}$') { throw 'GITHUB_SHA must identify the tested source' }
$name = "calctabdd-v$version-windows-x64-$($sha.Substring(0, 7))"
$stage = Join-Path $PWD "build/package-$name"
New-Item -ItemType Directory -Force "$stage/plugin", dist | Out-Null
Copy-Item build/windows/plugin/calctabdd.dll "$stage/plugin/calctabdd.dll"
Copy-Item packaging/README.zh-CN.md "$stage/README.md"
Copy-Item LICENSE "$stage/LICENSE"
Copy-Item docs/manual-testing.md "$stage/TESTING.md"
Copy-Item "docs/releases/v$version-preview.md" "$stage/CHANGELOG.md"
$tests = @{}
foreach ($suite in @('engine_tests', 'page_tests', 'plugin_tests')) {
    $text = Get-Content -Raw "build/windows/$suite.txt"
    $match = [regex]::Match($text, 'Totals: (\d+) passed, 0 failed, 0 skipped')
    if (-not $match.Success) { throw "No complete passing result for $suite" }
    $tests[$suite] = @{passed = [int]$match.Groups[1].Value; failed = 0; skipped = 0}
}
@{
    version = $version
    commit = $sha
    architecture = 'x64'
    qt = '5.15.2'
    toolset = 'MSVC v142'
    host_source = '91105f68b74382128f3313ac5af8accdc77de918'
    tests = $tests
    real_host_manual_test = 'not verified'
    workflow_run = $env:GITHUB_RUN_ID
} | ConvertTo-Json -Depth 5 | Set-Content "$stage/BUILD-INFO.json" -Encoding utf8
$archive = "dist/$name.zip"
Compress-Archive -Path "$stage/*" -DestinationPath $archive -CompressionLevel Optimal
$checksum = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$checksum  $name.zip" | Set-Content "$archive.sha256" -Encoding ascii
python scripts/verify-package.py $archive --commit $sha
if ($LASTEXITCODE -ne 0) { throw 'Package verification failed' }
"artifact=$name" >> $env:GITHUB_OUTPUT
"version=$version" >> $env:GITHUB_OUTPUT
