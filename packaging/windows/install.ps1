[CmdletBinding()]
param(
    [switch]$AllowUnsignedDevelopment
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$HostName = "com.onibyts.quickswap"
$ExtensionId = "quick-swap-tools@onibyts.com"
$BundleRoot = $PSScriptRoot
$InstallRoot = Join-Path $env:LOCALAPPDATA "Programs\Quick Swap Tools"
$ManifestPath = Join-Path $InstallRoot "$HostName.json"
$RegistryPath = "HKCU:\Software\Mozilla\NativeMessagingHosts\$HostName"
$RegistryParentPath = "HKCU:\Software\Mozilla\NativeMessagingHosts"
$RegistryParentSubKey = "Software\Mozilla\NativeMessagingHosts"
$RegistryStagingName = ".quick-swap-tools-$([Guid]::NewGuid().ToString('N'))"
$RegistryStagingPath = Join-Path $RegistryParentPath $RegistryStagingName
$StartMenuDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
$ShortcutPath = Join-Path $StartMenuDirectory "Quick Swap Tools.lnk"
$TemporaryShortcutPath = Join-Path $StartMenuDirectory ".quick-swap-tools-$([Guid]::NewGuid().ToString('N')).lnk"
$StagingRoot = "$InstallRoot.install-$([Guid]::NewGuid().ToString('N'))"
$InstallCreated = $false
$RegistryCreated = $false
$RegistryStagingCreated = $false
$ShortcutCreated = $false
$PublishedHashes = @{}
$OwnedInstallFiles = @()
$ExpectedShortcutTarget = Join-Path $InstallRoot "quick-swap-config.exe"
$ExpectedShortcutDescription = "Configure Quick Swap Tools auction and giveaway controls"
$ExpectedSignerThumbprints = @() # Populate only when a project Authenticode identity is approved.

if (-not ("QuickSwapRegistryNative" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class QuickSwapRegistryNative {
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode)]
    public static extern int RegRenameKey(IntPtr key, string oldName, string newName);
}
'@
}

function Fail([string]$Message) {
    throw "Quick Swap Tools: $Message"
}

function Assert-SafeLocalPath([string]$Path, [string]$Description) {
    if (-not [IO.Path]::IsPathRooted($Path)) {
        Fail "$Description must be an absolute path on a local fixed drive"
    }
    $PathUri = New-Object Uri($Path)
    if ($PathUri.IsUnc) {
        Fail "$Description must be an absolute path on a local fixed drive"
    }
    $Root = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path))
    $Drive = New-Object IO.DriveInfo($Root)
    if ($Drive.DriveType -ne [IO.DriveType]::Fixed) {
        Fail "$Description must be on a local fixed drive"
    }
    $Current = [IO.Path]::GetFullPath($Path)
    while (-not [string]::IsNullOrEmpty($Current)) {
        if (Test-Path -LiteralPath $Current) {
            $Item = Get-Item -LiteralPath $Current -Force
            if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                Fail "$Description must not traverse a reparse point"
            }
        }
        $Parent = [IO.Directory]::GetParent($Current)
        if ($null -eq $Parent) { break }
        $Current = $Parent.FullName
    }
}

function Assert-BundleIntegrity {
    $ChecksumFile = Join-Path $BundleRoot "SHA256SUMS"
    if (-not (Test-Path -LiteralPath $ChecksumFile -PathType Leaf)) {
        Fail "SHA256SUMS is missing"
    }
    $Seen = @{}
    foreach ($Line in Get-Content -LiteralPath $ChecksumFile) {
        if ($Line -notmatch '^([0-9a-fA-F]{64})  ([^\\/:*?"<>|]+)$') {
            Fail "SHA256SUMS contains an invalid line"
        }
        $Expected = $Matches[1].ToLowerInvariant()
        $Name = $Matches[2]
        if ($Seen.ContainsKey($Name)) {
            Fail "SHA256SUMS contains a duplicate filename: $Name"
        }
        $Seen[$Name] = $Expected
        $Candidate = Join-Path $BundleRoot $Name
        if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
            Fail "bundle file is missing: $Name"
        }
        $CandidateItem = Get-Item -LiteralPath $Candidate
        if (($CandidateItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Fail "bundle files must not be reparse points: $Name"
        }
        $Actual = (Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Actual -ne $Expected) {
            Fail "bundle integrity verification failed for $Name"
        }
    }
    $ExpectedNames = @(
        "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md", $XpiFiles[0].Name, "install.ps1",
        "quick-swap-config.exe", "quick-swap-tools.exe", "uninstall.ps1"
    )
    if ($Seen.Count -ne $ExpectedNames.Count) {
        Fail "SHA256SUMS does not contain the exact release payload"
    }
    foreach ($Name in $ExpectedNames) {
        if (-not $Seen.ContainsKey($Name)) {
            Fail "SHA256SUMS is missing the expected payload: $Name"
        }
    }
    return $Seen
}

