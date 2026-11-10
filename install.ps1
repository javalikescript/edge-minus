
Get-AppxPackage *EdgeMinus.Browser* | Remove-AppxPackage
Add-AppxPackage -Path "output\appx\EdgeMinus_1.0.0.0_Win32.appx"
