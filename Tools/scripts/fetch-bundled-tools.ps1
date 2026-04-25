param(
    [string]$HostId = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "../..")
$BundledRoot = Join-Path $RepoRoot "Tools/ThirdParty/BuildTools"
$DownloadDir = Join-Path $BundledRoot "downloads"

$CMakeVersion = "4.3.2"
$NinjaVersion = "1.13.2"

function Get-DefaultHostId {
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture
    if ([System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)) {
        if ($arch -eq [System.Runtime.InteropServices.Architecture]::Arm64) { return "windows-arm64" }
        if ($arch -eq [System.Runtime.InteropServices.Architecture]::X64) { return "windows-x86_64" }
    }
    if ([System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Linux)) {
        if ($arch -eq [System.Runtime.InteropServices.Architecture]::Arm64) { return "linux-aarch64" }
        if ($arch -eq [System.Runtime.InteropServices.Architecture]::X64) { return "linux-x86_64" }
    }
    throw "Unsupported host architecture: $arch"
}

function Get-AssetUrl {
    param([string]$Tool, [string]$Host)
    switch ("${Tool}:${Host}") {
        "cmake:windows-x86_64" { return "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip" }
        "cmake:windows-arm64" { return "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-arm64.zip" }
        "cmake:linux-x86_64" { return "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-linux-x86_64.tar.gz" }
        "ninja:windows-x86_64" { return "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-win.zip" }
        "ninja:windows-arm64" { return "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-winarm64.zip" }
        "ninja:linux-x86_64" { return "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-linux.zip" }
        "ninja:linux-aarch64" { return "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-linux-aarch64.zip" }
        default { throw "Unsupported asset: $Tool/$Host" }
    }
}

function Download-Asset {
    param([string]$Url)
    New-Item -ItemType Directory -Force -Path $DownloadDir | Out-Null
    $Output = Join-Path $DownloadDir (Split-Path -Leaf $Url)
    if (Test-Path $Output) {
        Write-Host "using cached $Output"
        return $Output
    }
    Write-Host "downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Output
    return $Output
}

function Expand-SingleRootArchive {
    param([string]$Archive, [string]$Destination)
    $Temp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString())
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Destination
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    New-Item -ItemType Directory -Force -Path $Temp | Out-Null

    if ($Archive.EndsWith(".zip")) {
        Expand-Archive -Path $Archive -DestinationPath $Temp -Force
    } elseif ($Archive.EndsWith(".tar.gz")) {
        tar -xzf $Archive -C $Temp
    } else {
        throw "Unsupported archive: $Archive"
    }

    $Children = Get-ChildItem -Force $Temp
    if ($Children.Count -eq 1 -and $Children[0].PSIsContainer) {
        Copy-Item -Recurse -Force (Join-Path $Children[0].FullName "*") $Destination
    } else {
        Copy-Item -Recurse -Force (Join-Path $Temp "*") $Destination
    }
    Remove-Item -Recurse -Force $Temp
}

function Fetch-CMake {
    param([string]$Host)
    if ($Host -eq "linux-aarch64") {
        throw "Use fetch-bundled-tools.sh to extract the CMake linux-aarch64 shell installer."
    }
    $Url = Get-AssetUrl "cmake" $Host
    $Archive = Download-Asset $Url
    $Destination = Join-Path $BundledRoot "cmake/$CMakeVersion/$Host"
    Expand-SingleRootArchive $Archive $Destination
}

function Fetch-Ninja {
    param([string]$Host)
    $Url = Get-AssetUrl "ninja" $Host
    $Archive = Download-Asset $Url
    $License = Download-Asset "https://raw.githubusercontent.com/ninja-build/ninja/v$NinjaVersion/COPYING"
    $Destination = Join-Path $BundledRoot "ninja/$NinjaVersion/$Host"
    Expand-SingleRootArchive $Archive $Destination
    Copy-Item -Force $License (Join-Path $Destination "COPYING")
}

if ([string]::IsNullOrWhiteSpace($HostId)) {
    $HostId = Get-DefaultHostId
}

Write-Host "fetching bundled tools for $HostId"
Fetch-CMake $HostId
Fetch-Ninja $HostId
Write-Host "bundled tools are ready under $BundledRoot"
