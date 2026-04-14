clang -S -emit-llvm test.c -o test_c.ll;
./build/bin/opt -S -load-pass-plugin=./build/lib/Duviz.so --passes=duviz test_c.ll -o instrumented_test_c.ll
clang instrumented_test_c.ll -o test -L/usr/local/lib build/lib/liblogger.a