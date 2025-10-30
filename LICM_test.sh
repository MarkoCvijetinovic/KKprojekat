#!/bin/bash
set -euo pipefail

SRC_DIR="build/tests"
OUT_ROOT="build/tests/llopt"

mkdir -p "$OUT_ROOT"

for src in "$SRC_DIR"/*.c; do
  [ -e "$src" ] || continue
  base=$(basename "$src" .c)
  OUT_DIR="$OUT_ROOT/$base"
  mkdir -p "$OUT_DIR"

  out="$OUT_DIR/$base.ll"

  echo "→ Compiling $src → $out"
  ./build/bin/clang -S -emit-llvm -Xclang -disable-O0-optnone "$src" -o "$out"
done

echo "✅ All .ll files saved under $OUT_ROOT/"

shopt -s nullglob dotglob

SRC_ROOT="build/tests/llopt"
BUILD_DIR="/home/matija/llvm-project/build"
OPT="$BUILD_DIR/bin/opt"
DOT_CMD="dot"
LICM_PASS="lib/LLVMMyLICMPass.so"

for srcd in "$SRC_ROOT"/*/; do
  [ -d "$srcd" ] || continue
  base=$(basename "$srcd")
  OUT_DIR="$SRC_ROOT/$base"
  mkdir -p "$OUT_DIR"

  ll_in="$base.ll"
  ll_out="$base.opt.ll"
  log_out="$base.opt.log"

  cd "$OUT_DIR"

  echo "→ Generating DOTs for $base.ll"
  echo "$ll_in  ->  $ll_out"
  "$OPT" -passes=dot-cfg -disable-output "$ll_in"
  for DOT_FILE in *.dot; do
    newname="${DOT_FILE#?}"       # remove leading dot (LLVM names .0.dot etc)
    mv "$DOT_FILE" "$newname"
    png="${newname%.dot}.png"
    echo "→ Rendering $png"
    "$DOT_CMD" -Tpng "$newname" -o "$png"
    rm "$newname"
  done

  echo "→ Running LICM on $ll_in → $ll_out"
  # Dump all output (stdout + stderr) to a .log file, overwrite each time
  if ! "$OPT" -S -load "$BUILD_DIR/$LICM_PASS" \
      --bugpoint-enable-legacy-pm -my-licm \
      "$ll_in" -o "$ll_out" >"$log_out" 2>&1; then
    echo "⚠️  LICM pass failed for $base — see $log_out"
    cd - >/dev/null
    continue
  fi

  echo "→ Generating DOTs for $base.opt.ll"
  "$OPT" -passes=dot-cfg -disable-output "$ll_out"
  for DOT_FILE in *.dot; do
    newname="${DOT_FILE#?}"       # remove leading dot
    mv "$DOT_FILE" "$newname"
    png="${newname%.dot}.opt.png"
    echo "→ Rendering $png"
    "$DOT_CMD" -Tpng "$newname" -o "$png"
    rm "$newname"
  done

  cd - >/dev/null
done

echo "✅ All optimized .ll files and CFG PNGs saved under $SRC_ROOT/"

