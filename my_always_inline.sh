#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  echo "Usage: $(basename "$0") <file.cpp | file.ll>"
  exit 1
}

[[ $# -eq 1 ]] || usage

INPUT="$1"
[[ -f "$INPUT" ]] || { echo "Error: '$INPUT' not found."; exit 2; }

# Resolve the input to an absolute path before cd'ing into build/
ABS_INPUT="$(readlink -f "$INPUT")"
EXT="${INPUT##*.}"

# Where to run everything
BUILD_DIR="build"

[[ -d "$BUILD_DIR" ]] || { echo "Error: '$BUILD_DIR' directory not found."; exit 3; }

pushd "$BUILD_DIR" > /dev/null

# Common filenames produced inside build/
OUT_LL="output.ll"
OUT1_LL="output1.ll"
MY_AI_OUT="my_ai_output.ll"
AI_OUT="ai_output.ll"

# Clean any leftovers from a previous run
rm -f "$OUT_LL" "$OUT1_LL" "$MY_AI_OUT" "$AI_OUT"

if [[ "$EXT" == "cpp" ]]; then
  # 1) Compile C++ to LLVM IR
  ./bin/clang -S -emit-llvm "$ABS_INPUT" -o "$OUT_LL"

  # 2) Infer attrs
  opt -passes="function-attrs" -S "$OUT_LL" -o "$OUT1_LL"

  # 3) Your legacy-PM plugin pass
  ./bin/opt -load lib/MyAlwaysInline.so --bugpoint-enable-legacy-pm \
            -my-always-inline -S "$OUT1_LL" -o "$MY_AI_OUT"

  # 4) Built-in always-inline (run on the original IR as requested)
  opt -passes="always-inline" -S "$OUT_LL" -o "$AI_OUT"

  # 5) Tidy up
  rm -f "$OUT1_LL"

elif [[ "$EXT" == "ll" ]]; then
  # In the .ll case, we don't create output.ll; use the provided IR directly.
  SRC_LL="$ABS_INPUT"

  # 1) Infer attrs
  opt -passes="function-attrs" -S "$SRC_LL" -o "$OUT1_LL"

  # 2) Your legacy-PM plugin pass
  ./bin/opt -load lib/MyAlwaysInline.so --bugpoint-enable-legacy-pm \
            -my-always-inline -S "$OUT1_LL" -o "$MY_AI_OUT"

  # 3) Built-in always-inline (run on the original input IR)
  opt -passes="always-inline" -S "$SRC_LL" -o "$AI_OUT"

  # 4) Tidy up
  rm -f "$OUT1_LL"
else
  echo "Error: Unsupported extension '.$EXT'. Use .cpp or .ll"
  popd > /dev/null
  exit 4
fi

echo "Done."
echo "  - MyAlwaysInline output:  $BUILD_DIR/$MY_AI_OUT"
echo "  - always-inline output:   $BUILD_DIR/$AI_OUT"

popd > /dev/null