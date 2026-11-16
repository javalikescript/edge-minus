param(
  [string]$Configuration = "Release",
  [string]$Platform = "Win32"
)

$appVersion = "1.1.0.0"

$appxSuffix = switch ($Configuration) {
  "Debug" { "_Debug" }
  default { "" }
}

Get-AppxPackage *EdgeMinus.Browser* | Remove-AppxPackage
Add-AppxPackage -Path "output\appx\EdgeMinus_${appVersion}_${Platform}${appxSuffix}_Test\EdgeMinus_${appVersion}_${Platform}${appxSuffix}.appx"
