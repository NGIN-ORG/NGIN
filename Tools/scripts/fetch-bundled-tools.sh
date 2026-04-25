#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
bundled_root="${repo_root}/Tools/ThirdParty/BuildTools"
download_dir="${bundled_root}/downloads"

cmake_version="4.3.2"
ninja_version="1.13.2"

usage() {
  cat <<'USAGE'
Usage: Tools/scripts/fetch-bundled-tools.sh [--all] [--host <host-id>]

Host ids:
  windows-x86_64
  windows-arm64
  linux-x86_64
  linux-aarch64

By default, only the current host payload is downloaded and extracted.
USAGE
}

detect_host() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"
  case "${os}:${arch}" in
    Linux:x86_64) echo "linux-x86_64" ;;
    Linux:aarch64|Linux:arm64) echo "linux-aarch64" ;;
    MINGW*:x86_64|MSYS*:x86_64|CYGWIN*:x86_64) echo "windows-x86_64" ;;
    MINGW*:aarch64|MSYS*:aarch64|CYGWIN*:aarch64|MINGW*:arm64|MSYS*:arm64|CYGWIN*:arm64) echo "windows-arm64" ;;
    *) echo "unsupported host: ${os}/${arch}" >&2; return 1 ;;
  esac
}

asset_url() {
  local tool="$1"
  local host="$2"
  case "${tool}:${host}" in
    cmake:windows-x86_64) echo "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-windows-x86_64.zip" ;;
    cmake:windows-arm64) echo "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-windows-arm64.zip" ;;
    cmake:linux-x86_64) echo "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-linux-x86_64.tar.gz" ;;
    cmake:linux-aarch64) echo "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-linux-aarch64.sh" ;;
    ninja:windows-x86_64) echo "https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-win.zip" ;;
    ninja:windows-arm64) echo "https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-winarm64.zip" ;;
    ninja:linux-x86_64) echo "https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-linux.zip" ;;
    ninja:linux-aarch64) echo "https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-linux-aarch64.zip" ;;
    *) echo "unsupported asset: ${tool}/${host}" >&2; return 1 ;;
  esac
}

download_asset() {
  local url="$1"
  local output="$2"
  mkdir -p "$(dirname "${output}")"
  if [[ -f "${output}" ]]; then
    echo "using cached ${output}"
    return
  fi
  echo "downloading ${url}"
  curl --fail --location --output "${output}" "${url}"
}

extract_single_root_archive() {
  local archive="$1"
  local destination="$2"
  local temp_dir
  temp_dir="$(mktemp -d)"
  rm -rf "${destination}"
  mkdir -p "${destination}"
  case "${archive}" in
    *.zip) unzip -q "${archive}" -d "${temp_dir}" ;;
    *.tar.gz) tar -xzf "${archive}" -C "${temp_dir}" ;;
    *) echo "unsupported archive: ${archive}" >&2; rm -rf "${temp_dir}"; return 1 ;;
  esac
  local root_count
  root_count="$(find "${temp_dir}" -mindepth 1 -maxdepth 1 | wc -l | tr -d ' ')"
  if [[ "${root_count}" == "1" && -d "$(find "${temp_dir}" -mindepth 1 -maxdepth 1 -type d | head -n 1)" ]]; then
    local root_dir
    root_dir="$(find "${temp_dir}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
    cp -a "${root_dir}/." "${destination}/"
  else
    cp -a "${temp_dir}/." "${destination}/"
  fi
  rm -rf "${temp_dir}"
}

extract_cmake_shell_installer() {
  local installer="$1"
  local destination="$2"
  local temp_dir
  temp_dir="$(mktemp -d)"
  rm -rf "${destination}"
  mkdir -p "${destination}"
  sh "${installer}" --skip-license --prefix="${temp_dir}"
  local root_dir
  root_dir="$(find "${temp_dir}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  if [[ -n "${root_dir}" && -d "${root_dir}/bin" ]]; then
    cp -a "${root_dir}/." "${destination}/"
  else
    cp -a "${temp_dir}/." "${destination}/"
  fi
  rm -rf "${temp_dir}"
}

fetch_cmake() {
  local host="$1"
  local url archive destination
  url="$(asset_url cmake "${host}")"
  archive="${download_dir}/$(basename "${url}")"
  destination="${bundled_root}/cmake/${cmake_version}/${host}"
  download_asset "${url}" "${archive}"
  if [[ "${archive}" == *.sh ]]; then
    extract_cmake_shell_installer "${archive}" "${destination}"
  else
    extract_single_root_archive "${archive}" "${destination}"
  fi
}

fetch_ninja() {
  local host="$1"
  local url archive destination executable license_file
  url="$(asset_url ninja "${host}")"
  archive="${download_dir}/$(basename "${url}")"
  destination="${bundled_root}/ninja/${ninja_version}/${host}"
  executable="ninja"
  license_file="${download_dir}/ninja-${ninja_version}-COPYING"
  [[ "${host}" == windows-* ]] && executable="ninja.exe"
  download_asset "${url}" "${archive}"
  download_asset "https://raw.githubusercontent.com/ninja-build/ninja/v${ninja_version}/COPYING" "${license_file}"
  extract_single_root_archive "${archive}" "${destination}"
  cp "${license_file}" "${destination}/COPYING"
  chmod +x "${destination}/${executable}" 2>/dev/null || true
}

hosts=()
if [[ $# -eq 0 ]]; then
  hosts=("$(detect_host)")
else
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --all)
        hosts=(windows-x86_64 windows-arm64 linux-x86_64 linux-aarch64)
        shift
        ;;
      --host)
        [[ $# -ge 2 ]] || { usage >&2; exit 2; }
        hosts=("$2")
        shift 2
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        usage >&2
        exit 2
        ;;
    esac
  done
fi

mkdir -p "${bundled_root}" "${download_dir}"
for host in "${hosts[@]}"; do
  echo "fetching bundled tools for ${host}"
  fetch_cmake "${host}"
  fetch_ninja "${host}"
done

echo "bundled tools are ready under ${bundled_root}"
