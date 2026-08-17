#include "ManualMapper.h"

#include <tlhelp32.h>

#include <cstring>
#include <memory>

// Global scope on purpose: declared with C linkage so the names stay unmangled
// on every toolchain. (A namespace-scope extern "C" variable would be mangled
// by GCC/MinGW, while ml64 and nasm both export plain codeStart/codeEnd.)
extern "C" unsigned char codeStart[];
extern "C" unsigned char codeEnd[];

namespace mm {
namespace {

// ---------------------------------------------------------------------------
// Payload structures. Layout must match Shellcode.asm offsets (all uint64_t).
// ---------------------------------------------------------------------------
struct alignas(8) PayloadParams {
    std::uint64_t imageBase;                 // 0x00
    std::uint64_t entryPoint;                // 0x08
    std::uint64_t fnLdrLoadDll;              // 0x10
    std::uint64_t fnLdrGetProcedureAddress;  // 0x18
    std::uint64_t fnRtlAddFunctionTable;     // 0x20
    std::uint64_t pRuntimeFunctionTable;     // 0x28
    std::uint64_t runtimeFunctionCount;      // 0x30
    std::uint64_t pTlsCallbacks;             // 0x38
    std::uint64_t tlsCallbackCount;          // 0x40
    std::uint64_t importCount;               // 0x48
    std::uint64_t pImportEntries;            // 0x50
    std::uint64_t pStatus;                   // 0x58
};
static_assert(sizeof(PayloadParams) == 0x60, "PayloadParams layout mismatch");

struct alignas(8) ImportEntry {
    std::uint64_t pModuleNameW;  // 0x00
    std::uint64_t pThunks;       // 0x08
    std::uint64_t thunkCount;    // 0x10
};
static_assert(sizeof(ImportEntry) == 0x18);

struct alignas(8) Thunk {
    std::uint64_t pFuncName;     // 0x00
    std::uint64_t ordinal;       // 0x08
    std::uint64_t pIatSlot;      // 0x10
};
static_assert(sizeof(Thunk) == 0x18);

std::size_t ShellcodeSize() {
    const auto start = reinterpret_cast<std::uintptr_t>(codeStart);
    const auto end = reinterpret_cast<std::uintptr_t>(codeEnd);
    // The linker must keep the two labels in order. If it routed the address
    // references through incremental-link (ILT) thunks, the subtraction can
    // come out negative and wrap to a huge size_t; report 0 in that case so
    // the caller can fail cleanly instead of overflowing a buffer.
    if (end <= start)
        return 0;
    return static_cast<std::size_t>(end - start);
}

std::wstring LastErrorText(DWORD err) {
    wchar_t buf[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, 512, nullptr);
    std::wstring s(buf);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' '))
        s.pop_back();
    return s;
}

std::wstring Err(const std::wstring& msg) {
    const DWORD e = GetLastError();
    return e ? msg + L" (error " + std::to_wstring(e) + L": " + LastErrorText(e) + L")"
             : msg;
}

// ---------------------------------------------------------------------------
// Parsed PE description.
// ---------------------------------------------------------------------------
struct ImportThunk {
    std::uintptr_t iatSlotRva = 0;  // RVA of the IAT slot inside the image
    std::string funcName;           // empty when imported by ordinal
    std::uint16_t ordinal = 0;
};

struct ImportDll {
    std::string name;  // e.g. "KERNEL32.dll"
    std::vector<ImportThunk> thunks;
};

struct ParsedPe {
    std::uint64_t preferredBase = 0;
    std::uintptr_t entryRva = 0;
    std::size_t imageSize = 0;
    std::size_t headersSize = 0;
    bool hasRelocs = false;
    std::vector<ImportDll> imports;
};

