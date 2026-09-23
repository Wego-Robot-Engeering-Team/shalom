param(
    [Parameter(Mandatory = $true)][string]$Version,
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT' -or [Environment]::Is64BitOperatingSystem -eq $false) {
    throw '이 스크립트는 Windows x64 release runner에서만 실행합니다.'
}
if ($Version -notmatch '^\d+\.\d+\.\d+([.-][A-Za-z0-9]+)*$') {
    throw '--Version <major.minor.patch>가 필요합니다.'
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$HmiRoot = Join-Path $RepoRoot 'hmi'
$CmakeText = Get-Content (Join-Path $HmiRoot 'CMakeLists.txt') -Raw
$HmiVersion = ([regex]::Match($CmakeText, 'project\(inspection_hmi VERSION ([^ ]+)')).Groups[1].Value
if ($Version -ne $HmiVersion) {
    throw "HMI project version($HmiVersion)과 요청 버전($Version)이 다릅니다."
}
if (-not $AllowDirty -and (git -C $RepoRoot status --porcelain --untracked-files=all)) {
    throw 'Git 작업 트리가 깨끗하지 않습니다. 릴리스에는 -AllowDirty를 사용하지 마십시오.'
}

$DeployTool = Get-Command windeployqt.exe -ErrorAction Stop
$DistRoot = Join-Path $RepoRoot 'dist'
$ReleaseName = "inspection-hmi-$Version-win64"
$StageRoot = Join-Path $DistRoot $ReleaseName
$AppRoot = Join-Path $StageRoot 'inspection-hmi'
$Archive = Join-Path $DistRoot "$ReleaseName.zip"
$BuildRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("shalom-hmi-build-" + [guid]::NewGuid())

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $StageRoot, $Archive, $BuildRoot
New-Item -ItemType Directory -Force $BuildRoot | Out-Null
try {
    cmake -S $HmiRoot -B $BuildRoot -G Ninja -DCMAKE_BUILD_TYPE=Release -DHMI_BUILD_TESTS=OFF
    cmake --build $BuildRoot --parallel
    cmake --install $BuildRoot --prefix $AppRoot

    # HMI는 SDK가 아니다. CMake install의 계약 헤더를 HMI 고객 번들에서 제외한다.
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $AppRoot 'include')
    & $DeployTool.Source --release --no-translations --dir (Join-Path $AppRoot 'bin') (Join-Path $AppRoot 'bin\inspection_hmi.exe')
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt 실패' }

    @(
        'HMI 실행: bin\inspection_hmi.exe',
        'Qt runtime과 plugin은 bin\ 아래에 포함됩니다.',
        'HMI 번들에는 고객 SDK 라이브러리·헤더를 포함하지 않습니다.'
    ) | Set-Content -Encoding utf8 (Join-Path $AppRoot 'README.txt')

    Get-ChildItem -File -Recurse $StageRoot |
        Where-Object { $_.Name -ne 'checksums.txt' } |
        Sort-Object FullName |
        ForEach-Object {
            $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
            "$hash  .\$($_.FullName.Substring($StageRoot.Length + 1))"
        } | Set-Content -Encoding ascii (Join-Path $StageRoot 'checksums.txt')
    Compress-Archive -Path $StageRoot -DestinationPath $Archive -CompressionLevel Optimal
    Write-Output "Built: $Archive"
}
finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $BuildRoot
}
