[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$HostName = "com.onibyts.quickswap"
$ExtensionId = "quick-swap-tools@onibyts.com"
$InstallRoot = $PSScriptRoot
$ExpectedInstallRoot = Join-Path $env:LOCALAPPDATA "Programs\Quick Swap Tools"
$ExpectedHost = Join-Path $InstallRoot "quick-swap-tools.exe"
$ExpectedConfig = Join-Path $InstallRoot "quick-swap-config.exe"
$ManifestPath = Join-Path $InstallRoot "$HostName.json"
$OwnerPath = Join-Path $InstallRoot "install-owner.json"
$RegistryPath = "HKCU:\Software\Mozilla\NativeMessagingHosts\$HostName"
$SettingsPath = "HKCU:\Software\OniByts\Quick Swap Tools"
$StartMenuDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
$ShortcutPath = Join-Path $StartMenuDirectory "Quick Swap Tools.lnk"

function Refuse([string]$Message) {
    throw "Quick Swap Tools: Refusing to uninstall: $Message"
}

function Assert-SafeLocalPath([string]$Path, [string]$Description) {
    if (-not [IO.Path]::IsPathRooted($Path)) {
        Refuse "$Description is not an absolute local path"
    }
    $PathUri = New-Object Uri($Path)
    if ($PathUri.IsUnc) {
        Refuse "$Description is not an absolute local path"
    }
    $FullPath = [IO.Path]::GetFullPath($Path)
    $Root = [IO.Path]::GetPathRoot($FullPath)
    $Drive = New-Object IO.DriveInfo($Root)
    if ($Drive.DriveType -ne [IO.DriveType]::Fixed) {
        Refuse "$Description is not on a local fixed drive"
    }
    $Current = $FullPath
    while (-not [string]::IsNullOrEmpty($Current)) {
        if (Test-Path -LiteralPath $Current) {
            $Item = Get-Item -LiteralPath $Current -Force
            if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                Refuse "$Description traverses a reparse point"
            }
        }
        $Parent = Split-Path -LiteralPath $Current -Parent
        if ($Parent -eq $Current) { break }
        $Current = $Parent
    }
}

if (-not [string]::Equals(
        [IO.Path]::GetFullPath($InstallRoot),
        [IO.Path]::GetFullPath($ExpectedInstallRoot),
        [StringComparison]::OrdinalIgnoreCase)) {
    Refuse "the script is not running from the expected installation directory"
}
Assert-SafeLocalPath $InstallRoot "the installation directory"
Assert-SafeLocalPath $StartMenuDirectory "the Start Menu directory"

if (-not (Test-Path -LiteralPath $OwnerPath -PathType Leaf)) {
    Refuse "the installation ownership marker is missing"
}
$Owner = Get-Content -LiteralPath $OwnerPath -Raw | ConvertFrom-Json
if ($Owner.schema -ne 1 -or $Owner.product -ne "Quick Swap Tools" -or $Owner.host -ne $HostName) {
    Refuse "the installation ownership marker does not match this product"
}
$OwnedFiles = @($Owner.files)
$FixedFiles = @(
    "quick-swap-tools.exe", "quick-swap-config.exe", "uninstall.ps1",
    "LICENSE", "THIRD_PARTY_NOTICES.md", "$HostName.json", "install-owner.json"
)
if ($OwnedFiles.Count -ne 8) {
    Refuse "the ownership file list has an unexpected size"
}
foreach ($Name in $FixedFiles) {
    if ($OwnedFiles -notcontains $Name) {
        Refuse "the ownership file list is missing $Name"
    }
}
$OwnedXpi = @($OwnedFiles | Where-Object { $_ -like "Quick-Swap-Tools-*-firefox.xpi" })
if ($OwnedXpi.Count -ne 1) {
    Refuse "the ownership file list does not identify exactly one Firefox extension"
}
$ActualItems = @(Get-ChildItem -LiteralPath $InstallRoot -Force)
if (@($ActualItems | Where-Object { -not $_.PSIsContainer }).Count -ne $OwnedFiles.Count -or
    @($ActualItems | Where-Object { $_.PSIsContainer }).Count -ne 0) {
    Refuse "the installation contains unknown files or directories"
}
foreach ($Item in $ActualItems) {
    if ($OwnedFiles -notcontains $Item.Name) {
        Refuse "the installation contains an unknown file: $($Item.Name)"
    }
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    Refuse "the native-host manifest is missing"
}
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
# Verify manifest.path and every ownership-defining field before deleting registration.
if ($Manifest.name -ne $HostName -or
    $Manifest.path -ne $ExpectedHost -or
    $Manifest.type -ne "stdio" -or
    @($Manifest.allowed_extensions).Count -ne 1 -or
    @($Manifest.allowed_extensions)[0] -ne $ExtensionId) {
    Refuse "the native-host manifest is not owned by this installation"
}

if (Test-Path -LiteralPath $RegistryPath) {
    $RegisteredManifest = (Get-Item -LiteralPath $RegistryPath).GetValue("")
    if ($RegisteredManifest -ne $ManifestPath) {
        Refuse "the Firefox registry entry points somewhere else"
    }
}

if (Test-Path -LiteralPath $ShortcutPath) {
    $Shell = New-Object -ComObject WScript.Shell
    $Shortcut = $Shell.CreateShortcut($ShortcutPath)
    if ($Shortcut.TargetPath -ne $ExpectedConfig) {
        Refuse "the Start Menu shortcut points somewhere else"
    }
}

$RunningHost = Get-CimInstance Win32_Process -Filter "Name = 'quick-swap-tools.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.ExecutablePath -eq $ExpectedHost }
if ($null -ne $RunningHost) {
    Refuse "close Firefox before uninstalling so the native host can exit cleanly"
}

if (Test-Path -LiteralPath $RegistryPath) {
    Remove-Item -LiteralPath $RegistryPath -Force
}
if (Test-Path -LiteralPath $ShortcutPath) {
    Remove-Item -LiteralPath $ShortcutPath -Force
}
if (Test-Path -LiteralPath $SettingsPath) {
    foreach ($Name in @(
        "AuctionModifiers", "AuctionVirtualKey",
        "GiveawayModifiers", "GiveawayVirtualKey"
    )) {
        if ($null -ne (Get-ItemProperty -LiteralPath $SettingsPath -Name $Name -ErrorAction SilentlyContinue)) {
            Remove-ItemProperty -LiteralPath $SettingsPath -Name $Name -Force
        }
    }
    $SettingsKey = Get-Item -LiteralPath $SettingsPath
    if ($SettingsKey.ValueCount -eq 0 -and $SettingsKey.SubKeyCount -eq 0) {
        Remove-Item -LiteralPath $SettingsPath -Force
    }
}

Remove-Item -LiteralPath $InstallRoot -Recurse -Force
Write-Host "Quick Swap Tools was removed. Remove the Firefox extension from about:addons if you no longer want it."
