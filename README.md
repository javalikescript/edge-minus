# EdgeMinus

EdgeMinus is a small but extensible web browser for Windows 10 Mobile written in C++/WinRT.
It uses the platform `Windows.UI.Xaml.Controls.WebView` (the EdgeHTML engine),
so it runs without bundling a browser engine and installs as a normal ARM UWP package.

## Features

- User agent override
- Search URL customization
- Pin to start the current page
- Userscript or HTML extensions
- Best effort page adaption
- Log file to troubleshoot errors

## Extensions

The app allows adding extensions by selecting a file.

- a Javascript file is installed as a userscript extension and loaded on each page.
- an HTML file is installed as a local app that can be access through the browser.
- a Zip file is deployed as it is, it should contain at least a `userscript.js` or `index.html` file.

## Build target

| Item | Value |
| --- | --- |
| OS target | Windows 10 Mobile |
| Platform | ARM (32-bit) |
| App model | UWP / AppContainer |
| Language | C++/WinRT, C++17 |
| UI | XAML constructed in code (no `.xaml` page files) |
| Default target SDK | `10.0.15254.0` |
| Minimum version | `10.0.10586.0` |
| Maximum version tested | `10.0.15254.0` |
| Default toolset | `v141` (Visual Studio 2017) |

`Win32` project configurations also exist for desktop testing, but the intended and packaged target is ARM Windows 10 Mobile.

## Requirements

- Visual Studio 2017, 2019 or 2022 with the **Universal Windows Platform development** C++ workload.
- MSVC ARM build tools for the selected toolset.
- Windows 10 SDK `10.0.15254.0` for a true Windows 10 Mobile target.
  The build script can fall back to `10.0.19041.0`, `10.0.22621.0`, or the newest installed SDK, but those are development fallbacks, not the mobile SDK.
- PowerShell 5.1 or newer for `build.ps1`.
- Windows SDK tools `makeappx.exe`, `makepri.exe` and `signtool.exe` for the manual packaging fallback.

A normal desktop Build Tools installation does not include the UWP workload or the ARM compiler. Without both, MSBuild stops with `MSB8020` for the `Windows Store` application type.

## Building

From PowerShell:

```powershell
.\build.ps1
```

From `cmd.exe`:

```bat
build.bat
```

Both default to `Release|ARM`. `build.bat` forwards its arguments to `build.ps1`, so the two produce identical results.

Optional arguments:

```powershell
.\build.ps1 -Configuration Debug
.\build.ps1 -Platform Win32
.\build.ps1 -WindowsTargetPlatformVersion 10.0.15254.0
.\build.ps1 -PlatformToolset v141
```

`Platform` is normally `ARM`; `Win32` is useful for a quick desktop launch.
`WindowsTargetPlatformVersion` and `PlatformToolset` override the project defaults.

### What the script does

1. Finds MSBuild through `vswhere`.
2. Selects an installed SDK, preferring `10.0.15254.0`, then `10.0.19041.0`, then `10.0.22621.0`, then the newest available.
3. Selects a toolset that has a compiler for the requested architecture, and maps it to `v141`, `v142` or `v143`.
4. Creates a self-signed code-signing certificate `CN=EdgeMinus` on first use:
   - PFX: `app\EdgeMinus\EdgeMinus_TemporaryKey.pfx`
   - Password: `edgeminus`
   - Exported CER: `output\EdgeMinus.cer`
5. Builds the project with MSBuild into `app\EdgeMinus\bin\<Platform>\<Configuration>`.
6. Uses the MSBuild appx output when available.
7. Falls back to staging the output, generating `resources.pri` with `makepri`, packing with `makeappx`, and signing with `signtool`.

Package output:

```text
output\appx\EdgeMinus_1.0.0.0_<Platform>.appx
```

## Deploying to a phone

1. Enable Developer Mode or interop-unlock the device.
2. Install `output\EdgeMinus.cer` into the phone's **Trusted People**
   certificate store. The package is signed with the matching self-signed
   certificate, so the phone will not install it otherwise.
3. Sideload `output\appx\EdgeMinus_1.0.0.0_ARM.appx` with the Windows Device Portal or a deployment tool.
4. To use it as a browser, set EdgeMinus as the default browser in **Settings > System > Default apps > Web browser**.

The certificate subject is `CN=EdgeMinus`, matching the manifest publisher.
If you change the publisher, regenerate the certificate or update the script and manifest together.

You could also use the Windows Device Portal in dev mode.
