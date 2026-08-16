; ============================================================================
; x64 position-independent payload used for manual mapping.
;
; Runs inside the TARGET process. It resolves the import table of the mapped
; image via ntdll (LdrLoadDll / LdrGetProcedureAddress), registers the
; exception directory (RtlAddFunctionTable), invokes TLS callbacks and finally
; calls the image entry point (DllMain) with DLL_PROCESS_ATTACH.
;
; Entry: rcx = pointer to PayloadParams (see ManualMapper.cpp — offsets below).
; The whole routine is position independent: it never references absolute
; addresses of its own code/data; everything it needs is passed through the
; parameter block. The byte range [codeStart, codeEnd) is copied verbatim into
; the target by the injector.
; ============================================================================

option casemap:none

.code

; ---- PayloadParams field offsets (must match ManualMapper.cpp) ----
PP_imageBase        EQU 00h
PP_entryPoint       EQU 08h
PP_fnLdrLoadDll     EQU 10h
PP_fnLdrGetProc     EQU 18h
PP_fnRtlAddFT       EQU 20h
PP_pRuntimeFuncs    EQU 28h
PP_runtimeFuncCnt   EQU 30h
PP_pTlsCallbacks    EQU 38h
PP_tlsCallbackCnt   EQU 40h
PP_importCount      EQU 48h
PP_pImportEntries   EQU 50h
PP_pStatus          EQU 58h

; ---- ImportEntry field offsets ----
IE_pModuleNameW     EQU 00h
IE_pThunks          EQU 08h
IE_thunkCount       EQU 10h

; ---- Thunk field offsets ----
TH_pFuncName        EQU 00h
TH_ordinal          EQU 08h
TH_pIatSlot         EQU 10h

; ---- stack locals (relative to rsp after "sub rsp, 60h") ----
LS_uniString        EQU 20h
LS_hModule          EQU 30h
LS_ansiString       EQU 40h
LS_procOut          EQU 50h

PUBLIC codeStart
PUBLIC codeEnd

codeStart PROC

    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 60h

    mov r12, rcx                        ; r12 = PayloadParams*

    ; ------------------------------------------------------------------
    ; resolve imports
    ; ------------------------------------------------------------------
    mov rdi, qword ptr [r12 + PP_importCount]
    mov rbx, qword ptr [r12 + PP_pImportEntries]
    test rdi, rdi
    jz imports_done

load_next_dll:                          ; rbx = ImportEntry*
    ; wcslen(pModuleNameW)
    mov rcx, qword ptr [rbx + IE_pModuleNameW]
    xor eax, eax
wl_loop:
    movzx edx, word ptr [rcx + rax*2]
    test dx, dx
    jz wl_done
    inc rax
    jmp wl_loop
wl_done:
    shl rax, 1                          ; byte length
    mov rdx, rax
    mov word ptr [rsp + LS_uniString], dx        ; Length
    mov word ptr [rsp + LS_uniString + 2], dx    ; MaximumLength
    mov qword ptr [rsp + LS_uniString + 8], rcx  ; Buffer

    xor ecx, ecx                        ; SearchPath = NULL
    xor edx, edx                        ; Flags = 0
    lea r8, [rsp + LS_uniString]        ; DllName
    lea r9, [rsp + LS_hModule]          ; &hModule
    mov rax, qword ptr [r12 + PP_fnLdrLoadDll]
    call rax
    test rax, rax                       ; NTSTATUS
    jnz fail
    mov r14, qword ptr [rsp + LS_hModule]

    mov r15, qword ptr [rbx + IE_thunkCount]
    mov r13, qword ptr [rbx + IE_pThunks]
    test r15, r15
    jz thunks_done

resolve_next:                           ; r13 = Thunk*
    mov rax, qword ptr [r13 + TH_pFuncName]
    test rax, rax
    jz by_ordinal

    ; strlen(pFuncName)
    mov rdx, rax
    xor eax, eax
al_loop:
    movzx r8d, byte ptr [rdx + rax]
    test r8b, r8b
    jz al_done
    inc rax
    jmp al_loop
al_done:
    mov word ptr [rsp + LS_ansiString], ax       ; Length
    mov word ptr [rsp + LS_ansiString + 2], ax   ; MaximumLength
    mov qword ptr [rsp + LS_ansiString + 8], rdx ; Buffer

    mov rcx, r14                        ; Module
    lea rdx, [rsp + LS_ansiString]      ; Name
    xor r8d, r8d                        ; Ordinal = 0
    lea r9, [rsp + LS_procOut]          ; &ProcedureAddress
    mov rax, qword ptr [r12 + PP_fnLdrGetProc]
    call rax
    test rax, rax
    jnz fail
    mov rcx, qword ptr [r13 + TH_pIatSlot]
    mov rax, qword ptr [rsp + LS_procOut]
    mov qword ptr [rcx], rax
    jmp next_thunk

by_ordinal:
    mov rcx, r14                        ; Module
    mov edx, dword ptr [r13 + TH_ordinal]
    xor r8d, r8d
    lea r9, [rsp + LS_procOut]
    mov rax, qword ptr [r12 + PP_fnLdrGetProc]
    call rax
    test rax, rax
    jnz fail
    mov rcx, qword ptr [r13 + TH_pIatSlot]
    mov rax, qword ptr [rsp + LS_procOut]
    mov qword ptr [rcx], rax

next_thunk:
    add r13, 18h
    dec r15
    jnz resolve_next

thunks_done:
    add rbx, 18h
    dec rdi
    jnz load_next_dll

imports_done:
    ; ------------------------------------------------------------------
    ; TLS callbacks (void(HINSTANCE, DWORD reason, LPVOID reserved))
    ; ------------------------------------------------------------------
    mov rdi, qword ptr [r12 + PP_tlsCallbackCnt]
    mov rbx, qword ptr [r12 + PP_pTlsCallbacks]
    test rdi, rdi
    jz tls_done
tls_loop:
    mov rcx, qword ptr [r12 + PP_imageBase]
    mov edx, 1                          ; DLL_PROCESS_ATTACH
    xor r8d, r8d
    call qword ptr [rbx]
    add rbx, 8
    dec rdi
    jnz tls_loop
tls_done:

    ; ------------------------------------------------------------------
    ; register exception directory (x64 SEH)
    ; ------------------------------------------------------------------
    mov rax, qword ptr [r12 + PP_fnRtlAddFT]
    test rax, rax
    jz exc_done
    mov rcx, qword ptr [r12 + PP_pRuntimeFuncs]
    test rcx, rcx
    jz exc_done
    mov edx, dword ptr [r12 + PP_runtimeFuncCnt]
    test edx, edx
    jz exc_done
    mov r8, qword ptr [r12 + PP_imageBase]
    call rax
exc_done:

    ; ------------------------------------------------------------------
    ; call entry point (DllMain)
    ; ------------------------------------------------------------------
    mov rax, qword ptr [r12 + PP_entryPoint]
    test rax, rax
    jz success
    mov rcx, qword ptr [r12 + PP_imageBase]
    mov edx, 1                          ; DLL_PROCESS_ATTACH
    xor r8d, r8d
    call rax
    test rax, rax
    jz fail

success:
    mov eax, 1
    jmp finish
fail:
    xor eax, eax
finish:
    mov rcx, qword ptr [r12 + PP_pStatus]
    mov qword ptr [rcx], rax

    add rsp, 60h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

codeStart ENDP

; Unique marker so the linker cannot fold/merge this function with others.
codeEnd PROC
    mov rax, 0DEADBEEFDEADBEEFh
    ret
codeEnd ENDP

END
