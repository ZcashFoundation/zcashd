#!/usr/bin/env bash

export LC_ALL=C

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  zcutil/consolidate-zcashd-compat-release.sh --input-dir <dir> --output-dir <dir> --release-tag <vX.Y.Z> --repository <owner/name>

Reads every *.metadata.json written by zcutil/package-zcashd-compat.sh under
<input-dir>, fills in the release download URLs, and writes the release assets
to <output-dir>:
  - each platform's runtime archive, debug archive, standalone zcashd
    executable and its .sha256 file
  - zcashd-zebra-compat-manifest-<tag>.json
  - SHA256SUMS.txt
EOF
}

INPUT_DIR=""
OUTPUT_DIR=""
RELEASE_TAG=""
REPOSITORY=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --input-dir)
      INPUT_DIR="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --release-tag)
      RELEASE_TAG="$2"
      shift 2
      ;;
    --repository)
      REPOSITORY="$2"
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

if [[ -z "$INPUT_DIR" || -z "$OUTPUT_DIR" || -z "$RELEASE_TAG" || -z "$REPOSITORY" ]]; then
  usage
  exit 2
fi

mkdir -p "$OUTPUT_DIR"

INPUT_DIR="$INPUT_DIR" \
  OUTPUT_DIR="$OUTPUT_DIR" \
  RELEASE_TAG="$RELEASE_TAG" \
  REPOSITORY="$REPOSITORY" \
  python3 - <<'PY'
import hashlib
import json
import os
import shutil
from pathlib import Path

input_dir = Path(os.environ["INPUT_DIR"])
output_dir = Path(os.environ["OUTPUT_DIR"])
release_tag = os.environ["RELEASE_TAG"]
repository = os.environ["REPOSITORY"]
base_url = f"https://github.com/{repository}/releases/download/{release_tag}"

files = sorted(input_dir.glob("**/*.metadata.json"))
if not files:
    raise SystemExit("No metadata files found.")


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def publish(source_dir, name, expected_sha256=None):
    source = source_dir / name
    if expected_sha256 is not None and sha256_file(source) != expected_sha256:
        raise SystemExit(f"{source} does not match its metadata SHA-256.")
    shutil.copy2(source, output_dir / name)


entries = []
for path in files:
    item = json.loads(path.read_text(encoding="utf-8"))
    source_dir = path.parent

    runtime = item["runtime"]
    debug = item["debug"]
    binary = item["runtime_binary"]

    runtime["url"] = f"{base_url}/{runtime['archive']}"
    debug["url"] = f"{base_url}/{debug['archive']}"
    binary["url"] = f"{base_url}/{binary['filename']}"

    checksum_name = f"{binary['filename']}.sha256"
    checksum_line = (source_dir / checksum_name).read_text(encoding="utf-8")
    if checksum_line != f"{binary['sha256']}  {binary['filename']}\n":
        raise SystemExit(f"{checksum_name} does not match the runtime_binary metadata.")

    publish(source_dir, runtime["archive"], runtime["sha256"])
    publish(source_dir, debug["archive"], debug["sha256"])
    publish(source_dir, binary["filename"], binary["sha256"])
    publish(source_dir, checksum_name)
    (output_dir / binary["filename"]).chmod(0o755)

    entries.append(item)

output = {
    "schema_version": 1,
    "release_tag": release_tag,
    "repository": repository,
    "artifacts": entries,
}

(output_dir / f"zcashd-zebra-compat-manifest-{release_tag}.json").write_text(
    json.dumps(output, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

(cd "$OUTPUT_DIR" && rm -f SHA256SUMS.txt && sha256sum ./* > SHA256SUMS.txt)
