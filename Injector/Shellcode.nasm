; ============================================================================
; x64 position-independent payload used for manual mapping.
; NASM version of Shellcode.asm, for MinGW/LLVM toolchains (MSVC/clang-cl keep
; using Shellcode.asm with ml64). Semantically identical to the MASM original;
; keep both files in sync. See Shellcode.asm for the full contract.
;
; Runs inside the TARGET process. It resolves the import table of the mapped
; image via ntdll (LdrLoadDll / LdrGetProcedureAddress), registers the
; exception directory (RtlAddFunctionTable), invokes TLS callbacks and finally
; calls the image entry point (DllMain) with DLL_PROCESS_ATTACH.
;
; Entry: rcx = pointer to PayloadParams (see ManualMapper.cpp - offsets below).
; The whole routine is position independent: it never references absolute
; addresses of its own code/data; everything it needs is passed through the
; parameter block. The byte range [codeStart, codeEnd) is copied verbatim into
; the target by the injector.
; ============================================================================

BITS 64

section .text

; ---- PayloadParams field offsets (must match ManualMapper.cpp) ----
PP_imageBase        equ 0x00
PP_entryPoint       equ 0x08
PP_fnLdrLoadDll     equ 0x10
PP_fnLdrGetProc     equ 0x18
PP_fnRtlAddFT       equ 0x20
PP_pRuntimeFuncs    equ 0x28
PP_runtimeFuncCnt   equ 0x30
PP_pTlsCallbacks    equ 0x38
PP_tlsCallbackCnt   equ 0x40
PP_importCount      equ 0x48
PP_pImportEntries   equ 0x50
PP_pStatus          equ 0x58

; ---- ImportEntry field offsets ----
IE_pModuleNameW     equ 0x00
IE_pThunks          equ 0x08
IE_thunkCount       equ 0x10

; ---- Thunk field offsets ----
TH_pFuncName        equ 0x00
TH_ordinal          equ 0x08
TH_pIatSlot         equ 0x10

; ---- stack locals (relative to rsp after "sub rsp, 0x60") ----
LS_uniString        equ 0x20
LS_hModule          equ 0x30
LS_ansiString       equ 0x40
LS_procOut          equ 0x50

global codeStart
global codeEnd

codeStart:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 0x60

    mov r12, rcx                        ; r12 = PayloadParams*

    ; ------------------------------------------------------------------
    ; resolve imports
    ; ------------------------------------------------------------------
    mov rdi, qword [r12 + PP_importCount]
    mov rbx, qword [r12 + PP_pImportEntries]
    test rdi, rdi
    jz .imports_done

.load_next_dll:                         ; rbx = ImportEntry*
    ; wcslen(pModuleNameW)
    mov rcx, qword [rbx + IE_pModuleNameW]
    xor eax, eax
.wl_loop:
    movzx edx, word [rcx + rax*2]
    test dx, dx
    jz .wl_done
    inc rax
    jmp .wl_loop
.wl_done:
    shl rax, 1                          ; byte length
    mov rdx, rax
    mov word [rsp + LS_uniString], dx          ; Length
    mov word [rsp + LS_uniString + 2], dx      ; MaximumLength
    mov qword [rsp + LS_uniString + 8], rcx    ; Buffer

    xor ecx, ecx                        ; SearchPath = NULL
    xor edx, edx                        ; Flags = 0
    lea r8, [rsp + LS_uniString]        ; DllName
    lea r9, [rsp + LS_hModule]          ; &hModule
    mov rax, qword [r12 + PP_fnLdrLoadDll]
    call rax
    test rax, rax                       ; NTSTATUS
    jnz .fail
    mov r14, qword [rsp + LS_hModule]

    mov r15, qword [rbx + IE_thunkCount]
    mov r13, qword [rbx + IE_pThunks]
    test r15, r15
    jz .thunks_done

.resolve_next:                          ; r13 = Thunk*
    mov rax, qword [r13 + TH_pFuncName]
    test rax, rax
    jz .by_ordinal

    ; strlen(pFuncName)
    mov rdx, rax
    xor eax, eax
.al_loop:
    movzx r8d, byte [rdx + rax]
    test r8b, r8b
    jz .al_done
    inc rax
    jmp .al_loop
.al_done:
    mov word [rsp + LS_ansiString], ax          ; Length
    mov word [rsp + LS_ansiString + 2], ax      ; MaximumLength
    mov qword [rsp + LS_ansiString + 8], rdx    ; Buffer

    mov rcx, r14                        ; Module
    lea rdx, [rsp + LS_ansiString]      ; Name
    xor r8d, r8d                        ; Ordinal = 0
    lea r9, [rsp + LS_procOut]          ; &ProcedureAddress
    mov rax, qword [r12 + PP_fnLdrGetProc]
    call rax
    test rax, rax
    jnz .fail
    mov rcx, qword [r13 + TH_pIatSlot]
    mov rax, qword [rsp + LS_procOut]
    mov qword [rcx], rax
    jmp .next_thunk

.by_ordinal:
    mov rcx, r14                        ; Module
    mov edx, dword [r13 + TH_ordinal]
    xor r8d, r8d
    lea r9, [rsp + LS_procOut]
    mov rax, qword [r12 + PP_fnLdrGetProc]
    call rax
    test rax, rax
    jnz .fail
    mov rcx, qword [r13 + TH_pIatSlot]
    mov rax, qword [rsp + LS_procOut]
    mov qword [rcx], rax

.next_thunk:
    add r13, 0x18
    dec r15
    jnz .resolve_next

.thunks_done:
    add rbx, 0x18
    dec rdi
    jnz .load_next_dll

.imports_done:
    ; ------------------------------------------------------------------
    ; TLS callbacks (void(HINSTANCE, DWORD reason, LPVOID reserved))
    ; ------------------------------------------------------------------
    mov rdi, qword [r12 + PP_tlsCallbackCnt]
    mov rbx, qword [r12 + PP_pTlsCallbacks]
    test rdi, rdi
    jz .tls_done
.tls_loop:
    mov rcx, qword [r12 + PP_imageBase]
    mov edx, 1                          ; DLL_PROCESS_ATTACH
    xor r8d, r8d
    call qword [rbx]
    add rbx, 8
    dec rdi
    jnz .tls_loop
.tls_done:

    ; ------------------------------------------------------------------
    ; register exception directory (x64 SEH)
    ; ------------------------------------------------------------------
    mov rax, qword [r12 + PP_fnRtlAddFT]
    test rax, rax
    jz .exc_done
    mov rcx, qword [r12 + PP_pRuntimeFuncs]
    test rcx, rcx
    jz .exc_done
    mov edx, dword [r12 + PP_runtimeFuncCnt]
    test edx, edx
    jz .exc_done
    mov r8, qword [r12 + PP_imageBase]
    call rax
.exc_done:

    ; ------------------------------------------------------------------
    ; call entry point (DllMain)
    ; ------------------------------------------------------------------
    mov rax, qword [r12 + PP_entryPoint]
    test rax, rax
    jz .success
    mov rcx, qword [r12 + PP_imageBase]
    mov edx, 1                          ; DLL_PROCESS_ATTACH
    xor r8d, r8d
    call rax
    test rax, rax
    jz .fail

.success:
    mov eax, 1
    jmp .finish
.fail:
    xor eax, eax
.finish:
    mov rcx, qword [r12 + PP_pStatus]
    mov qword [rcx], rax

    add rsp, 0x60
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; Unique marker so the linker cannot fold/merge this function with others.
codeEnd:
    mov rax, 0xDEADBEEFDEADBEEF
    ret
