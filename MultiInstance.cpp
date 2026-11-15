namespace GOTHIC_ENGINE {
    // Test-only: lets several copies of the game run on one PC for automated QA.
    // Active only with "autotest": { "allowMultiInstance": true } in GothicCoopConfig.json.
    // Gothic2.exe refuses a second copy through a named file mapping "Lock_<app name>"
    // (and looks for an existing window). We patch the exe's import table so the lock name
    // gets this process id appended, and log FindWindowA calls to see what they look for.

    typedef HANDLE(WINAPI* CreateFileMappingA_t)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
    typedef HWND(WINAPI* FindWindowA_t)(LPCSTR, LPCSTR);
    static CreateFileMappingA_t RealCreateFileMappingA = NULL;
    static FindWindowA_t RealFindWindowA = NULL;

    static void MultiInstanceLog(const std::string& line) {
        std::ofstream f(std::string(GothicExeFolderPath) + "\\GothicCoopTest.log", std::ios_base::app | std::ios_base::out);
        f << line << "\n";
    }

    static HANDLE WINAPI HookCreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD hi, DWORD lo, LPCSTR name) {
        if (name && strncmp(name, "Lock_", 5) == 0) {
            std::string unique = std::string(name) + "_" + std::to_string(GetCurrentProcessId());
            json j; j["event"] = "multi_instance_lock"; j["from"] = name; j["to"] = unique;
            MultiInstanceLog(j.dump());
            return RealCreateFileMappingA(file, sa, protect, hi, lo, unique.c_str());
        }
        return RealCreateFileMappingA(file, sa, protect, hi, lo, name);
    }

    typedef HANDLE(WINAPI* CreateMutexA_t)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
    static CreateMutexA_t RealCreateMutexA = NULL;

    static HANDLE WINAPI HookCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name) {
        if (!name) {
            return RealCreateMutexA(sa, owner, name);
        }
        std::string unique = std::string(name) + "_" + std::to_string(GetCurrentProcessId());
        json j; j["event"] = "multi_instance_mutex"; j["from"] = name; j["to"] = unique;
        MultiInstanceLog(j.dump());
        return RealCreateMutexA(sa, owner, unique.c_str());
    }

    static HWND WINAPI HookFindWindowA(LPCSTR cls, LPCSTR title) {
        HWND result = RealFindWindowA(cls, title);
        json j; j["event"] = "find_window";
        j["class"] = (cls && HIWORD(cls)) ? std::string(cls) : std::string("");
        j["title"] = title ? std::string(title) : std::string("");
        j["found"] = result != NULL;
        MultiInstanceLog(j.dump());
        // Another copy's window must not stop this one from starting.
        return NULL;
    }

    static bool PatchImport(HMODULE module, const char* dll, const char* func, void* hook, void** original) {
        auto base = (BYTE*)module;
        auto dos = (IMAGE_DOS_HEADER*)base;
        auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) {
            return false;
        }

        for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
            if (_stricmp((char*)(base + imp->Name), dll) != 0) {
                continue;
            }

            auto names = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            auto thunks = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
            for (; names->u1.AddressOfData; names++, thunks++) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                    continue;
                }
                auto byName = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
                if (strcmp((char*)byName->Name, func) != 0) {
                    continue;
                }

                DWORD old;
                VirtualProtect(&thunks->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                *original = (void*)thunks->u1.Function;
                thunks->u1.Function = (decltype(thunks->u1.Function))hook;
                VirtualProtect(&thunks->u1.Function, sizeof(void*), old, &old);
                return true;
            }
        }
        return false;
    }

    // --- Test-only diagnostics: who ends the process, and which fatal exceptions happen. ---
    typedef void(WINAPI* ExitProcess_t)(UINT);
    static ExitProcess_t RealExitProcess = NULL;
    static volatile LONG DiagExceptionCount = 0;

    static std::string DiagAddress(void* addr) {
        HMODULE mod = NULL;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod) && mod) {
            GetModuleFileNameA(mod, name, MAX_PATH);
            char* base = strrchr(name, '\\');
            char buf[MAX_PATH + 32];
            sprintf_s(buf, "%s+0x%X", base ? base + 1 : name, (unsigned)((BYTE*)addr - (BYTE*)mod));
            return buf;
        }
        char buf[32];
        sprintf_s(buf, "0x%p", addr);
        return buf;
    }

    static json DiagStack(int skip) {
        void* frames[24];
        USHORT n = CaptureStackBackTrace(skip, 24, frames, NULL);
        json st = json::array();
        for (USHORT i = 0; i < n; i++) {
            st.push_back(DiagAddress(frames[i]));
        }
        return st;
    }

    static void WINAPI HookExitProcess(UINT code) {
        json j; j["event"] = "exit_process"; j["code"] = code; j["thread"] = GetCurrentThreadId();
        j["mainThread"] = MainThreadId; j["stack"] = DiagStack(1);
        MultiInstanceLog(j.dump());
        RealExitProcess(code);
    }

    static LONG CALLBACK DiagExceptionHandler(EXCEPTION_POINTERS* info) {
        DWORD code = info->ExceptionRecord->ExceptionCode;
        bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == 0xC0000374 /* heap corruption */
            || code == 0xC0000409 /* stack buffer overrun */ || code == EXCEPTION_STACK_OVERFLOW
            || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == 0xE06D7363 /* C++ throw */;
        if (fatal && InterlockedIncrement(&DiagExceptionCount) <= 30) {
            char c[16];
            sprintf_s(c, "0x%08X", (unsigned)code);
            json j; j["event"] = "exception"; j["code"] = c;
            j["at"] = DiagAddress(info->ExceptionRecord->ExceptionAddress);
            j["thread"] = GetCurrentThreadId(); j["mainThread"] = MainThreadId;
            if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
                j["write"] = info->ExceptionRecord->ExceptionInformation[0] == 1;
                char a[16];
                sprintf_s(a, "0x%08X", (unsigned)info->ExceptionRecord->ExceptionInformation[1]);
                j["address"] = a;
            }
            j["stack"] = DiagStack(1);
            MultiInstanceLog(j.dump());
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Watchdog: when Game_Loop has not run for 3 s, log where the main thread is.
    static volatile long long DiagLastLoopMs = 0;
    static HANDLE DiagMainThread = NULL;

    // While the main thread is suspended nothing here may take a lock or allocate (it may hold
    // the heap or loader lock): copy EIP and the top of its stack into static memory, resume,
    // and only then turn the copy into text.
    static DWORD DiagStackCopy[1024];

    static bool DiagIsCodeAddress(DWORD v) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)(DWORD_PTR)v, &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) {
            return false;
        }
        return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    static json DiagWalkThread(HANDLE thread) {
        json st = json::array();
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_CONTROL;
        DWORD words = 0;
        if (SuspendThread(thread) == (DWORD)-1) {
            return st;
        }
        if (GetThreadContext(thread, &ctx)) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((void*)(DWORD_PTR)ctx.Esp, &mbi, sizeof(mbi))) {
                DWORD_PTR end = (DWORD_PTR)mbi.BaseAddress + mbi.RegionSize;
                DWORD_PTR bytes = end - ctx.Esp;
                if (bytes > sizeof(DiagStackCopy)) {
                    bytes = sizeof(DiagStackCopy);
                }
                memcpy(DiagStackCopy, (void*)(DWORD_PTR)ctx.Esp, bytes);
                words = (DWORD)(bytes / 4);
            }
        }
        ResumeThread(thread);

        st.push_back(std::string("eip ") + DiagAddress((void*)(DWORD_PTR)ctx.Eip));
        for (DWORD i = 0; i < words && st.size() < 32; i++) {
            if (DiagIsCodeAddress(DiagStackCopy[i])) {
                st.push_back(DiagAddress((void*)(DWORD_PTR)DiagStackCopy[i]));
            }
        }
        return st;
    }

    static DWORD WINAPI DiagWatchdog(LPVOID) {
        long long reportedFor = 0;
        int reports = 0;
        while (true) {
            Sleep(1000);
            long long last = DiagLastLoopMs;
            if (!last || !DiagMainThread) {
                continue;
            }
            long long now = GetCurrentMs();
            if (now - last > 3000 && reports < 40) {
                // one stack per second while it lasts, at most 40 in total
                json j; j["event"] = "main_thread_stuck"; j["forMs"] = now - last; j["wall"] = now;
                j["stack"] = DiagWalkThread(DiagMainThread);
                MultiInstanceLog(j.dump());
                reports++;
                reportedFor = last;
            }
        }
        return 0;
    }

    void DiagLoopTick() {
        if (!DiagMainThread) {
            DiagMainThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
        }
        DiagLastLoopMs = GetCurrentMs();
    }

    void InstallDiagnostics() {
        if (!CoopConfig.contains("autotest")) {
            return;
        }
        AddVectoredExceptionHandler(1, DiagExceptionHandler);
        CreateThread(NULL, 0, DiagWatchdog, NULL, 0, NULL);
        bool e = PatchImport(GetModuleHandleA(NULL), "kernel32.dll", "ExitProcess", (void*)&HookExitProcess, (void**)&RealExitProcess);
        json j; j["event"] = "diagnostics"; j["exitProcessHook"] = e;
        MultiInstanceLog(j.dump());
    }

    void InstallMultiInstanceHooks() {
        InstallDiagnostics();
        if (!CoopConfig.contains("autotest") || !CoopConfig["autotest"].value("allowMultiInstance", false)) {
            return;
        }

        HMODULE exe = GetModuleHandleA(NULL);
        bool a = PatchImport(exe, "kernel32.dll", "CreateFileMappingA", (void*)&HookCreateFileMappingA, (void**)&RealCreateFileMappingA);
        bool b = PatchImport(exe, "user32.dll", "FindWindowA", (void*)&HookFindWindowA, (void**)&RealFindWindowA);

        bool c = PatchImport(exe, "kernel32.dll", "CreateMutexA", (void*)&HookCreateMutexA, (void**)&RealCreateMutexA);

        json j; j["event"] = "multi_instance_hooks"; j["createFileMapping"] = a; j["findWindow"] = b; j["createMutex"] = c;
        MultiInstanceLog(j.dump());
    }
}
