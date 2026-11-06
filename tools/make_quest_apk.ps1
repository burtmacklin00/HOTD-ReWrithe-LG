# quest (2026-11-03, docs/quest.md): the standalone Meta Quest 3 build: an arm64-v8a APK (OpenGL ES 3, OpenXR), signed
# with a debug key, to sideload with adb. No Gradle: CMake + the NDK build libmain.so (the game, SDL_main) and
# libSDL3.so; javac + d8 compile SDL's own activity (the SDL3 source's android-project) and ours; aapt2 links the
# manifest and the game's own data\ folder (fonts, ui, lang, vr.ini: assets; no game data); zipalign + apksigner.
#
# Needs, in <main checkout>\private\deps (never committed):
#   android-sdk     platform-tools, platforms;android-34, build-tools;35.0.1, ndk;27.3.13750724, cmake;3.31.6
#   openxr_android  the Khronos OpenXR loader for Android (openxr_loader_for_android-1.1.65.aar; unpacked into aar\)
#   dr_libs, musashi, mame_scsp, stb   as for the Windows build
# and a JDK (17+) on PATH, Python 3 (the build's generated sources), SDL 3.2.10's source (the main checkout's
# build\_deps\sdl3-src), a Windows build's build\m68kgen (the arcade sound's generated 68000 core: a cross build cannot
# run m68kmake).
#
#   powershell -ExecutionPolicy Bypass -File tools\make_quest_apk.ps1            # build + package
#   powershell -ExecutionPolicy Bypass -File tools\make_quest_apk.ps1 -NoBuild   # package the last build again
# Result: <checkout>\build-quest\HOTD-ReWrithe-quest-<version>-debug.apk (docs/quest.md: adb install)
# Built with HOTD_EXPERIMENTAL_MODES=OFF (no Typing of the Dead 0 or unreleased modes; Survival Mode in: docs/porting.md "Experimental
# modes"); -Experimental keeps them (a testing APK, never released)
param(
    [string]$Deps = "",
    [string]$BuildDir = "",
    [string]$SdlSrc = "",
    [string]$M68kGen = "",
    [switch]$NoBuild,
    [switch]$Experimental
)
$ErrorActionPreference = 'Stop'

