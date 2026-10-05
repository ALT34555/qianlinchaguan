# Qianlin Chaguan - create a desktop shortcut for the Windows launcher.
#
# Called by 创建桌面快捷方式.cmd, which passes the full path of the entry script,
# so this file contains no non-ASCII literals and cannot be broken by encoding.
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Icon,
    [string]$Name
)

$ErrorActionPreference = 'Stop'

$targetPath = (Resolve-Path -LiteralPath $Target).Path
$folder = Split-Path -Parent $targetPath
$root = Split-Path -Parent (Split-Path -Parent $folder)
if (-not $Name) { $Name = [IO.Path]::GetFileNameWithoutExtension($targetPath) }
if (-not $Icon) { $Icon = Join-Path $folder 'qianlin.ico' }

$desktop = [Environment]::GetFolderPath('Desktop')
$shortcutPath = Join-Path $desktop ($Name + '.lnk')

$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcutPath)
$link.TargetPath = $targetPath
$link.WorkingDirectory = $root
$link.WindowStyle = 1
$link.Description = 'Qianlin Chaguan - local launcher'
if (Test-Path -LiteralPath $Icon) { $link.IconLocation = "$Icon,0" }
$link.Save()

Write-Host ''
if (Test-Path -LiteralPath $shortcutPath) {
    Write-Host '  Desktop shortcut created:'
    Write-Host "    $shortcutPath"
    Write-Host "    -> $targetPath"
    if (Test-Path -LiteralPath $Icon) { Write-Host "    icon: $Icon" } else { Write-Host '    icon: (qianlin.ico missing, default icon used)' }
} else {
    Write-Host '  [ERROR] The shortcut could not be created.' -ForegroundColor Red
    exit 1
}
