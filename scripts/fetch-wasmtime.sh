#!/bin/sh
# Fetch and verify the pinned wasmtime C API prebuilt archive.
#
# The WASM DSP sandbox links against the wasmtime C API. The dependency is
# OPTIONAL: CMake finds it via find_package(Wasmtime) (see
# cmake/modules/FindWasmtime.cmake); when absent, the sandbox is compiled out
# and a plain `cmake -B build -DWANT_QT6=ON` still configures.
#
# This script obtains the exact archive the build was verified against. No
# Rust toolchain is required - the archive is the official prebuilt C API
# release. Run from the repository root:
#
#     scripts/fetch-wasmtime.sh
#
# Result: third_party/wasmtime/{include,lib} (gitignored).

set -eu

VERSION="48.0.1"
ARCH="x86_64-linux"
ARCHIVE="wasmtime-v${VERSION}-${ARCH}-c-api.tar.xz"
URL="https://github.com/bytecodealliance/wasmtime/releases/download/v${VERSION}/${ARCHIVE}"
# SHA-256 of the archive as downloaded and verified on 2026-09-09.
SHA256="67683d04b416a8b91f0e607e7b4c22bd32f18f947c10b5372eb8c277ae3b883a"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DEST="${ROOT}/third_party"
EXTRACTED="${DEST}/wasmtime-v${VERSION}-${ARCH}-c-api"

mkdir -p "${DEST}"
cd "${DEST}"

if [ ! -f "${ARCHIVE}" ]; then
	echo "downloading ${URL}"
	curl -fL --retry 3 -o "${ARCHIVE}" "${URL}"
fi

echo "${SHA256}  ${ARCHIVE}" | sha256sum -c -

rm -rf "${EXTRACTED}" "${DEST}/wasmtime"
tar xf "${ARCHIVE}"
mv "${EXTRACTED}" "${DEST}/wasmtime"

echo "wasmtime C API v${VERSION} installed at ${DEST}/wasmtime"
ls -l "${DEST}/wasmtime/lib"
