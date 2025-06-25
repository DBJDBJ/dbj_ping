/*
 * minidump_writer.c - Implementation of minidump creation functionality
 * Place this file in the dbj_ping_test project directory
 */

#pragma region Headers_and_Definitions

#include "minidump_writer.h"
#include <dbghelp.h>
#include <stdio.h>
#include <time.h>

#pragma comment(lib, "dbghelp.lib")

static bool g_minidump_initialized = false;
static char g_minidump_directory[MAX_PATH] = {0};

#pragma endregion

#pragma region Utility_Functions

static void create_minidump_filename(char* filename, size_t filename_size, const char* suffix) {
    int result = 0;
    __try {
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        char process_name[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, process_name, sizeof(process_name));
        
        // Extract just the process name without path and extension
        char* name_start = strrchr(process_name, '\\');
        if (name_start) {
            name_start++;
        } else {
            name_start = process_name;
        }
        
        char* ext = strrchr(name_start, '.');
        if (ext) {
            *ext = '\0';
        }
        
        _snprintf_s(filename, filename_size, _TRUNCATE,
                   "%s\\%s_%s_%04d%02d%02d_%02d%02d%02d.dmp",
                   g_minidump_directory,
                   name_start,
                   suffix ? suffix : "crash",
                   st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond);
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

static bool ensure_minidump_directory(void) {
    int result = 0;
    __try {
        // Get current directory
        if (!GetCurrentDirectoryA(sizeof(g_minidump_directory), g_minidump_directory)) {
            strcpy_s(g_minidump_directory, sizeof(g_minidump_directory), ".");
        }
        
        // Append minidumps subdirectory
        strcat_s(g_minidump_directory, sizeof(g_minidump_directory), "\\minidumps");
        
        // Create directory if it doesn't exist
        if (GetFileAttributesA(g_minidump_directory) == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryA(g_minidump_directory, NULL)) {
                // Fall back to current directory
                GetCurrentDirectoryA(sizeof(g_minidump_directory), g_minidump_directory);
            }
        }
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result != 0;
}

#pragma endregion

#pragma region Minidump_Creation

static bool create_minidump_internal(HANDLE process, DWORD process_id, HANDLE thread, 
                                    EXCEPTION_POINTERS* exception_info, const char* suffix) {
    int result = 0;
    HANDLE dump_file = INVALID_HANDLE_VALUE;
    
    __try {
        char dump_filename[MAX_PATH];
        create_minidump_filename(dump_filename, sizeof(dump_filename), suffix);
        
        dump_file = CreateFileA(dump_filename,
                               GENERIC_WRITE,
                               0,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
        
        if (dump_file == INVALID_HANDLE_VALUE) {
            printf("Failed to create minidump file: %s (error: %lu)\n", dump_filename, GetLastError());
            __leave;
        }
        
        MINIDUMP_EXCEPTION_INFORMATION mei = {0};
        PMINIDUMP_EXCEPTION_INFORMATION pmei = NULL;
        
        if (exception_info) {
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = exception_info;
            mei.ClientPointers = FALSE;
            pmei = &mei;
        }
        
        MINIDUMP_TYPE dump_type = MiniDumpWithIndirectlyReferencedMemory |
                                 MiniDumpScanMemory |
                                 MiniDumpWithUnloadedModules |
                                 MiniDumpWithProcessThreadData;
        
        BOOL dump_result = MiniDumpWriteDump(process,
                                           process_id,
                                           dump_file,
                                           dump_type,
                                           pmei,
                                           NULL,
                                           NULL);
        
        if (dump_result) {
            printf("Minidump created: %s\n", dump_filename);
            result = 1;
        } else {
            printf("MiniDumpWriteDump failed: error %lu\n", GetLastError());
        }
    }
    __finally {
        if (dump_file != INVALID_HANDLE_VALUE) {
            CloseHandle(dump_file);
        }
    }
    
    return result != 0;
}

#pragma endregion

#pragma region Public_API

bool minidump_initialize(void) {
    int result = 0;
    __try {
        if (g_minidump_initialized) {
            result = 1;
            __leave;
        }
        
        if (!ensure_minidump_directory()) {
            printf("Warning: Failed to create minidump directory\n");
        }
        
        g_minidump_initialized = true;
        printf("Minidump writer initialized. Dumps will be saved to: %s\n", g_minidump_directory);
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result != 0;
}

bool minidump_create_on_exception(EXCEPTION_POINTERS* exception_info) {
    int result = 0;
    __try {
        if (!g_minidump_initialized) {
            minidump_initialize();
        }
        
        HANDLE current_process = GetCurrentProcess();
        DWORD current_process_id = GetCurrentProcessId();
        HANDLE current_thread = GetCurrentThread();
        
        result = create_minidump_internal(current_process, current_process_id, 
                                        current_thread, exception_info, "exception") ? 1 : 0;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result != 0;
}

bool minidump_create_manual(const char* reason) {
    int result = 0;
    __try {
        if (!g_minidump_initialized) {
            minidump_initialize();
        }
        
        HANDLE current_process = GetCurrentProcess();
        DWORD current_process_id = GetCurrentProcessId();
        HANDLE current_thread = GetCurrentThread();
        
        const char* suffix = reason ? reason : "manual";
        result = create_minidump_internal(current_process, current_process_id, 
                                        current_thread, NULL, suffix) ? 1 : 0;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result != 0;
}

void minidump_cleanup(void) {
    __try {
        if (g_minidump_initialized) {
            g_minidump_initialized = false;
            memset(g_minidump_directory, 0, sizeof(g_minidump_directory));
        }
    }
    __finally {
        // Nothing to cleanup here
    }
}

#pragma endregion