function Assert-Authenticode([string]$Path) {
    $Signature = Get-AuthenticodeSignature -FilePath $Path
    if ($Signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid) {
        $Thumbprint = $Signature.SignerCertificate.Thumbprint
        if ($null -eq $Thumbprint -or $ExpectedSignerThumbprints -notcontains $Thumbprint) {
            Fail "the Windows binary is signed by an unapproved publisher"
        }
        return
    }
    if ($Signature.Status -ne [System.Management.Automation.SignatureStatus]::NotSigned) {
        Fail "the Windows binary has an invalid or untrusted Authenticode signature"
    }
    if (-not $AllowUnsignedDevelopment) {
        Fail "the Windows binaries are not trusted Authenticode-signed. Development bundles require the explicit -AllowUnsignedDevelopment switch"
    }
    Write-Warning "Installing an unsigned development build: $([IO.Path]::GetFileName($Path))"
}

function Assert-FirefoxExtension([string]$Path) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $Archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $Entries = @{}
        foreach ($Entry in $Archive.Entries) {
            if ($Entries.ContainsKey($Entry.FullName)) {
                Fail "the Firefox XPI contains a duplicate entry: $($Entry.FullName)"
            }
            $Entries[$Entry.FullName] = $Entry
        }
        foreach ($Name in @(
            "META-INF/cose.sig", "META-INF/manifest.mf",
            "META-INF/mozilla.rsa", "META-INF/mozilla.sf", "manifest.json"
        )) {
            if (-not $Entries.ContainsKey($Name)) {
                Fail "the Firefox XPI is missing expected Mozilla signature metadata: $Name"
            }
        }
        $Reader = New-Object IO.StreamReader($Entries["manifest.json"].Open())
        try {
            $ExtensionManifest = $Reader.ReadToEnd() | ConvertFrom-Json
        } finally {
            $Reader.Dispose()
        }
        if ($ExtensionManifest.browser_specific_settings.gecko.id -ne $ExtensionId) {
            Fail "the Firefox XPI has the wrong extension ID"
        }
    } finally {
        $Archive.Dispose()
    }
}