bool ParsePe(const std::uint8_t* file, std::size_t fileSize, ParsedPe& out,
             std::wstring& error) {
    if (fileSize < sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD)) {
        error = L"input is too small to be a PE file";
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = L"not a PE file (missing MZ signature)";
        return false;
    }
    if (dos->e_lfanew <= 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > fileSize) {
        error = L"invalid PE header offset";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        error = L"invalid PE signature";
        return false;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        error = L"module is not x64 (expected IMAGE_FILE_MACHINE_AMD64)";
        return false;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        error = L"module is not PE32+";
        return false;
    }
    out.preferredBase = nt->OptionalHeader.ImageBase;
    out.entryRva = nt->OptionalHeader.AddressOfEntryPoint;
    out.imageSize = nt->OptionalHeader.SizeOfImage;
    out.headersSize = nt->OptionalHeader.SizeOfHeaders;
    if (out.imageSize == 0 || out.imageSize > 0x40000000u) {
        error = L"suspicious SizeOfImage";
        return false;
    }
    const auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    out.hasRelocs = relocDir.VirtualAddress != 0 && relocDir.Size != 0;
    return true;
}

// Builds the in-memory (RVAs aligned) image from the on-disk file image.
bool BuildMappedImage(const std::uint8_t* file, std::size_t fileSize,
                      const ParsedPe& pe, std::vector<std::uint8_t>& image,
                      std::wstring& error) {
    image.assign(pe.imageSize, 0);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file + dos->e_lfanew);

    if (pe.headersSize <= image.size())
        std::memcpy(image.data(), file, pe.headersSize);

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const DWORD va = sec[i].VirtualAddress;
        const DWORD vsz = sec[i].Misc.VirtualSize;
        const DWORD raw = sec[i].PointerToRawData;
        const DWORD rawSz = sec[i].SizeOfRawData;
        if (raw == 0 || rawSz == 0)
            continue;
        if (raw + rawSz > fileSize) {
            error = L"section raw data exceeds file size";
            return false;
        }
        const std::size_t copyLen = (vsz < rawSz ? vsz : rawSz);
        if (va + copyLen > image.size()) {
            error = L"section virtual range exceeds SizeOfImage";
            return false;
        }
        std::memcpy(image.data() + va, file + raw, copyLen);
    }
    return true;
}

// Applies base relocations for the case actualBase != preferredBase.
bool ApplyRelocations(std::vector<std::uint8_t>& image, std::uint64_t preferredBase,
                      std::uint64_t actualBase, std::wstring& error) {
    if (preferredBase == actualBase)
        return true;
    const std::int64_t delta =
        static_cast<std::int64_t>(actualBase) - static_cast<std::int64_t>(preferredBase);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (relocDir.VirtualAddress == 0 || relocDir.Size == 0)
        return true;  // no relocations needed

    std::size_t rva = relocDir.VirtualAddress;
    const std::size_t end = static_cast<std::size_t>(relocDir.VirtualAddress) + relocDir.Size;
    while (rva + sizeof(IMAGE_BASE_RELOCATION) <= end) {
        const auto* block =
            reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image.data() + rva);
        const std::size_t blockSize = block->SizeOfBlock;
        if (blockSize < sizeof(IMAGE_BASE_RELOCATION) || rva + blockSize > end) {
            error = L"corrupt relocation block";
            return false;
        }
        const std::size_t count =
            (blockSize - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(std::uint16_t);
        const auto* entries = reinterpret_cast<const std::uint16_t*>(
            image.data() + rva + sizeof(IMAGE_BASE_RELOCATION));
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t type = static_cast<std::uint8_t>(entries[i] >> 12);
            const std::uint16_t off = static_cast<std::uint16_t>(entries[i] & 0x0FFF);
            if (type == IMAGE_REL_BASED_ABSOLUTE)
                continue;
            const std::size_t target =
                static_cast<std::size_t>(block->VirtualAddress) + off;
            if (target + sizeof(std::uint64_t) > image.size()) {
                error = L"relocation target out of range";
                return false;
            }
            if (type == IMAGE_REL_BASED_HIGHLOW) {
                auto* p = reinterpret_cast<std::uint32_t*>(image.data() + target);
                *p = static_cast<std::uint32_t>(static_cast<std::uint64_t>(*p) + delta);
            } else if (type == IMAGE_REL_BASED_DIR64) {
                auto* p = reinterpret_cast<std::uint64_t*>(image.data() + target);
                *p = static_cast<std::uint64_t>(static_cast<std::int64_t>(*p) + delta);
            } else {
                error = L"unsupported relocation type " + std::to_wstring(type);
                return false;
            }
        }
        rva += blockSize;
    }
    return true;
}

