#!/usr/bin/env bash
set -euo pipefail

echo "=========================================================="
echo " verifying all mde exports work without pandoc "
echo "=========================================================="

# Ensure binaries are built
make -s test >/dev/null

# Create an isolated bin directory without pandoc
ISOLATED_DIR="/tmp/mde_verify_no_pandoc_bin"
rm -rf "$ISOLATED_DIR"
mkdir -p "$ISOLATED_DIR"

# Link standard utilities but strictly exclude pandoc
for cmd in cc gcc ld as ar make sh bash rm cat mkdir sed grep unzip diff head ls wc awk; do
    cmd_path=$(type -P "$cmd" || true)
    if [ -n "$cmd_path" ]; then
        ln -sf "$cmd_path" "$ISOLATED_DIR/"
    fi
done

# Run strictly in the isolated environment
export PATH="$ISOLATED_DIR"

if type -P pandoc >/dev/null 2>&1; then
    echo "ERROR: pandoc is unexpectedly present on PATH!" >&2
    exit 1
fi
echo "[OK] Confirmed: pandoc is NOT on PATH."

echo ""
echo "--- 1. Running Comprehensive Unit Test Suite (C11) ---"
./bin/test_runner

echo ""
echo "--- 2. Testing Standalone md2epub CLI without pandoc ---"
OUT_EPUB="/tmp/mde_demo_no_pandoc.epub"
rm -f "$OUT_EPUB"
./bin/md2epub examples/demo.md "$OUT_EPUB"
if [ ! -f "$OUT_EPUB" ]; then
    echo "ERROR: $OUT_EPUB was not generated!" >&2
    exit 1
fi
echo "[OK] $OUT_EPUB generated successfully ($(wc -c < "$OUT_EPUB") bytes)."

echo ""
echo "--- 3. Verifying EPUB OCF Archive Integrity (unzip -t) ---"
unzip -t "$OUT_EPUB"

echo ""
echo "--- 4. Checking First Zip Entry is Uncompressed mimetype ---"
FIRST_ENTRY=$(unzip -v "$OUT_EPUB" | awk 'NR==4 {print $1, $2, $8}')
echo "First entry: $FIRST_ENTRY"
if [[ "$FIRST_ENTRY" != *"20 Stored mimetype"* ]]; then
    echo "ERROR: First entry is not 20 bytes Stored mimetype!" >&2
    exit 1
fi
echo "[OK] OCF mimetype compliance confirmed (20 bytes, Stored at offset 0)."

rm -f "$OUT_EPUB"
rm -rf "$ISOLATED_DIR"

echo ""
echo "=========================================================="
echo " ALL EXPORTS VERIFIED: 100% OPERATIONAL WITHOUT PANDOC "
echo "=========================================================="
