param(
  [string]$Configuration = "Release",
  [string]$Platform = "ARM",
  [string]$WindowsTargetPlatformVersion = "",
  [string]$PlatformToolset = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $root "app\EdgeMinus\EdgeMinus.vcxproj"
$manifest = Join-Path $root "app\EdgeMinus\Package.appxmanifest"
$projectDir = Split-Path -Parent $project
$outDir = Join-Path $root "output"
$appxDir = Join-Path $outDir "appx"
$pfx = Join-Path $projectDir "EdgeMinus_TemporaryKey.pfx"
$pfxPassword = "edgeminus"
$appVersion = "1.0.0.0"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $appxDir | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  throw "vswhere.exe was not found. Install Visual Studio with the UWP C++ workload."
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
  Select-Object -First 1
if (-not $msbuild) {
  throw "MSBuild was not found. Install Visual Studio with the UWP C++ workload."
}

if (-not $WindowsTargetPlatformVersion) {
  $sdkRoot = "C:\Program Files (x86)\Windows Kits\10\Include"
  $sdks = @()
  if (Test-Path $sdkRoot) {
    $sdks = Get-ChildItem $sdkRoot -Directory |
      Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
      Sort-Object { [version]$_.Name } -Descending
  }
  $preferred = $sdks | Where-Object { $_.Name -eq "10.0.15254.0" } | Select-Object -First 1
  if (-not $preferred) {
    $preferred = $sdks | Where-Object { $_.Name -eq "10.0.19041.0" } | Select-Object -First 1
  }
  if (-not $preferred) {
    $preferred = $sdks | Where-Object { $_.Name -eq "10.0.22621.0" } | Select-Object -First 1
  }
  if (-not $preferred) {
    $preferred = $sdks | Select-Object -First 1
  }
  if (-not $preferred) {
    throw "No Windows 10 SDK was found under $sdkRoot."
  }
  $WindowsTargetPlatformVersion = $preferred.Name
}

$VCToolsVersion = ""

$compilerArch = switch ($Platform) {
  "ARM" { "arm" }
  "ARM64" { "arm64" }
  default { "x86" }
}
$compilerPaths = if ($compilerArch -eq "x86") {
  @("bin\Hostx64\x86\cl.exe", "bin\Hostx86\x86\cl.exe")
} else {
  @("bin\Hostx64\$compilerArch\cl.exe", "bin\Hostx86\$compilerArch\cl.exe")
}

$installs = & $vswhere -all -products * -format json | ConvertFrom-Json
foreach ($install in $installs) {
  $msvc = Join-Path $install.installationPath "VC\Tools\MSVC"
  if (-not (Test-Path $msvc)) { continue }
  foreach ($toolsetDir in (Get-ChildItem $msvc -Directory | Sort-Object Name -Descending)) {
    $hasCompiler = $false
    foreach ($relative in $compilerPaths) {
      if (Test-Path (Join-Path $toolsetDir.FullName $relative)) {
        $hasCompiler = $true
        break
      }
    }
    if (-not $hasCompiler) { continue }
    # Microsoft.Cpp.AppContainerApplication.props always adds
    # lib\x86\store\references\platform.winmd as a forced #using file. The
    # project cannot compile with a toolset that is missing it, even when the
    # target architecture is ARM.
    $storeRef = Join-Path $toolsetDir.FullName "lib\x86\store\references\platform.winmd"
    if (-not (Test-Path $storeRef)) { continue }
    $version = [version]$toolsetDir.Name
    $detected = if ($version.Minor -lt 20) { "v141" } elseif ($version.Minor -lt 30) { "v142" } else { "v143" }
    if (-not $PlatformToolset -or $PlatformToolset -eq $detected) {
      $PlatformToolset = $detected
      $VCToolsVersion = $toolsetDir.Name
      break
    }
  }
  if ($VCToolsVersion) { break }
}

if (-not $VCToolsVersion) {
  if (-not $PlatformToolset) {
    throw "No MSVC toolset with a $Platform compiler and UWP references was found. Install the matching ARM/x86 build tools."
  }
  Write-Warning "No MSVC toolset for $PlatformToolset with UWP references was found; using MSBuild's default toolset."
}

Write-Host "MSBuild:  $msbuild"
Write-Host "SDK:      $WindowsTargetPlatformVersion"
Write-Host "Toolset:  $PlatformToolset"
if ($VCToolsVersion) { Write-Host "VC Tools: $VCToolsVersion" }
Write-Host "Target:   $Configuration|$Platform"

if (-not (Test-Path $pfx)) {
  $cert = New-SelfSignedCertificate -Subject "CN=EdgeMinus" `
    -CertStoreLocation Cert:\CurrentUser\My -Type Custom `
    -KeyExportPolicy Exportable -HashAlgorithm Sha256 -KeyLength 2048 `
    -NotAfter (Get-Date).AddYears(5) `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
  $secure = ConvertTo-SecureString -String $pfxPassword -Force -AsPlainText
  Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $secure | Out-Null
  Export-Certificate -Cert $cert -FilePath (Join-Path $outDir "EdgeMinus.cer") | Out-Null
  Write-Host "Certificate: $pfx"
}

$buildLog = Join-Path $outDir "build.log"
$common = @(
  "/p:Configuration=$Configuration",
  "/p:Platform=$Platform",
  "/p:WindowsTargetPlatformVersion=$WindowsTargetPlatformVersion",
  "/p:PlatformToolset=$PlatformToolset",
  "/p:AppxBundle=Never",
  "/p:AppxSymbolPackageEnabled=false",
  "/p:EnableSigningChecks=false",
  "/p:AppxPackageDir=$appxDir\",
  "/p:PackageCertificateKeyFile=EdgeMinus_TemporaryKey.pfx",
  "/p:PackageCertificatePassword=$pfxPassword"
)
if ($VCToolsVersion) {
  $common += "/p:VCToolsVersion=$VCToolsVersion"
}

& $msbuild $project /t:Build @common /v:minimal 2>&1 |
  Tee-Object -FilePath $buildLog
if ($LASTEXITCODE -ne 0) {
  $text = Get-Content $buildLog -Raw
  if ($text -match "MSB8020|MSB4019|Windows Store") {
    Write-Host ""
    Write-Host "Hint: install the 'Universal Windows Platform development' workload"
    Write-Host "      and the Windows 10 SDK 10.0.15254.0 (or 10.0.19041.0), plus"
    Write-Host "      the ARM build tools for the selected toolset."
  }
  throw "Build failed for $Platform."
}

$produced = Get-ChildItem $appxDir -Recurse -File -Filter "EdgeMinus*_$Platform.appx" -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($produced) {
  $appx = Join-Path $appxDir "EdgeMinus_${appVersion}_$Platform.appx"
  if ($produced.FullName -ne $appx) {
    Copy-Item $produced.FullName $appx -Force
  }
  Write-Host "Appx: $appx"
  return
}

Write-Host "MSBuild did not produce a package; packing manually with makeappx."
$binRoot = Join-Path $projectDir "bin\$Platform\$Configuration"
$exe = Get-ChildItem $binRoot -Recurse -Filter EdgeMinus.exe -ErrorAction SilentlyContinue |
  Select-Object -First 1
if (-not $exe) {
  throw "Build output was not found under $binRoot."
}
$binDir = $exe.DirectoryName

$staging = Join-Path $outDir "staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$manifestText = Get-Content $manifest -Raw
if ($Platform -eq "Win32") {
  $manifestText = $manifestText.Replace('ProcessorArchitecture="arm"', 'ProcessorArchitecture="x86"')
} elseif ($Platform -eq "ARM64") {
  $manifestText = $manifestText.Replace('ProcessorArchitecture="arm"', 'ProcessorArchitecture="arm64"')
}
Set-Content -Path (Join-Path $staging "AppxManifest.xml") -Value $manifestText -Encoding UTF8

Get-ChildItem $binDir -Force |
  Where-Object { $_.Name -ne "AppPackages" -and $_.Name -ne "Add-AppDevPackage.resources" } |
  ForEach-Object { Copy-Item $_.FullName $staging -Recurse -Force }
$assets = Join-Path $projectDir "Assets"
Copy-Item $assets (Join-Path $staging "Assets") -Recurse -Force

$makeappx = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -Last 1
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -Last 1
$makepri = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter makepri.exe -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -Last 1
if (-not $makeappx) { throw "makeappx.exe was not found." }
if (-not $signtool) { throw "signtool.exe was not found." }

if (-not (Test-Path (Join-Path $staging "resources.pri"))) {
  if (-not $makepri) { throw "makepri.exe was not found and no resources.pri was produced." }
  $priConfig = Join-Path $outDir "priconfig.xml"
  & $makepri.FullName createconfig /cf $priConfig /dq en-US /o | Out-Null
  & $makepri.FullName new /pr $staging /cf $priConfig /of (Join-Path $staging "resources.pri") /o | Out-Null
}

$appx = Join-Path $appxDir "EdgeMinus_${appVersion}_$Platform.appx"
& $makeappx.FullName pack /d $staging /p $appx /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed." }

& $signtool.FullName sign /f $pfx /p $pfxPassword /fd sha256 $appx
if ($LASTEXITCODE -ne 0) { throw "signtool failed." }

Write-Host "Appx: $appx"