// Parses the import directory from the mapped image (RVA addressing).
bool ParseImports(std::vector<std::uint8_t>& image, ParsedPe& pe, std::wstring& error) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (impDir.VirtualAddress == 0)
        return true;

    const std::size_t rva = impDir.VirtualAddress;
    if (rva + sizeof(IMAGE_IMPORT_DESCRIPTOR) > image.size()) {
        error = L"invalid import directory";
        return false;
    }
    const auto* desc =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image.data() + rva);
    for (; desc->Name != 0; ++desc) {
        if (reinterpret_cast<const std::uint8_t*>(desc + 1) > image.data() + image.size()) {
            error = L"import descriptors run past image end";
            return false;
        }
        ImportDll dll;
        const char* modName = reinterpret_cast<const char*>(image.data() + desc->Name);
        dll.name.assign(modName);  // caller guarantees image bounds via Name < SizeOfImage

        const std::uintptr_t thunkRva = desc->OriginalFirstThunk
                                            ? desc->OriginalFirstThunk
                                            : desc->FirstThunk;
        const std::uintptr_t iatRva = desc->FirstThunk;
        for (std::size_t i = 0;; ++i) {
            const auto* t = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                image.data() + thunkRva + i * sizeof(IMAGE_THUNK_DATA64));
            if (t->u1.AddressOfData == 0)
                break;
            ImportThunk th;
            th.iatSlotRva = iatRva + i * sizeof(IMAGE_THUNK_DATA64);
            if (t->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                th.ordinal = static_cast<std::uint16_t>(t->u1.Ordinal & 0xFFFF);
            } else {
                const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                    image.data() + static_cast<std::size_t>(t->u1.AddressOfData));
                th.funcName.assign(reinterpret_cast<const char*>(byName->Name));
            }
            dll.thunks.push_back(th);
        }
        if (!dll.thunks.empty())
            pe.imports.push_back(std::move(dll));
    }
    return true;
}

// Reads the TLS callbacks list from the mapped image into absolute target VAs.
bool ParseTlsCallbacks(std::vector<std::uint8_t>& image, std::uint64_t preferredBase,
                       std::vector<std::uintptr_t>& callbacks, std::wstring& error) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto& tlsDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tlsDir.VirtualAddress == 0)
        return true;
    if (tlsDir.VirtualAddress + sizeof(IMAGE_TLS_DIRECTORY64) > image.size()) {
        error = L"invalid TLS directory";
        return false;
    }
    const auto* tls = reinterpret_cast<const IMAGE_TLS_DIRECTORY64*>(
        image.data() + tlsDir.VirtualAddress);
    const std::uint64_t callbacksVa = tls->AddressOfCallBacks;
    if (callbacksVa < preferredBase ||
        callbacksVa - preferredBase + sizeof(std::uint64_t) > image.size())
        return true;  // no callbacks

    std::size_t rva = static_cast<std::size_t>(callbacksVa - preferredBase);
    for (;;) {
        if (rva + sizeof(std::uint64_t) > image.size())
            break;
        std::uint64_t cb = 0;
        std::memcpy(&cb, image.data() + rva, sizeof(cb));
        if (cb == 0)
            break;
        callbacks.push_back(static_cast<std::uintptr_t>(cb));
        rva += sizeof(std::uint64_t);
    }
    return true;
}

