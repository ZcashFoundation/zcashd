#!/usr/bin/env bash

export LC_ALL=C

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  zcutil/package-zcashd-compat.sh --host <host-triple> --version <vX.Y.Z> --output-dir <dir> [--release-url-base <url>]

Build prerequisites:
  - ./zcutil/build.sh must already have produced ./src/zcashd
  - objcopy/strip (or llvm-objcopy/llvm-strip) must be available

Outputs:
  - zcashd-zebra-compat-<version>-<platform>.tar.gz
  - zcashd-zebra-compat-<version>-<platform>-debug.tar.gz
  - zcashd-zebra-compat-<version>-<platform>.metadata.json
EOF
}

HOST_TRIPLE=""
VERSION_TAG=""
OUTPUT_DIR=""
RELEASE_URL_BASE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      HOST_TRIPLE="$2"
      shift 2
      ;;
    --version)
      VERSION_TAG="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --release-url-base)
      RELEASE_URL_BASE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "$HOST_TRIPLE" || -z "$VERSION_TAG" || -z "$OUTPUT_DIR" ]]; then
  usage
  exit 2
fi

if [[ "$VERSION_TAG" != v* ]]; then
  echo "--version must include leading 'v' (for example: v6.12.4)" >&2
  exit 2
fi

case "$HOST_TRIPLE" in
  x86_64-pc-linux-gnu)
    PLATFORM_ID="linux-x86_64"
    ;;
  *)
    echo "Unsupported host triple: $HOST_TRIPLE" >&2
    exit 2
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [[ ! -x "./src/zcashd" ]]; then
  echo "./src/zcashd not found; run ./zcutil/build.sh first." >&2
  exit 1
fi

find_tool() {
  local host="$1"
  local tool="$2"
  local llvm_tool="$3"
  local candidate

  candidate="./depends/${host}/native/bin/${llvm_tool}"
  if [[ -x "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi

  candidate="./depends/${host}/native/bin/${tool}"
  if [[ -x "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi

  if command -v "$llvm_tool" >/dev/null 2>&1; then
    command -v "$llvm_tool"
    return 0
  fi

  if command -v "$tool" >/dev/null 2>&1; then
    command -v "$tool"
    return 0
  fi

  return 1
}

OBJCOPY_BIN="$(find_tool "$HOST_TRIPLE" objcopy llvm-objcopy)" || {
  echo "Unable to locate objcopy/llvm-objcopy." >&2
  exit 1
}
STRIP_BIN="$(find_tool "$HOST_TRIPLE" strip llvm-strip)" || {
  echo "Unable to locate strip/llvm-strip." >&2
  exit 1
}

mkdir -p "$OUTPUT_DIR"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

RUNTIME_STAGING="$TMPDIR/runtime"
DEBUG_STAGING="$TMPDIR/debug"
mkdir -p "$RUNTIME_STAGING/bin" "$DEBUG_STAGING"

cp "./src/zcashd" "$RUNTIME_STAGING/bin/zcashd"
if [[ -f "./COPYING" ]]; then
  cp "./COPYING" "$RUNTIME_STAGING/COPYING"
fi
if [[ -f "./README.md" ]]; then
  cp "./README.md" "$RUNTIME_STAGING/README.md"
fi

"$OBJCOPY_BIN" --only-keep-debug "$RUNTIME_STAGING/bin/zcashd" "$DEBUG_STAGING/zcashd.dbg"
"$STRIP_BIN" -s "$RUNTIME_STAGING/bin/zcashd"
"$OBJCOPY_BIN" --add-gnu-debuglink="$DEBUG_STAGING/zcashd.dbg" "$RUNTIME_STAGING/bin/zcashd"

RUNTIME_ARCHIVE_BASENAME="zcashd-zebra-compat-${VERSION_TAG}-${PLATFORM_ID}"
DEBUG_ARCHIVE_BASENAME="${RUNTIME_ARCHIVE_BASENAME}-debug"
RUNTIME_ARCHIVE_PATH="${OUTPUT_DIR}/${RUNTIME_ARCHIVE_BASENAME}.tar.gz"
DEBUG_ARCHIVE_PATH="${OUTPUT_DIR}/${DEBUG_ARCHIVE_BASENAME}.tar.gz"

tar \
  --sort=name \
  --mtime='UTC 2020-01-01' \
  --owner=0 --group=0 --numeric-owner \
  -C "$RUNTIME_STAGING" \
  -czf "$RUNTIME_ARCHIVE_PATH" \
  .

tar \
  --sort=name \
  --mtime='UTC 2020-01-01' \
  --owner=0 --group=0 --numeric-owner \
  -C "$DEBUG_STAGING" \
  -czf "$DEBUG_ARCHIVE_PATH" \
  .

RUNTIME_SHA256="$(sha256sum "$RUNTIME_ARCHIVE_PATH" | awk '{print $1}')"
DEBUG_SHA256="$(sha256sum "$DEBUG_ARCHIVE_PATH" | awk '{print $1}')"
RUNTIME_SIZE_BYTES="$(wc -c < "$RUNTIME_ARCHIVE_PATH" | tr -d ' ')"
DEBUG_SIZE_BYTES="$(wc -c < "$DEBUG_ARCHIVE_PATH" | tr -d ' ')"
GIT_COMMIT="$(git rev-parse --verify HEAD)"
ZCASHD_VERSION="$(./src/zcashd --version | awk '/version/{print $4}' | tr -d '\n')"

RUNTIME_URL=""
DEBUG_URL=""
if [[ -n "$RELEASE_URL_BASE" ]]; then
  RUNTIME_URL="${RELEASE_URL_BASE}/${RUNTIME_ARCHIVE_BASENAME}.tar.gz"
  DEBUG_URL="${RELEASE_URL_BASE}/${DEBUG_ARCHIVE_BASENAME}.tar.gz"
fi

python3 - <<PY
import json
from pathlib import Path

metadata = {
  "target_triple": "${HOST_TRIPLE}",
  "platform_id": "${PLATFORM_ID}",
  "version": "${VERSION_TAG}",
  "zcashd_version_output": "${ZCASHD_VERSION}",
  "git_commit": "${GIT_COMMIT}",
  "required_mode_flag": "-zebra-compat",
  "runtime": {
    "archive": "${RUNTIME_ARCHIVE_BASENAME}.tar.gz",
    "sha256": "${RUNTIME_SHA256}",
    "size_bytes": int("${RUNTIME_SIZE_BYTES}"),
    "archive_member_binary_path": "./bin/zcashd",
    "url": "${RUNTIME_URL}",
  },
  "debug": {
    "archive": "${DEBUG_ARCHIVE_BASENAME}.tar.gz",
    "sha256": "${DEBUG_SHA256}",
    "size_bytes": int("${DEBUG_SIZE_BYTES}"),
    "archive_member_debug_path": "./zcashd.dbg",
    "url": "${DEBUG_URL}",
  },
}

out = Path("${OUTPUT_DIR}") / "${RUNTIME_ARCHIVE_BASENAME}.metadata.json"
out.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\\n", encoding="utf-8")
PY

echo "Created:"
echo "  ${RUNTIME_ARCHIVE_PATH}"
echo "  ${DEBUG_ARCHIVE_PATH}"
echo "  ${OUTPUT_DIR}/${RUNTIME_ARCHIVE_BASENAME}.metadata.json"