function Say([string]$s) { Write-Host "quest: $s" }
function Run([string]$exe, [string[]]$a) {
    & $exe @a
    if ($LASTEXITCODE -ne 0) { throw "failed ($LASTEXITCODE): $exe $($a -join ' ')" }
}
function Fwd([string]$p) { return $p.Replace('\', '/') }
function Newest([string]$dir) {
    $d = Get-ChildItem -Directory $dir | Sort-Object Name | Select-Object -Last 1
    if ($null -eq $d) { throw "nothing in $dir" }
    return $d.FullName
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# the main checkout (a worktree's private\ is the main checkout's)
$main = $root
$common = (& git -C $root rev-parse --git-common-dir)
if ($LASTEXITCODE -eq 0 -and $common) {
    if (-not [IO.Path]::IsPathRooted($common)) { $common = Join-Path $root $common }
    $main = (Resolve-Path (Join-Path $common '..')).Path
}
if (-not $Deps) { $Deps = Join-Path $main 'private\deps' }
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build-quest' }
if (-not $SdlSrc) { $SdlSrc = Join-Path $main 'build\_deps\sdl3-src' }
if (-not $M68kGen) {
    foreach ($c in @((Join-Path $root 'build\m68kgen'), (Join-Path $main 'build\m68kgen'))) {
        if (-not $M68kGen -and (Test-Path (Join-Path $c 'm68kops.c'))) { $M68kGen = $c }
    }
}
$sdk = Join-Path $Deps 'android-sdk'
$ndk = Newest (Join-Path $sdk 'ndk')
$cmakeBin = Join-Path (Newest (Join-Path $sdk 'cmake')) 'bin'
$bt = Newest (Join-Path $sdk 'build-tools')
$androidJar = Join-Path $sdk 'platforms\android-34\android.jar'
$llvm = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin'
foreach ($p in @($androidJar, (Join-Path $SdlSrc 'android-project'), (Join-Path $bt 'aapt2.exe'))) {
    if (-not (Test-Path $p)) { throw "missing: $p" }
}

# the OpenXR loader for Android: the AAR unpacked (its prefab: headers, libopenxr_loader.so, a CMake config)
$oxr = Join-Path $Deps 'openxr_android'
$aar = Join-Path $oxr 'aar'
if (-not (Test-Path (Join-Path $aar 'prefab'))) {
    $file = Get-ChildItem (Join-Path $oxr 'openxr_loader_for_android-*.aar') | Select-Object -First 1
    if ($null -eq $file) { throw "no openxr_loader_for_android-*.aar in $oxr (docs/quest.md)" }
    $zip = Join-Path $oxr 'loader.zip'
    Copy-Item $file.FullName $zip -Force
    Expand-Archive $zip -DestinationPath $aar -Force
    Remove-Item $zip
}
$oxrCmake = Join-Path $aar 'prefab\modules\openxr_loader\libs\android.arm64-v8a\cmake\openxr'
$oxrSo = Join-Path $aar 'prefab\modules\openxr_loader\libs\android.arm64-v8a\libopenxr_loader.so'

# the version (CMakeLists.txt's project(... VERSION x.y.z ...)): versionName, versionCode = x*10000 + y*100 + z
$m = Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(hotd_m2 VERSION (\d+)\.(\d+)\.(\d+)' | Select-Object -First 1
$ver = "$($m.Matches[0].Groups[1].Value).$($m.Matches[0].Groups[2].Value).$($m.Matches[0].Groups[3].Value)"
$verCode = [int]$m.Matches[0].Groups[1].Value * 10000 + [int]$m.Matches[0].Groups[2].Value * 100 + [int]$m.Matches[0].Groups[3].Value
Say "version $ver ($verCode); NDK $ndk; build $BuildDir"

# ---- the native part: libmain.so + libSDL3.so ----
if (-not $NoBuild) {
    $py = ''
    try { $py = (& py -3 -c "import sys; print(sys.executable)") } catch { $py = '' }
    $cfg = @('-G', 'Ninja', '-S', $root, '-B', $BuildDir,
        "-DCMAKE_MAKE_PROGRAM=$(Fwd (Join-Path $cmakeBin 'ninja.exe'))",
        "-DCMAKE_TOOLCHAIN_FILE=$(Fwd (Join-Path $ndk 'build\cmake\android.toolchain.cmake'))",
        '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-32', '-DANDROID_STL=c++_static',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DHOTD_FETCH_SDL3=ON', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', "-DFETCHCONTENT_SOURCE_DIR_SDL3=$(Fwd $SdlSrc)",
        "-DOpenXR_DIR=$(Fwd $oxrCmake)",
        "-DHOTD_DR_LIBS_DIR=$(Fwd (Join-Path $Deps 'dr_libs'))",
        "-DHOTD_MUSASHI_DIR=$(Fwd (Join-Path $Deps 'musashi'))",
        "-DHOTD_SCSP_DIR=$(Fwd (Join-Path $Deps 'mame_scsp'))",
        "-DHOTD_STB_DIR=$(Fwd (Join-Path $Deps 'stb'))",
        "-DHOTD_EXPERIMENTAL_MODES=$(if ($Experimental) { 'ON' } else { 'OFF' })")
    if ($M68kGen) { $cfg += "-DHOTD_M68KOPS_DIR=$(Fwd $M68kGen)" } else { Say 'no build\m68kgen (a Windows build): the arcade sound is left out' }
    if ($py) { $cfg += "-DPython3_EXECUTABLE=$(Fwd $py)" }
    Run (Join-Path $cmakeBin 'cmake.exe') $cfg
    Run (Join-Path $cmakeBin 'cmake.exe') @('--build', $BuildDir, '--target', 'hotd')
}
$libMain = Join-Path $BuildDir 'libmain.so'
$libSdl = Join-Path $BuildDir '_deps\sdl3-build\libSDL3.so'
foreach ($p in @($libMain, $libSdl, $oxrSo)) { if (-not (Test-Path $p)) { throw "missing: $p" } }

# ---- the APK's contents ----
$stage = Join-Path $BuildDir 'apk'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$libDir = Join-Path $stage 'lib\arm64-v8a'
New-Item -ItemType Directory -Force $libDir | Out-Null
Run (Join-Path $llvm 'llvm-strip.exe') @('--strip-unneeded', $libMain, '-o', (Join-Path $libDir 'libmain.so'))
Run (Join-Path $llvm 'llvm-strip.exe') @('--strip-unneeded', $libSdl, '-o', (Join-Path $libDir 'libSDL3.so'))
Copy-Item $oxrSo (Join-Path $libDir 'libopenxr_loader.so')

# the game's own data\ (not game data) as assets\data\..., with assets\data_index.txt listing them (util/android_app.cpp
# copies them into the app's folder on the first start of each build)
$assets = Join-Path $stage 'assets'
New-Item -ItemType Directory -Force $assets | Out-Null
Copy-Item -Recurse (Join-Path $root 'data') (Join-Path $assets 'data')
$dataRoot = (Resolve-Path (Join-Path $assets 'data')).Path
$index = Get-ChildItem -Recurse -File $dataRoot | Sort-Object FullName | ForEach-Object {
    'data/' + $_.FullName.Substring($dataRoot.Length + 1).Replace('\', '/')
}
$utf8 = New-Object System.Text.UTF8Encoding $false
[IO.File]::WriteAllText((Join-Path $assets 'data_index.txt'), (($index -join "`n") + "`n"), $utf8)
Say "assets: $($index.Count) files of data\"
# the licences of what the APK carries (NOTICE.md): ours, the notices, the OpenXR loader's, SDL's
$lic = Join-Path $assets 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $lic 'LICENSE.txt')
Copy-Item (Join-Path $root 'NOTICE.md') (Join-Path $lic 'NOTICE.md')
Copy-Item (Join-Path $root 'docs\release\LICENSE-OpenXR-Apache-2.0.txt') (Join-Path $lic 'OpenXR-LICENSE.txt')
Copy-Item (Join-Path $SdlSrc 'LICENSE.txt') (Join-Path $lic 'SDL-LICENSE.txt')

# Java: SDL's activity (the same SDL version as libSDL3.so) + ours -> classes.dex
$classes = Join-Path $stage 'classes'
New-Item -ItemType Directory -Force $classes | Out-Null
$java = @(Get-ChildItem -Recurse -File -Filter *.java (Join-Path $SdlSrc 'android-project\app\src\main\java')) +
        @(Get-ChildItem -Recurse -File -Filter *.java (Join-Path $root 'android\quest\java'))
$javaList = Join-Path $stage 'java_files.txt'
[IO.File]::WriteAllText($javaList, (($java | ForEach-Object { '"' + (Fwd $_.FullName) + '"' }) -join "`n"), $utf8)
Run 'javac' @('-nowarn', '-Xlint:-options', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-bootclasspath', $androidJar,
              '-d', $classes, "@$javaList")
$jar = Join-Path $stage 'classes.jar'
Run 'jar' @('cf', $jar, '-C', $classes, '.')
Run (Join-Path $bt 'd8.bat') @('--release', '--min-api', '32', '--lib', $androidJar, '--output', $stage, $jar)

# the manifest (the version filled in) -> base.apk; then classes.dex, the libraries and the assets added with the JDK's
# jar (aapt2's -A on Windows names the entries with backslashes: "assets/data\fonts\...", which Android never finds)
$manifest = Join-Path $stage 'AndroidManifest.xml'
$text = [IO.File]::ReadAllText((Join-Path $root 'android\quest\AndroidManifest.xml')).Replace('@VERSION_CODE@', "$verCode").Replace('@VERSION_NAME@', $ver)
[IO.File]::WriteAllText($manifest, $text, $utf8)
$base = Join-Path $BuildDir 'base.apk'
Run (Join-Path $bt 'aapt2.exe') @('link', '-o', $base, '--manifest', $manifest, '-I', $androidJar,
                                   '--min-sdk-version', '32', '--target-sdk-version', '34')
Run 'jar' @('--update', '--no-manifest', '--file', $base, '-C', $stage, 'classes.dex', '-C', $stage, 'lib', '-C', $stage, 'assets')
$aligned = Join-Path $BuildDir 'aligned.apk'
Run (Join-Path $bt 'zipalign.exe') @('-f', '-p', '4', $base, $aligned)

# the debug key (made once, kept in private\deps: the same key for every build, so an update installs over the last)
$ks = Join-Path $Deps 'android-debug.keystore'
if (-not (Test-Path $ks)) {
    Say "making the debug key $ks"
    Run 'keytool' @('-genkeypair', '-keystore', $ks, '-storepass', 'android', '-alias', 'androiddebugkey', '-keypass', 'android',
                    '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=Android Debug,O=Android,C=US')
}
$apk = Join-Path $BuildDir "HOTD-ReWrithe-quest-$ver-debug.apk"
Run (Join-Path $bt 'apksigner.bat') @('sign', '--ks', $ks, '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
                                       '--ks-key-alias', 'androiddebugkey', '--out', $apk, $aligned)
Run (Join-Path $bt 'apksigner.bat') @('verify', $apk)
Remove-Item $base, $aligned -ErrorAction SilentlyContinue
Remove-Item "$apk.idsig" -ErrorAction SilentlyContinue
$mb = [math]::Round((Get-Item $apk).Length / 1MB, 1)
Say "APK: $apk ($mb MB)"
Say 'install: adb install -r "<that file>"   (docs/quest.md)'
