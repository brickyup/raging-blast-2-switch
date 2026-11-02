# Double-click installer behind INSTALL.bat: sets up WSL, builds rb2.nro from the user's own game, copies it to the SD card.
# -GameDir and -SdDir skip the folder pickers.
param([string]$GameDir, [string]$SdDir)

$XexSha256 = 'A43D7FA4404041DEB82ECABB32A22D639B96DAC351D4269114A7BC395E1B8973'
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
Add-Type -AssemblyName System.Windows.Forms

function Fail($message) {
    Write-Host "`n$message" -ForegroundColor Red
    exit 1
}
function Pick-Folder($description) {
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = $description
    $owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true }
    if ($dialog.ShowDialog($owner) -eq 'OK') { return $dialog.SelectedPath }
    return ''
}

Write-Host "Raging Blast 2 for Switch - Installer`n" -ForegroundColor Cyan

# 1. The game: checked first, so nothing is installed for a copy that can't work.
if (-not $GameDir) { $GameDir = Pick-Folder 'Select your extracted game folder (the one with default.xex inside)' }
if (-not $GameDir) { Fail 'No game folder selected.' }
$xex = Join-Path $GameDir 'default.xex'
if (-not (Test-Path -LiteralPath $xex)) {
    Fail "default.xex isn't in that folder. Your disc image has to be extracted first (see the README)."
}
if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash -ne $XexSha256) {
    Fail "This default.xex isn't supported. Only the Xbox 360 Europe disc works."
}
Write-Host 'Game files OK.' -ForegroundColor Green

# 2. WSL: Windows' built-in Linux, which the build runs in.
$distro = (& wsl.exe -l -q 2>$null) -replace "`0", '' | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^Ubuntu' } | Select-Object -First 1
if (-not $distro) {
    Write-Host "`nThe build needs Ubuntu on WSL (Windows' built-in Linux). Windows will ask for permission to install it."
    if ((Read-Host 'Install it now? (y/n)') -ne 'y') { Fail 'Stopped. Nothing was installed.' }
    Start-Process wsl.exe -Verb RunAs -Wait -ArgumentList '--install -d Ubuntu'
    Write-Host "`nNext:" -ForegroundColor Cyan
    Write-Host '  1. Restart your PC if Windows asks you to.'
    Write-Host '  2. Open Ubuntu from the Start menu and pick a username and password when it asks.'
    Write-Host '  3. Double-click INSTALL.bat again.'
    exit 0
}

# 3. Build.
Write-Host "`nBuilding. This can take 30 minutes or more the first time. Leave this window open.`n"
$env:GAME = $GameDir
$env:WSLENV = 'GAME/p'
& wsl.exe -d $distro -u root --cd $root -e bash rb2/switch-p2/tools/easy_build.sh
$buildExit = $LASTEXITCODE
$nro = Join-Path $root 'rb2\switch-p2\out\rb2.nro'
if ($buildExit -ne 0 -or -not (Test-Path -LiteralPath $nro)) {
    Fail 'The build failed. Open an issue on the GitHub page and attach build.log from this folder.'
}
Write-Host "`nBuild finished." -ForegroundColor Green

# 4. SD card.
if (-not $SdDir) { $SdDir = Pick-Folder 'Select your Switch SD card (Cancel to copy the files yourself later)' }
if (-not $SdDir) {
    Write-Host "`nNo SD card selected. Your files are in: $(Split-Path $nro)"
    Write-Host 'The README shows where to put them.'
    exit 0
}
if (-not (Test-Path -LiteralPath (Join-Path $SdDir 'atmosphere'))) {
    Write-Host 'Warning: no atmosphere folder on this card, so it may not be set up for homebrew.' -ForegroundColor Yellow
}
$dest = Join-Path $SdDir 'switch\rb2-nx'
$destGame = Join-Path $dest 'game'
if (-not (Test-Path -LiteralPath (Join-Path $destGame 'default.xex'))) {
    $need = (Get-ChildItem -LiteralPath $GameDir -Recurse -File -Force | Measure-Object Length -Sum).Sum + 100MB
    $free = (New-Object System.IO.DriveInfo((Get-Item -LiteralPath $SdDir).Root.FullName)).AvailableFreeSpace
    if ($free -lt $need) {
        Fail ("The SD card needs {0:N1} GB free but has {1:N1} GB. Free up space and run this again." -f ($need / 1GB), ($free / 1GB))
    }
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -LiteralPath $nro -Destination $dest -Force
# Keep the settings of an earlier install.
$toml = Join-Path $dest 'rb2.toml'
if (-not (Test-Path -LiteralPath $toml)) { Copy-Item -LiteralPath (Join-Path (Split-Path $nro) 'rb2.toml') -Destination $toml }
Write-Host "`nCopying the game to the SD card. This can take a while..."
& robocopy.exe $GameDir $destGame /E /R:2 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Fail 'Copying to the SD card failed. Check the card and run this again.' }

Write-Host "`nDone!" -ForegroundColor Green
Write-Host 'Put the SD card in your Switch. Hold R while opening any installed game, then pick the app in the Homebrew Menu.'
