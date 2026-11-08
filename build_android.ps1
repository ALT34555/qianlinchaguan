param([string]$Gradle='gradle',[string]$BuildRoot='')
$ErrorActionPreference='Stop'
$project=$PSScriptRoot
if(!$BuildRoot) { $BuildRoot=Join-Path $project 'obj/android' }
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
& python (Join-Path $project 'build/package_resources.py') --source $project --output (Join-Path $BuildRoot 'assets')
if($LASTEXITCODE) { throw 'Resource packaging failed' }
& $Gradle -p (Join-Path $project 'android') "-PqlcgBuildRoot=$BuildRoot" --project-cache-dir (Join-Path $BuildRoot 'gradle-cache') --no-daemon assembleRelease
if($LASTEXITCODE) { throw 'Android release build failed' }
Write-Output (Join-Path $BuildRoot 'app-build/outputs/apk/release/app-release-unsigned.apk')
