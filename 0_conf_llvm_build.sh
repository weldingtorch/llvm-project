ninja -C llvm-project/build/ clean
cmake -S llvm-project/llvm -B llvm-project/build -G Ninja -DLLVM_TARGETS_TO_BUILD=X86
