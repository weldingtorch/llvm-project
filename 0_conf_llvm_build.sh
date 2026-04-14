ninja -C build/ clean
cmake -S llvm -B build -G Ninja -DLLVM_TARGETS_TO_BUILD=X86
