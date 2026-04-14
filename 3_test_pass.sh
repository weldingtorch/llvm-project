clang -S -emit-llvm test.c -o test_c.ll;
./llvm-project/build/bin/opt -S -load-pass-plugin=./llvm-project/build/lib/Duviz.so --passes=duviz test_c.ll -o instrumented_test_c.ll
clang instrumented_test_c.ll -o test -L/usr/local/lib llvm-project/build/lib/liblogger.a