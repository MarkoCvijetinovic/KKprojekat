cd build
#./bin/clang -S -emit-llvm 1.cpp -o output.ll
./bin/opt -passes="function-attrs" -S testmare.ll -o output1.ll
./bin/opt -load lib/MyAlwaysInline.so --bugpoint-enable-legacy-pm -my-always-inline -S output1.ll -o my_ai_output.ll
./bin/opt -passes="always-inline" -S testmare.ll -o ai_output.ll
rm output1.ll
