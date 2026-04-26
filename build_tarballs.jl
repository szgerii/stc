using BinaryBuilder, Pkg, Dates

name = "stc"

# a bit hacky, but keeps the CMake file as the single source of truth for versioning
cmake_content = read("CMakeLists.txt", String)
version_match = match(r"project\([^ ]+ VERSION (\d+\.\d+\.\d+)\)", cmake_content)
if isnothing(version_match)
    error("could not strip VERSION from CMakeLists.txt file")
end
version = VersionNumber(version_match[1])

# try to grab git hash for versioning on the host itself
git_hash = try
    readchomp(`git rev-parse --short HEAD`)
catch
    "unknown"
end

build_date = Dates.format(Dates.now(), "yyyy-mm-dd HH:MM:SS")

prod_dir = joinpath(@__DIR__, "products")
if isdir(prod_dir)
    for item in readdir(prod_dir; join=true)
        rm(item, recursive=true, force=true)
    end
end

# create staging dir, so that local builds are possible without having to delete build and other unnecessary local folders
staging_dir = mktempdir()
for item in readdir(@__DIR__)
    if item in [".git", ".vscode", ".vs", "build", "STC.jl"]
        continue
    end

    cp(item, joinpath(staging_dir, item))
end

println("staging dir is $staging_dir")

sources = [
    DirectorySource(staging_dir)
]

script = """
cd \${WORKSPACE}/srcdir
mkdir -p build && cd build

JV_MAJOR=\$(grep \"#define JULIA_VERSION_MAJOR\" \"\${prefix}/include/julia/julia_version.h\" | awk '{print \$3}')
JV_MINOR=\$(grep \"#define JULIA_VERSION_MINOR\" \"\${prefix}/include/julia/julia_version.h\" | awk '{print \$3}')
JV_PATCH=\$(grep \"#define JULIA_VERSION_PATCH\" \"\${prefix}/include/julia/julia_version.h\" | awk '{print \$3}')
ACTUAL_JULIA_VER="\$JV_MAJOR.\$JV_MINOR.\$JV_PATCH"

cmake .. -DCMAKE_INSTALL_PREFIX=\${prefix} \\
         -DCMAKE_TOOLCHAIN_FILE=\${CMAKE_TARGET_TOOLCHAIN} \\
         -DCMAKE_BUILD_TYPE=Release \\
         -DJULIA_INCLUDE_DIR=\${prefix}/include/julia \\
         -DJULIA_LIB_DIR=\${prefix}/lib \\
         -DSTC_JULIA_VERSION=\${ACTUAL_JULIA_VER} \\
         -DSTC_GIT_HASH=$git_hash \\
         -DSTC_BUILD_DATE=\"$build_date\" \\
         -DNO_DOCS=ON -DNO_SANDBOX=ON -DBUILD_TESTING=OFF \\
         -DNO_SAN=ON -DNO_TIDY=ON

make -j\${nproc}
make install

install_license \${WORKSPACE}/srcdir/LICENSE
"""

base_platforms = [
    Platform("x86_64", "linux"; libc="glibc"),
    Platform("aarch64", "linux"; libc="glibc"),
    Platform("x86_64", "windows"),
    Platform("aarch64", "windows"),
    # Platform("x86_64", "macos"),
    # Platform("aarch64", "macos")
]

julia_versions = [v"1.10.0", v"1.11.0", v"1.12.0"]

platforms = Platform[]
for jl_ver in julia_versions
    for p in base_platforms
        p_copy = deepcopy(p)
        p_copy["julia_version"] = string(jl_ver)
        push!(platforms, p_copy)
    end
end

platforms = expand_cxxstring_abis(platforms)

dependencies = [
    BuildDependency(PackageSpec(name="libjulia_jll", version="1.11.0")),
    Dependency("Fmt_jll")
]

products = [
    LibraryProduct("libstc", :libstc; dont_dlopen=true),
    ExecutableProduct("stc_cli", :stc_cli)
]

min_julia_ver = minimum(julia_versions)
build_tarballs(ARGS, name, version, sources, script, platforms, products, dependencies;
    julia_compat="$(min_julia_ver.major).$(min_julia_ver.minor)", preferred_gcc_version=v"12")
