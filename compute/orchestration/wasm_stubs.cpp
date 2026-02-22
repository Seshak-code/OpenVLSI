#include <cstdint>

extern "C" {
    // Stubs for Wasm EH / SjLj symbols missing at link time due to SDK/Qt mismatch
    
    // Updated signatures based on linker warnings
    // __wasm_setjmp: (i32, i32, i32) -> void
    void __wasm_setjmp(int env, int label, int val) {}
    
    // __wasm_setjmp_test: (i32, i32) -> i32 (guess, or keep previous if no warning)
    int __wasm_setjmp_test(int env, int type) { return 0; }

    // emscripten_longjmp: usually (env, val)
    void emscripten_longjmp(void* env, int val) {}
    
    // __cxa_find_matching_catch_3: (i32) -> i32
    int __cxa_find_matching_catch_3(int exc_ptr) { return 0; }

    // __cxa_find_matching_catch_2: (void) -> void* (guess)
    void* __cxa_find_matching_catch_2() { return nullptr; }

    void __resumeException(void* exception) {}
    
    int llvm_eh_typeid_for(void* type) { return 1; }
}