try {
    if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
        Fail "Windows x64 and 64-bit PowerShell are required"
    }
    Assert-SafeLocalPath $BundleRoot "the bundle directory"
    Assert-SafeLocalPath $InstallRoot "the installation directory"
    Assert-SafeLocalPath $StartMenuDirectory "the Start Menu directory"
    $BundleDirectory = Get-Item -LiteralPath $BundleRoot
    if (($BundleDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        Fail "the bundle directory must not be a reparse point"
    }
    foreach ($Name in @(
        "quick-swap-tools.exe",
        "quick-swap-config.exe",
        "uninstall.ps1",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $BundleRoot $Name) -PathType Leaf)) {
            Fail "bundle file is missing: $Name"
        }
        $BundleItem = Get-Item -LiteralPath (Join-Path $BundleRoot $Name)
        if (($BundleItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Fail "bundle files must not be reparse points: $Name"
        }
    }
    $XpiFiles = @(Get-ChildItem -LiteralPath $BundleRoot -Filter "Quick-Swap-Tools-*-firefox.xpi" -File)
    if ($XpiFiles.Count -ne 1) {
        Fail "expected exactly one Mozilla-signed Firefox XPI"
    }
    Assert-FirefoxExtension $XpiFiles[0].FullName

    $VerifiedHashes = Assert-BundleIntegrity
    Assert-Authenticode (Join-Path $BundleRoot "quick-swap-tools.exe")
    Assert-Authenticode (Join-Path $BundleRoot "quick-swap-config.exe")

    if (Test-Path -LiteralPath $InstallRoot) {
        Fail "an installation already exists at $InstallRoot; uninstall it first"
    }
    if (Test-Path -LiteralPath $RegistryPath) {
        Fail "a Firefox native-messaging registration already exists for $HostName"
    }
    if (Test-Path -LiteralPath $ShortcutPath) {
        Fail "a Start Menu shortcut already exists at $ShortcutPath"
    }

    New-Item -ItemType Directory -Path $StagingRoot | Out-Null
    foreach ($Name in @(
        "quick-swap-tools.exe",
        "quick-swap-config.exe",
        "uninstall.ps1",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md"
    )) {
        Copy-Item -LiteralPath (Join-Path $BundleRoot $Name) -Destination $StagingRoot
    }
    Copy-Item -LiteralPath $XpiFiles[0].FullName -Destination $StagingRoot
    foreach ($Name in @("quick-swap-tools.exe", "quick-swap-config.exe", "uninstall.ps1", "LICENSE", "THIRD_PARTY_NOTICES.md", $XpiFiles[0].Name)) {
        $StagedHash = (Get-FileHash -LiteralPath (Join-Path $StagingRoot $Name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($StagedHash -ne $VerifiedHashes[$Name]) {
            Fail "staged payload integrity verification failed for $Name"
        }
    }

    $Manifest = [ordered]@{
        name = $HostName
        description = "Quick Swap Tools Windows hotkey bridge"
        path = (Join-Path $InstallRoot "quick-swap-tools.exe")
        type = "stdio"
        allowed_extensions = @($ExtensionId)
    }
    $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText(
        (Join-Path $StagingRoot "$HostName.json"),
        ($Manifest | ConvertTo-Json -Depth 3),
        $Utf8NoBom
    )
    $Owner = [ordered]@{
        schema = 1
        product = "Quick Swap Tools"
        host = $HostName
        files = @(
            "quick-swap-tools.exe", "quick-swap-config.exe", "uninstall.ps1",
            "LICENSE", "THIRD_PARTY_NOTICES.md", $XpiFiles[0].Name,
            "$HostName.json", "install-owner.json"
        )
    }
    [IO.File]::WriteAllText(
        (Join-Path $StagingRoot "install-owner.json"),
        ($Owner | ConvertTo-Json -Depth 3),
        $Utf8NoBom
    )
    $OwnedInstallFiles = @($Owner.files)
    foreach ($Name in $OwnedInstallFiles) {
        $PublishedHashes[$Name] = (Get-FileHash -LiteralPath (Join-Path $StagingRoot $Name) -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    Move-Item -LiteralPath $StagingRoot -Destination $InstallRoot
    $InstallCreated = $true

    $Shell = New-Object -ComObject WScript.Shell
    $Shortcut = $Shell.CreateShortcut($TemporaryShortcutPath)
    $Shortcut.TargetPath = $ExpectedShortcutTarget
    $Shortcut.WorkingDirectory = $InstallRoot
    $Shortcut.Description = $ExpectedShortcutDescription
    $Shortcut.Arguments = ""
    $Shortcut.IconLocation = ""
    $Shortcut.Hotkey = ""
    $Shortcut.WindowStyle = 1
    $Shortcut.Save()
    [IO.File]::Move($TemporaryShortcutPath, $ShortcutPath)
    $ShortcutCreated = $true

    # Register Firefox last so it never observes a partial installation.
    New-Item -Path $RegistryParentPath -Force | Out-Null
    New-Item -Path $RegistryStagingPath | Out-Null
    $RegistryStagingCreated = $true
    Set-Item -LiteralPath $RegistryStagingPath -Value $ManifestPath
    $ParentKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($RegistryParentSubKey, $true)
    if ($null -eq $ParentKey) {
        Fail "could not open the Firefox native-messaging registry parent"
    }
    try {
        $RenameStatus = [QuickSwapRegistryNative]::RegRenameKey(
            $ParentKey.Handle,
            $RegistryStagingName,
            $HostName
        )
    } finally {
        $ParentKey.Dispose()
    }
    if ($RenameStatus -ne 0) {
        Fail "could not atomically publish the Firefox native-messaging registration (Win32 $RenameStatus)"
    }
    $RegistryStagingCreated = $false
    $RegistryCreated = $true
    if ((Get-Item -LiteralPath $RegistryPath).GetValue("") -ne $ManifestPath) {
        Fail "Firefox native-messaging registration verification failed"
    }

    Write-Host "Quick Swap Tools was installed for the current user."
    Write-Host "Configure controls from the Start Menu: Quick Swap Tools"
    Write-Host "Add the signed Firefox extension by opening: $($XpiFiles[0].Name)"
    Write-Host "Windows defaults: Ctrl+Shift+F9 for Auction; Ctrl+Shift+F10 for Giveaway."
} catch {
    if (Test-Path -LiteralPath $TemporaryShortcutPath) {
        Remove-Item -LiteralPath $TemporaryShortcutPath -Force -ErrorAction SilentlyContinue
    }
    if ($ShortcutCreated -and (Test-Path -LiteralPath $ShortcutPath)) {
        $RollbackShell = New-Object -ComObject WScript.Shell
        $RollbackShortcut = $RollbackShell.CreateShortcut($ShortcutPath)
        if ($RollbackShortcut.TargetPath -eq $ExpectedShortcutTarget -and
            $RollbackShortcut.WorkingDirectory -eq $InstallRoot -and
            $RollbackShortcut.Description -eq $ExpectedShortcutDescription -and
            [string]::IsNullOrEmpty($RollbackShortcut.Arguments) -and
            [string]::IsNullOrEmpty($RollbackShortcut.IconLocation) -and
            [string]::IsNullOrEmpty($RollbackShortcut.Hotkey) -and
            $RollbackShortcut.WindowStyle -eq 1) {
            Remove-Item -LiteralPath $ShortcutPath -Force -ErrorAction SilentlyContinue
        }
    }
    if ($RegistryCreated -and (Test-Path -LiteralPath $RegistryPath)) {
        $RollbackKey = Get-Item -LiteralPath $RegistryPath
        if ($RollbackKey.GetValue("") -eq $ManifestPath -and
            $RollbackKey.ValueCount -eq 1 -and $RollbackKey.SubKeyCount -eq 0) {
            Remove-Item -LiteralPath $RegistryPath -Force -ErrorAction SilentlyContinue
        }
    }
    if ($RegistryStagingCreated -and (Test-Path -LiteralPath $RegistryStagingPath)) {
        Remove-Item -LiteralPath $RegistryStagingPath -Force -ErrorAction SilentlyContinue
    }
    if ($InstallCreated -and (Test-Path -LiteralPath $InstallRoot)) {
        $CanRemovePublishedInstall = $true
        try {
            $PublishedItems = @(Get-ChildItem -LiteralPath $InstallRoot -Force)
            if ($PublishedItems.Count -ne $OwnedInstallFiles.Count) {
                $CanRemovePublishedInstall = $false
            }
            foreach ($Item in $PublishedItems) {
                if ($Item.PSIsContainer -or $OwnedInstallFiles -notcontains $Item.Name -or
                    ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    $CanRemovePublishedInstall = $false
                }
            }
            foreach ($Name in $OwnedInstallFiles) {
                $PublishedPath = Join-Path $InstallRoot $Name
                if (-not (Test-Path -LiteralPath $PublishedPath -PathType Leaf)) {
                    $CanRemovePublishedInstall = $false
                    continue
                }
                $CurrentHash = (Get-FileHash -LiteralPath $PublishedPath -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($CurrentHash -ne $PublishedHashes[$Name]) {
                    $CanRemovePublishedInstall = $false
                }
            }
        } catch {
            $CanRemovePublishedInstall = $false
        }
        if ($CanRemovePublishedInstall) {
            foreach ($Name in $OwnedInstallFiles) {
                Remove-Item -LiteralPath (Join-Path $InstallRoot $Name) -Force -ErrorAction SilentlyContinue
            }
            Remove-Item -LiteralPath $InstallRoot -Force -ErrorAction SilentlyContinue
        }
    }
    if (Test-Path -LiteralPath $StagingRoot) {
        Remove-Item -LiteralPath $StagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    throw
}
