clang -S -emit-llvm test.c -o test_c_ir.ll;
./build/bin/opt -S -load-pass-plugin=./build/lib/Duviz.so --passes=duviz test_c_ir.ll -o test_c_ir_instrumented.ll
clang test_c_ir_instrumented.ll -o test -L/usr/local/lib build/lib/liblogger.a