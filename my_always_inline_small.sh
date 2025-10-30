cd build
./bin/clang -S -emit-llvm 1.cpp -o output.ll
opt -passes="function-attrs" -S output.ll -o output1.ll
./bin/opt -load lib/MyAlwaysInline.so --bugpoint-enable-legacy-pm -my-always-inline -S output1.ll -o my_ai_output.ll
opt -passes="always-inline" -S output.ll -o ai_output.ll
rm output1.ll
