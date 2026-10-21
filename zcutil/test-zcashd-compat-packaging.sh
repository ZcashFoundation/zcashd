#!/usr/bin/env bash
#
# Packages a fixture zcashd with zcutil/package-zcashd-compat.sh, consolidates
# it with zcutil/consolidate-zcashd-compat-release.sh, and checks the release
# assets. Needs a C compiler, objcopy/strip, git, python3 and sha256sum.

export LC_ALL=C

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-cc}"
TAG="v0.0.0-fixture"
PLATFORM="linux-x86_64"
BASENAME="zcashd-zebra-compat-${TAG}-${PLATFORM}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The packaging script runs from the parent of its own directory, so give it a
# fixture tree with fixture binaries and a git commit.
FIXTURE="$WORK/tree"
mkdir -p "$FIXTURE/zcutil" "$FIXTURE/src"
cp "$SCRIPT_DIR/package-zcashd-compat.sh" "$FIXTURE/zcutil/"
cat > "$WORK/fixture.c" <<'EOF'
#include <stdio.h>
int main(void) {
    puts("Zcash Daemon version v0.0.0-fixture");
    return 0;
}
EOF
"$CC" -g -O0 -o "$FIXTURE/src/zcashd" "$WORK/fixture.c"
"$CC" -g -O0 -o "$FIXTURE/src/zcash-cli" "$WORK/fixture.c"
echo "fixture readme" > "$FIXTURE/README.md"
git -C "$FIXTURE" init -q
git -C "$FIXTURE" -c user.name=fixture -c user.email=fixture@example.invalid commit -q --allow-empty -m fixture

PACKAGED="$WORK/publish/zcashd-compat-${PLATFORM}"
"$FIXTURE/zcutil/package-zcashd-compat.sh" \
  --host x86_64-pc-linux-gnu \
  --version "$TAG" \
  --output-dir "$PACKAGED" > /dev/null

for name in "$BASENAME" "$BASENAME.sha256" "$BASENAME.tar.gz" "$BASENAME-debug.tar.gz" "$BASENAME.metadata.json"; do
  [[ -f "$PACKAGED/$name" ]] || fail "packaging did not write $name"
done
[[ -x "$PACKAGED/$BASENAME" ]] || fail "standalone executable is not executable"
(cd "$PACKAGED" && sha256sum -c --quiet "$BASENAME.sha256") || fail "standalone executable checksum file does not verify"

# The standalone executable must be the exact bytes of ./bin/zcashd in the
# runtime archive, which are stripped and carry a debuglink.
mkdir -p "$WORK/archive"
tar -xzf "$PACKAGED/$BASENAME.tar.gz" -C "$WORK/archive" ./bin/zcashd
cmp "$PACKAGED/$BASENAME" "$WORK/archive/bin/zcashd" || fail "standalone executable differs from the archived ./bin/zcashd"
cmp -s "$PACKAGED/$BASENAME" "$FIXTURE/src/zcashd" && fail "standalone executable was not stripped"
# Without an output operand objcopy rewrites its input, so write the copy elsewhere
# and check that inspecting the release executable left its bytes alone.
objcopy --dump-section .gnu_debuglink="$WORK/debuglink" "$PACKAGED/$BASENAME" "$WORK/debuglink-inspected" 2> /dev/null \
  || fail "standalone executable has no .gnu_debuglink section"
(cd "$PACKAGED" && sha256sum -c --quiet "$BASENAME.sha256") || fail "inspecting the debuglink rewrote the standalone executable"
cmp "$PACKAGED/$BASENAME" "$WORK/archive/bin/zcashd" || fail "inspecting the debuglink rewrote the standalone executable"
"$PACKAGED/$BASENAME" | grep -q 'version v0.0.0-fixture' || fail "standalone executable does not run"

python3 - "$PACKAGED/$BASENAME.metadata.json" "$PACKAGED" "$BASENAME" <<'PY' || fail "package metadata is wrong"
import hashlib
import json
import sys
from pathlib import Path

