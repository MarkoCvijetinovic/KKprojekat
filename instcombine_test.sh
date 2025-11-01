#!/bin/bash
shopt -s nullglob dotglob

for src in ./build/instcombine_tests/*.ll; do	
	echo "Compiling $(basename "$src" .ll)"
	./build/bin/opt --load ./build/lib/MyInstCombine.so --bugpoint-enable-legacy-pm -my-inst-combine -S $src -o ./build/instcombine_tests/$(basename "$src" .ll)_after.ll
done
