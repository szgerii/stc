$build_timer = [System.Diagnostics.Stopwatch]::StartNew()

$PROJECT_ROOT = (Resolve-Path "$PSScriptRoot\..").Path
$BASE_BUILD_DIR = Join-Path $PROJECT_ROOT "build"

Write-Host "project root: $PROJECT_ROOT" -ForegroundColor DarkCyan
Write-Host "build root:   $BASE_BUILD_DIR" -ForegroundColor DarkCyan

# assumes msvc is the default c/cpp compiler
# if this is not the case and cl is in path, it can simply be added here
# otherwise, msvc configs can also just be removed
$configs = @(
    @{ Name = "gcc-dbg";   Gen = "Ninja"; CC = "gcc";   CXX = "g++";     Type = "Debug";   NoTidy = "ON";  Build = $true },
    @{ Name = "gcc-rel";   Gen = "Ninja"; CC = "gcc";   CXX = "g++";     Type = "Release"; NoTidy = "ON";  Build = $true },
    @{ Name = "msvc-dbg";  Gen = "";      CC = "";      CXX = "";        Type = "Debug";   NoTidy = "ON";  Build = $true },
    @{ Name = "msvc-rel";  Gen = "";      CC = "";      CXX = "";        Type = "Release"; NoTidy = "ON";  Build = $true },
    @{ Name = "clang-dbg"; Gen = "Ninja"; CC = "clang"; CXX = "clang++"; Type = "Debug";   NoTidy = "ON";  Build = $true },
    @{ Name = "clang-rel"; Gen = "Ninja"; CC = "clang"; CXX = "clang++"; Type = "Release"; NoTidy = "ON";  Build = $true },
    @{ Name = "gcc-tidy";  Gen = "Ninja"; CC = "gcc";   CXX = "g++";     Type = "Debug";   NoTidy = "OFF"; Build = $true }
)

$successful_builds = 0
$current_step = 0

foreach ($cfg in $configs) {
    $current_step++
    $progress = "($current_step/$($configs.Count))"

    $build_dir = Join-Path $BASE_BUILD_DIR $cfg.Name

    if (-Not (Test-Path $build_dir)) {
        Write-Host "`n$progress >> directory $build_dir not found, configuring $($cfg.Name)..." -ForegroundColor Cyan

        $cmakeArgs = @("-S", $PROJECT_ROOT, "-B", $build_dir)

        if ($cfg.Gen) { $cmakeArgs += "-G", $cfg.Gen }
        if ($cfg.CC)  { $cmakeArgs += "-DCMAKE_C_COMPILER=$($cfg.CC)" }
        if ($cfg.CXX) { $cmakeArgs += "-DCMAKE_CXX_COMPILER=$($cfg.CXX)" }

        $cmakeArgs += "-DCMAKE_BUILD_TYPE=$($cfg.Type)"
        $cmakeArgs += "-DNO_SAN=ON"
        $cmakeArgs += "-DNO_TIDY=$($cfg.NoTidy)"
        $cmakeArgs += "-DBUILD_TESTING=OFF"

        Write-Host "$progress >> cmake $($cmakeArgs -join ' ')`n" -ForegroundColor Yellow
        & cmake $cmakeArgs
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host "$progress >> configuration failed for $($cfg.Name), skipping build." -ForegroundColor Red
            continue
        }
    }
    else {
        Write-Host "`n$progress >> directory $build_dir found, skipping config." -ForegroundColor DarkGray
    }

    if (-Not $cfg.Build) {
        Write-Host "`n$progress >> only configuration requested for $($cfg.Name), skipping build." -ForegroundColor Green
        $successful_builds++;
        continue
    }

    Write-Host "`n$progress >> building $($cfg.Name)..." -ForegroundColor Cyan

    # append --config because msvc
    Write-Host "$progress >> cmake --build $build_dir --config $($cfg.Type) -j`n" -ForegroundColor Yellow
    & cmake --build $build_dir --config $cfg.Type -j

    if ($LASTEXITCODE -eq 0) {
        $successful_builds++;
        Write-Host "`n$progress >> build successful for $($cfg.Name)" -ForegroundColor Green
    } else {
        Write-Host "`n$progress >> build failed for $($cfg.Name)" -ForegroundColor Red
    }
}

$build_timer.Stop()
$elapsed_str = "{0:mm}m {0:ss}s" -f $build_timer.Elapsed

$report_col = if ($successful_builds -eq $configs.Count) { "Green" } else { "Yellow" }
if ($successful_builds -eq 0) { $report_col = "Red" }

Write-Host "`n--- build report ---" -ForegroundColor Cyan
Write-Host "total time: $elapsed_str`n" -ForegroundColor Cyan
Write-Host "$successful_builds/$($configs.Count) builds succeeded`n" -ForegroundColor $report_col
