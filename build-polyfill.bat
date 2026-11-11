@echo off
setlocal

echo ==========================================
echo Building EdgeHTML 15 polyfill.js
echo ==========================================

set "OUTPUT=extensions\polyfill-edge15.js"
set "TEMPFILE=%TEMP%\ungap-polyfill-download.js"

if exist "%OUTPUT%" del "%OUTPUT%"

type nul > "%OUTPUT%"

for %%P in (
    global-this
    assign
    at
    custom-elements
    custom-event
    dom-iterable
    element-matches
    event
    event-target
    from-entries
    get-own-property-descriptors
    has-own
    is-array
    map
    set
    own-keys
    promise-any
    promise-all-settled
    queue-microtask
    random-uuid
    url-search-params
    weakmap
    weakset
    trim-start
    trim-end
) do (
    call :download %%P
    if errorlevel 1 goto :failed
)

if exist "%TEMPFILE%" del "%TEMPFILE%"

echo.
echo ==========================================
echo SUCCESS: %OUTPUT% generated.
echo ==========================================
exit /b 0

:download
echo Downloading @ungap/%1 ...

curl -fLsS --retry 2 ^
    "https://unpkg.com/@ungap/%1" ^
    -o "%TEMPFILE%"

if errorlevel 1 (
    echo ERROR: Failed downloading @ungap/%1
    exit /b 1
)

echo.>> "%OUTPUT%"
echo /* @ungap/%1 */>> "%OUTPUT%"
type "%TEMPFILE%" >> "%OUTPUT%"
echo.>> "%OUTPUT%"

if errorlevel 1 exit /b 1

exit /b 0

:failed
if exist "%TEMPFILE%" del "%TEMPFILE%"
echo.
echo BUILD FAILED.
exit /b 1