// ---------------------------------------------------------------------------
// In-target payload assembly.
// ---------------------------------------------------------------------------
std::size_t Align8(std::size_t v) { return (v + 7) & ~std::size_t(7); }

struct PayloadLayout {
    std::size_t shellcode = 0;
    std::size_t params = 0;
    std::size_t importEntries = 0;
    std::size_t thunks = 0;
    std::size_t moduleNames = 0;
    std::size_t funcNames = 0;
    std::size_t tlsCallbacks = 0;
    std::size_t status = 0;
    std::size_t total = 0;
};

PayloadLayout ComputeLayout(const ParsedPe& pe, std::size_t shellcodeSize,
                            std::size_t tlsCallbackCount, std::size_t& moduleNamesBytes,
                            std::size_t& funcNamesBytes) {
    PayloadLayout l;
    std::size_t totalThunks = 0;
    for (const auto& d : pe.imports)
        totalThunks += d.thunks.size();

    moduleNamesBytes = 0;
    for (const auto& d : pe.imports)
        moduleNamesBytes += (d.name.size() + 1) * sizeof(wchar_t);
    funcNamesBytes = 0;
    for (const auto& d : pe.imports)
        for (const auto& t : d.thunks)
            if (!t.funcName.empty())
                funcNamesBytes += t.funcName.size() + 1;

    l.shellcode = 0;
    std::size_t off = shellcodeSize;
    l.params = Align8(off);
    off = l.params + sizeof(PayloadParams);
    l.importEntries = Align8(off);
    off = l.importEntries + pe.imports.size() * sizeof(ImportEntry);
    l.thunks = Align8(off);
    off = l.thunks + totalThunks * sizeof(Thunk);
    l.moduleNames = Align8(off);
    l.funcNames = l.moduleNames + moduleNamesBytes;
    l.tlsCallbacks = Align8(l.funcNames + funcNamesBytes);
    l.status = Align8(l.tlsCallbacks + tlsCallbackCount * sizeof(std::uint64_t));
    l.total = l.status + sizeof(std::uint64_t);
    return l;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
std::vector<ProcessInfo> FindProcessesByName(const wchar_t* exeName) {
    std::vector<ProcessInfo> result;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0)
                result.push_back({pe.th32ProcessID, pe.szExeFile});
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

bool ManualMap(HANDLE process, const std::uint8_t* dllBytes, std::size_t dllSize,
               std::wstring& error) {
    if (process == nullptr || process == INVALID_HANDLE_VALUE) {
        error = L"invalid process handle";
        return false;
    }
    if (dllBytes == nullptr || dllSize == 0) {
        error = L"empty module data";
        return false;
    }

    // ---- 1. parse the PE ----
    ParsedPe pe;
    if (!ParsePe(dllBytes, dllSize, pe, error))
        return false;

    // ---- 2. allocate the image in the target ----
    LPVOID imageBase = VirtualAllocEx(process, reinterpret_cast<LPVOID>(pe.preferredBase),
                                      pe.imageSize, MEM_RESERVE | MEM_COMMIT,
                                      PAGE_EXECUTE_READWRITE);
    if (!imageBase) {
        imageBase = VirtualAllocEx(process, nullptr, pe.imageSize, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
    }
    if (!imageBase) {
        error = Err(L"VirtualAllocEx(image) failed");
        return false;
    }

    // ---- 3. build the mapped image (headers + sections, VA-aligned) ----
    std::vector<std::uint8_t> image;
    if (!BuildMappedImage(dllBytes, dllSize, pe, image, error)) {
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }

    // ---- 4. relocations ----
    if (!ApplyRelocations(image, pe.preferredBase,
                          reinterpret_cast<std::uint64_t>(imageBase), error)) {
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }

    // ---- 5. parse imports / TLS from the fixed image ----
    if (!ParseImports(image, pe, error)) {
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }
    std::vector<std::uintptr_t> tlsCallbacks;
    if (!ParseTlsCallbacks(image, pe.preferredBase, tlsCallbacks, error)) {
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }

    // ---- 6. prepare ntdll function addresses (same base in every process) ----
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto getNt = [&](const char* name) -> std::uint64_t {
        return reinterpret_cast<std::uint64_t>(GetProcAddress(ntdll, name));
    };
    const std::uint64_t fnLdrLoadDll = getNt("LdrLoadDll");
    const std::uint64_t fnLdrGetProcedureAddress = getNt("LdrGetProcedureAddress");
    const std::uint64_t fnRtlAddFunctionTable = getNt("RtlAddFunctionTable");
    if (!fnLdrLoadDll || !fnLdrGetProcedureAddress) {
        error = L"could not resolve ntdll export addresses";
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }

    // ---- 7. exception directory ----
    std::uint64_t pRuntimeFunctionTable = 0;
    std::uint32_t runtimeFunctionCount = 0;
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
        const auto& excDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (excDir.VirtualAddress != 0 && excDir.Size >= sizeof(RUNTIME_FUNCTION)) {
            pRuntimeFunctionTable =
                reinterpret_cast<std::uint64_t>(imageBase) + excDir.VirtualAddress;
            runtimeFunctionCount = excDir.Size / static_cast<DWORD>(sizeof(RUNTIME_FUNCTION));
        }
    }

    // ---- 8. assemble the payload blob ----
    std::size_t moduleNamesBytes = 0, funcNamesBytes = 0;
    const std::size_t codeSize = ShellcodeSize();
    // The shellcode is a few hundred bytes. Anything outside this range means
    // the linker mislaid the codeStart/codeEnd labels (e.g. /INCREMENTAL
    // routing the address references through ILT thunks - see the vcxproj and
    // CMakeLists LinkIncremental settings); copying a bogus range would crash
    // the injector, so fail cleanly instead.
    if (codeSize < 0x40 || codeSize > 0x10000) {
        error = L"invalid shellcode size (" + std::to_wstring(codeSize) +
                L" bytes): codeStart/codeEnd are mislaid. Link the Injector "
                L"with /INCREMENTAL:NO.";
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }
    const PayloadLayout layout =
        ComputeLayout(pe, codeSize, tlsCallbacks.size(), moduleNamesBytes, funcNamesBytes);

    const LPVOID payloadBase =
        VirtualAllocEx(process, nullptr, layout.total, MEM_RESERVE | MEM_COMMIT,
                       PAGE_EXECUTE_READWRITE);
    if (!payloadBase) {
        error = Err(L"VirtualAllocEx(payload) failed");
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }
    const std::uint64_t P = reinterpret_cast<std::uint64_t>(payloadBase);

    std::vector<std::uint8_t> payload(layout.total, 0);

    // shellcode bytes
    std::memcpy(payload.data() + layout.shellcode, codeStart, codeSize);

    // params
    auto* params = reinterpret_cast<PayloadParams*>(payload.data() + layout.params);
    params->imageBase = reinterpret_cast<std::uint64_t>(imageBase);
    params->entryPoint = pe.entryRva
                             ? reinterpret_cast<std::uint64_t>(imageBase) + pe.entryRva
                             : 0;
    params->fnLdrLoadDll = fnLdrLoadDll;
    params->fnLdrGetProcedureAddress = fnLdrGetProcedureAddress;
    params->fnRtlAddFunctionTable = fnRtlAddFunctionTable;
    params->pRuntimeFunctionTable = pRuntimeFunctionTable;
    params->runtimeFunctionCount = runtimeFunctionCount;
    params->pTlsCallbacks = tlsCallbacks.empty() ? 0 : P + layout.tlsCallbacks;
    params->tlsCallbackCount = tlsCallbacks.size();
    params->importCount = pe.imports.size();
    params->pImportEntries = P + layout.importEntries;
    params->pStatus = P + layout.status;

    // import entries + thunks + names
    auto* entry = reinterpret_cast<ImportEntry*>(payload.data() + layout.importEntries);
    auto* thunk = reinterpret_cast<Thunk*>(payload.data() + layout.thunks);
    std::uint8_t* modNamePtr = payload.data() + layout.moduleNames;
    std::uint8_t* funcNamePtr = payload.data() + layout.funcNames;

    for (const auto& dll : pe.imports) {
        entry->pModuleNameW = P + static_cast<std::size_t>(modNamePtr - payload.data());
        std::size_t n = 0;
        for (; n < dll.name.size(); ++n)
            reinterpret_cast<wchar_t*>(modNamePtr)[n] = static_cast<wchar_t>(dll.name[n]);
        reinterpret_cast<wchar_t*>(modNamePtr)[n] = L'\0';
        modNamePtr += (dll.name.size() + 1) * sizeof(wchar_t);

        entry->thunkCount = dll.thunks.size();
        const std::size_t thunkIndex =
            static_cast<std::size_t>(thunk - reinterpret_cast<Thunk*>(payload.data() + layout.thunks));
        entry->pThunks = P + layout.thunks + thunkIndex * sizeof(Thunk);

        for (const auto& t : dll.thunks) {
            thunk->pIatSlot =
                reinterpret_cast<std::uint64_t>(imageBase) + t.iatSlotRva;
            if (!t.funcName.empty()) {
                thunk->pFuncName =
                    P + static_cast<std::size_t>(funcNamePtr - payload.data());
                std::memcpy(funcNamePtr, t.funcName.c_str(), t.funcName.size() + 1);
                funcNamePtr += t.funcName.size() + 1;
                thunk->ordinal = 0;
            } else {
                thunk->pFuncName = 0;
                thunk->ordinal = t.ordinal;
            }
            ++thunk;
        }
        ++entry;
    }

    // TLS callback addresses (absolute target VAs, already rebased)
    if (!tlsCallbacks.empty()) {
        std::memcpy(payload.data() + layout.tlsCallbacks, tlsCallbacks.data(),
                    tlsCallbacks.size() * sizeof(std::uint64_t));
    }

    // ---- 9. write payload + image into the target ----
    if (!WriteProcessMemory(process, payloadBase, payload.data(), layout.total, nullptr)) {
        error = Err(L"WriteProcessMemory(payload) failed");
        VirtualFreeEx(process, payloadBase, 0, MEM_RELEASE);
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }
    if (!WriteProcessMemory(process, imageBase, image.data(), image.size(), nullptr)) {
        error = Err(L"WriteProcessMemory(image) failed");
        VirtualFreeEx(process, payloadBase, 0, MEM_RELEASE);
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }

    // ---- 10. run the payload in the target ----
    HANDLE hThread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(P + layout.shellcode),
        reinterpret_cast<LPVOID>(P + layout.params), 0, nullptr);
    if (!hThread) {
        error = Err(L"CreateRemoteThread failed");
        VirtualFreeEx(process, payloadBase, 0, MEM_RELEASE);
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait = WaitForSingleObject(hThread, 60000);
    const bool success = (wait == WAIT_OBJECT_0);

    std::uint64_t status = 0;
    if (success)
        ReadProcessMemory(process, reinterpret_cast<LPCVOID>(P + layout.status), &status,
                          sizeof(status), nullptr);
    else
        error = wait == WAIT_TIMEOUT ? L"payload timed out in target"
                                     : L"payload thread failed in target";

    CloseHandle(hThread);
    VirtualFreeEx(process, payloadBase, 0, MEM_RELEASE);
    if (!success) {
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }
    if (status != 1) {
        error = L"DllMain returned FALSE or import resolution failed in target";
        VirtualFreeEx(process, imageBase, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

}  // namespace mm
