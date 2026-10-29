#!/bin/bash

set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
linux_build_directory="${project_root}/build_linux"
windows_build_directory="${project_root}/build_windows"
mingw_sysroot="${MINGW_SYSROOT:-/usr/x86_64-w64-mingw32}"
mingw_pkg_config_libdir="${MINGW_PKG_CONFIG_LIBDIR:-${mingw_sysroot}/lib/pkgconfig:${mingw_sysroot}/share/pkgconfig:/usr/lib/x86_64-w64-mingw32/pkgconfig}"

build_linux() {
    printf '[Linux] Configuring in %s\n' "${linux_build_directory}"
    cmake -S "${project_root}" -B "${linux_build_directory}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=ON || return $?

    printf '[Linux] Compiling\n'
    cmake --build "${linux_build_directory}" --parallel || return $?

    printf '[Linux] Running tests\n'
    ctest --test-dir "${linux_build_directory}" --output-on-failure
}

build_windows() {
    local tool
    for tool in x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ \
                x86_64-w64-mingw32-windres x86_64-w64-mingw32-objdump; do
        if ! command -v "${tool}" >/dev/null 2>&1; then
            printf '[Windows] Missing MinGW-w64 tool: %s\n' "${tool}" >&2
            printf '[Windows] Install the x86_64 MinGW-w64 toolchain and retry.\n' >&2
            return 127
        fi
    done
    if [[ ! -d "${mingw_sysroot}" ]]; then
        printf '[Windows] MinGW sysroot not found: %s\n' "${mingw_sysroot}" >&2
        printf '[Windows] Set MINGW_SYSROOT to the Windows dependency sysroot.\n' >&2
        return 2
    fi

    printf '[Windows] Configuring MinGW cross-build in %s\n' "${windows_build_directory}"
    if PKG_CONFIG_LIBDIR="${mingw_pkg_config_libdir}" \
       PKG_CONFIG_PATH= \
           cmake -S "${project_root}" -B "${windows_build_directory}" -G Ninja \
               -U 'IMGUI_*' \
               -DCMAKE_BUILD_TYPE=Release \
               -DBUILD_TESTING=OFF \
               -DCMAKE_SYSTEM_NAME=Windows \
               -DCMAKE_C_COMPILER="$(command -v x86_64-w64-mingw32-gcc)" \
               -DCMAKE_CXX_COMPILER="$(command -v x86_64-w64-mingw32-g++)" \
               -DCMAKE_RC_COMPILER="$(command -v x86_64-w64-mingw32-windres)" \
               -DCMAKE_FIND_ROOT_PATH="${mingw_sysroot}" \
               -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
               -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
               -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
               -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY; then
        :
    else
        local configure_status=$?
        printf '[Windows] Configure failed. Install Windows-target ImGui (including imgui.pc), GLFW3, Assimp >= 5 and their CMake package files under MINGW_SYSROOT; set MINGW_PKG_CONFIG_LIBDIR to the target .pc directories. Do not point these variables at Linux libraries.\n' >&2
        return "${configure_status}"
    fi

    printf '[Windows] Compiling\n'
    cmake --build "${windows_build_directory}" --parallel || return $?

    local executable="${windows_build_directory}/AquariusEngine.exe"
    if [[ ! -f "${executable}" ]] ||
       ! x86_64-w64-mingw32-objdump -f "${executable}" |
           grep -q 'file format pei-'; then
        printf '[Windows] Build did not produce a verified Windows PE executable: %s\n' \
            "${executable}" >&2
        return 1
    fi
    printf '[Windows] Verified Windows executable: %s\n' "${executable}"
}

build_linux &
linux_pid=$!
build_windows &
windows_pid=$!

linux_status=0
windows_status=0
if wait "${linux_pid}"; then
    printf '[Linux] Build and tests succeeded (exit 0)\n'
else
    linux_status=$?
    printf '[Linux] Build or tests failed (exit %s)\n' "${linux_status}" >&2
fi
if wait "${windows_pid}"; then
    printf '[Windows] Build succeeded (exit 0)\n'
else
    windows_status=$?
    printf '[Windows] Build failed (exit %s)\n' "${windows_status}" >&2
fi

printf 'Build results: Linux=%s Windows=%s\n' "${linux_status}" "${windows_status}"
if (( linux_status != 0 || windows_status != 0 )); then
    exit 1
fi
