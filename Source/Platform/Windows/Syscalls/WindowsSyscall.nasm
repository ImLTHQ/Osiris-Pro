; x64 syscall trampoline.
; NASM version of WindowsSyscall.asm, for MinGW/LLVM toolchains (MSVC/clang-cl
; keep using WindowsSyscall.asm with ml64). Semantically identical.
;
; rcx = SyscallParams* { uint64 syscallIndex; uintptr_t firstParam; }
; The remaining register arguments are forwarded by the C caller unchanged.

section .text

global systemCall

systemCall:
    mov rax, [rcx]      ; load syscall index into rax
    mov r10, [rcx + 8]  ; load first parameter into r10
    syscall
    ret