metadata = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
packaged = Path(sys.argv[2])
basename = sys.argv[3]
binary = (packaged / basename).read_bytes()

assert metadata["runtime_binary"] == {
    "filename": basename,
    "sha256": hashlib.sha256(binary).hexdigest(),
    "size_bytes": len(binary),
    "url": "",
}, metadata["runtime_binary"]
assert metadata["runtime"]["archive"] == f"{basename}.tar.gz"
assert metadata["runtime"]["archive_member_binary_path"] == "./bin/zcashd"
assert metadata["runtime"]["archive_member_cli_path"] == "./bin/zcash-cli"
assert metadata["runtime"]["sha256"] == hashlib.sha256((packaged / f"{basename}.tar.gz").read_bytes()).hexdigest()
assert metadata["debug"]["archive"] == f"{basename}-debug.tar.gz"
assert metadata["debug"]["archive_member_debug_path"] == "./zcashd.dbg"
assert metadata["zcashd_version_output"] == "v0.0.0-fixture"
PY

FINAL="$WORK/publish/final"
"$SCRIPT_DIR/consolidate-zcashd-compat-release.sh" \
  --input-dir "$WORK/publish" \
  --output-dir "$FINAL" \
  --release-tag "$TAG" \
  --repository example/zcashd

(cd "$FINAL" && sha256sum -c --quiet SHA256SUMS.txt) || fail "SHA256SUMS.txt does not verify"
(cd "$FINAL" && sha256sum -c --quiet "$BASENAME.sha256") || fail "published checksum file does not verify"
[[ -x "$FINAL/$BASENAME" ]] || fail "published executable is not executable"
cmp "$FINAL/$BASENAME" "$PACKAGED/$BASENAME" || fail "published executable differs from the packaged one"

expected_assets="$(printf '%s\n' \
  SHA256SUMS.txt \
  "zcashd-zebra-compat-manifest-${TAG}.json" \
  "$BASENAME" \
  "$BASENAME-debug.tar.gz" \
  "$BASENAME.sha256" \
  "$BASENAME.tar.gz" | sort)"
actual_assets="$(find "$FINAL" -mindepth 1 -printf '%f\n' | sort)"
[[ "$actual_assets" == "$expected_assets" ]] || fail "unexpected release assets: $actual_assets"
grep -q "  ./$BASENAME\$" "$FINAL/SHA256SUMS.txt" || fail "SHA256SUMS.txt does not list the standalone executable"

python3 - "$FINAL/zcashd-zebra-compat-manifest-${TAG}.json" "$PACKAGED/$BASENAME.metadata.json" "$TAG" <<'PY' || fail "release manifest is wrong"
import json
import sys
from pathlib import Path

manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
metadata = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
tag = sys.argv[3]
base = f"https://github.com/example/zcashd/releases/download/{tag}"

assert manifest["schema_version"] == 1
assert manifest["release_tag"] == tag
assert manifest["repository"] == "example/zcashd"
[artifact] = manifest["artifacts"]
binary = artifact["runtime_binary"]
assert binary["url"] == f"{base}/{binary['filename']}", binary
assert binary["sha256"] == metadata["runtime_binary"]["sha256"]
assert binary["size_bytes"] == metadata["runtime_binary"]["size_bytes"]
assert artifact["runtime"]["url"] == f"{base}/{artifact['runtime']['archive']}"
assert artifact["debug"]["url"] == f"{base}/{artifact['debug']['archive']}"
assert artifact["runtime"]["sha256"] == metadata["runtime"]["sha256"]
PY

# Consolidation refuses an executable that does not match its metadata.
printf 'tampered' >> "$PACKAGED/$BASENAME"
if "$SCRIPT_DIR/consolidate-zcashd-compat-release.sh" \
  --input-dir "$WORK/publish" \
  --output-dir "$WORK/tampered" \
  --release-tag "$TAG" \
  --repository example/zcashd 2> /dev/null; then
  fail "consolidation accepted a tampered standalone executable"
fi

echo "zcashd compat packaging tests passed"
