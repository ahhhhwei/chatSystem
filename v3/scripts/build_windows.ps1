[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtDir,

    [Parameter(Mandatory = $true)]
    [string]$VcpkgRoot,

    [string]$HttpUrl = "http://127.0.0.1:9000",
    [string]$WebSocketUrl = "ws://127.0.0.1:9001",
    [string]$Triplet = "x64-windows-static-md"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command,
        [Parameter(Mandatory = $true)]
        [string]$FailureMessage
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

function Assert-UrlScheme {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value,
        [Parameter(Mandatory = $true)]
        [string[]]$AllowedSchemes,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    [Uri]$parsed = $null
    if (-not [Uri]::TryCreate($Value, [UriKind]::Absolute, [ref]$parsed) -or
        $AllowedSchemes -notcontains $parsed.Scheme) {
        throw "$Name 地址无效：$Value"
    }
}

Assert-UrlScheme -Value $HttpUrl -AllowedSchemes @("http", "https") -Name "HTTP"
Assert-UrlScheme -Value $WebSocketUrl -AllowedSchemes @("ws", "wss") -Name "WebSocket"

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$QtDir = (Resolve-Path $QtDir).Path
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$WinDeployQt = Join-Path $QtDir "bin\windeployqt.exe"
$BuildDir = Join-Path $ProjectRoot "build-windows"
$DistRoot = Join-Path $ProjectRoot "dist"
$PackageDir = Join-Path $DistRoot "Chat-Windows"
$ArchivePath = Join-Path $DistRoot "Chat-Windows.zip"

if (-not (Test-Path $VcpkgExe -PathType Leaf)) {
    throw "没有找到 vcpkg.exe：$VcpkgExe"
}
if (-not (Test-Path $VcpkgToolchain -PathType Leaf)) {
    throw "没有找到 vcpkg CMake 工具链：$VcpkgToolchain"
}
if (-not (Test-Path $WinDeployQt -PathType Leaf)) {
    throw "QtDir 不正确，没有找到：$WinDeployQt"
}

New-Item -ItemType Directory -Force -Path $BuildDir, $DistRoot | Out-Null

Invoke-Checked -FailureMessage "安装 Protobuf 失败" -Command {
    & $VcpkgExe install "protobuf:$Triplet"
}

Invoke-Checked -FailureMessage "配置 Windows 客户端失败" -Command {
    & cmake `
        -S $ProjectRoot `
        -B $BuildDir `
        -G "Visual Studio 17 2022" `
        -A x64 `
        "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain" `
        "-DVCPKG_TARGET_TRIPLET=$Triplet" `
        "-DCMAKE_PREFIX_PATH=$QtDir" `
        -DBUILD_TESTING=OFF
}

Invoke-Checked -FailureMessage "编译 Windows 客户端失败" -Command {
    & cmake --build $BuildDir --config Release --parallel
}

if (Test-Path $PackageDir) {
    Remove-Item -Recurse -Force $PackageDir
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

Invoke-Checked -FailureMessage "安装 Windows 客户端失败" -Command {
    & cmake --install $BuildDir --config Release --prefix $PackageDir
}

$Executable = Join-Path $PackageDir "Chat.exe"
if (-not (Test-Path $Executable -PathType Leaf)) {
    throw "没有生成 Chat.exe：$Executable"
}

Invoke-Checked -FailureMessage "收集 Qt 运行库失败" -Command {
    & $WinDeployQt `
        --release `
        --no-translations `
        --compiler-runtime `
        $Executable
}

$Utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
$ServerConfig = @"
[server]
http_url=$HttpUrl
ws_url=$WebSocketUrl
"@
[IO.File]::WriteAllText(
    (Join-Path $PackageDir "server.ini"),
    $ServerConfig,
    $Utf8WithoutBom)

$PackageGuide = @"
聊天 Windows 测试版

1. 双击 Chat.exe 启动。
2. 当前 HTTP 地址：$HttpUrl
3. 当前实时消息地址：$WebSocketUrl
4. 如果无法连接，请先确认服务器在线、地址可访问并且防火墙已放行。
5. server.ini 必须与 Chat.exe 放在同一目录，可以修改其中的服务器地址。
"@
[IO.File]::WriteAllText(
    (Join-Path $PackageDir "使用说明.txt"),
    $PackageGuide,
    $Utf8WithoutBom)

$PlatformPlugin = Join-Path $PackageDir "platforms\qwindows.dll"
if (-not (Test-Path $PlatformPlugin -PathType Leaf)) {
    throw "Windows 平台插件缺失：$PlatformPlugin"
}

if (Test-Path $ArchivePath) {
    Remove-Item -Force $ArchivePath
}
Compress-Archive -Path (Join-Path $PackageDir "*") -DestinationPath $ArchivePath

Write-Host "Windows 免安装包已经生成："
Write-Host $ArchivePath
