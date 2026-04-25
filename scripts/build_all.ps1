$PROJECT_ROOT = (Resolve-Path "$PSScriptRoot\..").Path
$BASE_BUILD_DIR = Join-Path $PROJECT_ROOT "build"

Write-Host "project root: $PROJECT_ROOT" -ForegroundColor DarkCyan
Write-Host "build root:   $BASE_BUILD_DIR" -ForegroundColor DarkCyan

$clean_first = $false;
if ($args -contains "--clean-first") {
    $clean_first = $true;
}

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

$build_timer = [System.Diagnostics.Stopwatch]::StartNew()

$successful_builds = 0
$current_step = 0

foreach ($cfg in $configs) {
    $current_step++
    $progress = "($current_step/$($configs.Count))"

    $build_dir = Join-Path $BASE_BUILD_DIR $cfg.Name

    if (-Not (Test-Path $build_dir)) {
        Write-Host "`n$progress >> directory $build_dir not found, configuring $($cfg.Name)..." -ForegroundColor Cyan

        $cmake_config_args = @("-S", $PROJECT_ROOT, "-B", $build_dir)

        if ($cfg.Gen) { $cmake_config_args += "-G", $cfg.Gen }
        if ($cfg.CC)  { $cmake_config_args += "-DCMAKE_C_COMPILER=$($cfg.CC)" }
        if ($cfg.CXX) { $cmake_config_args += "-DCMAKE_CXX_COMPILER=$($cfg.CXX)" }

        $cmake_config_args += "-DCMAKE_BUILD_TYPE=$($cfg.Type)"
        $cmake_config_args += "-DNO_SAN=ON"
        $cmake_config_args += "-DNO_TIDY=$($cfg.NoTidy)"
        $cmake_config_args += "-DBUILD_TESTING=OFF"

        Write-Host "$progress >> cmake $($cmake_config_args -join ' ')`n" -ForegroundColor Yellow
        & cmake $cmake_config_args
        
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
    $cmake_build_args = @("--build", $build_dir, "--config", $($cfg.Type), "-j")
    if ($clean_first) {
        $cmake_build_args += "--clean-first";
    }

    Write-Host "$progress >> cmake $($cmake_build_args -join ' ')`n" -ForegroundColor Yellow
    & cmake $cmake_build_args

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
