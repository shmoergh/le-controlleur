#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_NAME="le_controlleur"
OUTPUT_PREFIX="le-controlleur"
DEBUG_LOG="ON"
DEBUG_LEVEL="1"

usage() {
	echo "Usage: $0 [--no-debug|--debug|--trace]"
	echo "  --no-debug  Disable firmware debug logs"
	echo "  --debug     Enable INFO-level logs (default)"
	echo "  --trace     Enable TRACE-level logs"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--no-debug)
			DEBUG_LOG="OFF"
			DEBUG_LEVEL="0"
			shift
			;;
		--debug)
			DEBUG_LOG="ON"
			DEBUG_LEVEL="1"
			shift
			;;
		--trace)
			DEBUG_LOG="ON"
			DEBUG_LEVEL="2"
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown option: $1" >&2
			usage
			exit 1
			;;
	esac
done

build_target() {
	local board="$1"
	local platform="$2"
	local build_dir="$3"
	local output_file="$4"

	echo "Configuring ${board} (${platform})..."
	cmake -S "$ROOT_DIR" -B "$ROOT_DIR/$build_dir" \
		-DPICO_BOARD="$board" \
		-DPICO_PLATFORM="$platform" \
		-DLE_CONTROLLEUR_DEBUG_LOG="$DEBUG_LOG" \
		-DLE_CONTROLLEUR_DEBUG_LEVEL="$DEBUG_LEVEL"

	echo "Building ${board} (${platform})..."
	cmake --build "$ROOT_DIR/$build_dir"

	local uf2_path="$ROOT_DIR/$build_dir/${TARGET_NAME}.uf2"
	if [[ ! -f "$uf2_path" ]]; then
		echo "Expected UF2 file not found: $uf2_path" >&2
		exit 1
	fi

	cp "$uf2_path" "$ROOT_DIR/$output_file"
	echo "Saved $output_file"
}

echo "Debug logging: ${DEBUG_LOG} (level ${DEBUG_LEVEL})"

# Build order requested by user: Pico first, then Pico 2.
build_target "pico" "rp2040" "build-pico" "${OUTPUT_PREFIX}-pico.uf2"
build_target "pico2" "rp2350-arm-s" "build-pico-2" "${OUTPUT_PREFIX}-pico-2.uf2"

echo "Done."
