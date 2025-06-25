/*
 * dbj_ping.c - Ping Countermeasures DLL
 * C17 with SEH, External INI Configuration, and Windows Event Logging
 * Compile with: cl /std:c17 /EHa /kernel /utf-8 /DDBJ_PING_EXPORTS /LD dbj_ping.c /link ws2_32.lib iphlpapi.lib /DEF:dbj_ping.def /OUT:dbj_ping.dll
 */

#pragma region Headers_and_Definitions

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <float.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

#define MAX_TARGET_LEN 256
#define MAX_LOG_MSG 0xFF
#define PING_DATA_SIZE 32
#define MAX_BACKUP_DNS 8

// Log levels
typedef enum {
    LOG_INFO = 0,
    LOG_WARNING = 1,
    LOG_ERROR = 2,
    LOG_CRITICAL = 3
} log_kind_t;

// Configuration structure
typedef struct {
    char target[MAX_TARGET_LEN];
    DWORD timeout_ms;
    DWORD interval_ms;
    DWORD loss_threshold;
    DWORD latency_threshold;
    DWORD jitter_threshold;
    DWORD max_retries;
    bool enable_countermeasures;
    bool enable_dns_switching;
    bool enable_route_refresh;
    bool enable_logging;
    char backup_dns[MAX_BACKUP_DNS][16];
    DWORD backup_dns_count;
} ping_config_t;

// Ping statistics
typedef struct {
    DWORD packets_sent;
    DWORD packets_received;
    DWORD packets_lost;
    double min_rtt;
    double max_rtt;
    double avg_rtt;
    double jitter;
    bool countermeasures_active;
    DWORD current_dns_index;
    SYSTEMTIME last_countermeasure;
} ping_stats_t;

// Ping result structure
typedef struct {
    bool success;
    DWORD rtt_ms;
    DWORD status;
    char target_ip[16];
    SYSTEMTIME timestamp;
} ping_result_t;

// DLL Function Declarations
#ifdef DBJ_PING_EXPORTS
#define PING_API __declspec(dllexport)
#else
#define PING_API __declspec(dllimport)
#endif

#pragma endregion

#pragma region Global_Variables_and_Defaults

// Global configuration and stats
static ping_config_t g_config = {0};
static ping_stats_t g_stats = {0};
static HANDLE g_icmp_handle = INVALID_HANDLE_VALUE;
static bool g_initialized = false;
static CRITICAL_SECTION g_cs;

// Default configuration values
static const ping_config_t DEFAULT_CONFIG = {
    .target = "8.8.8.8",
    .timeout_ms = 3000,
    .interval_ms = 1000,
    .loss_threshold = 30,
    .latency_threshold = 500,
    .jitter_threshold = 100,
    .max_retries = 3,
    .enable_countermeasures = true,
    .enable_dns_switching = true,
    .enable_route_refresh = true,
    .enable_logging = true,
    .backup_dns = {
        "8.8.8.8", "1.1.1.1", "9.9.9.9", "208.67.222.222",
        "8.8.4.4", "1.0.0.1", "149.112.112.112", "208.67.220.220"
    },
    .backup_dns_count = 8
};

#pragma endregion

#pragma region Function_Prototypes

void dbj_log(log_kind_t kind, const char msg[MAX_LOG_MSG], ...);
static bool load_configuration(void);
static bool save_configuration(void);
static bool create_default_config(void);
static void init_stats(void);
static DWORD resolve_hostname(const char* hostname, char* ip_buffer, size_t buffer_size);
static bool perform_ping(const char* target, ping_result_t* result);
static void analyze_network_health(void);
static void trigger_countermeasures(void);
static bool switch_dns_server(void);
static bool refresh_network_route(void);
static bool flush_dns_cache(void);

#pragma endregion

#pragma region Logging_Implementation

// Logging function targeting Windows Event Log
void dbj_log(log_kind_t kind, const char msg[MAX_LOG_MSG], ...) {
    int result = 0;
    __try {
        // Always log during initialization, check config only if initialized
        // if (g_initialized && !g_config.enable_logging) __leave;

        char formatted_msg[MAX_LOG_MSG];
        va_list args;
        va_start(args, msg);
        vsnprintf_s(formatted_msg, sizeof(formatted_msg), _TRUNCATE, msg, args);
        va_end(args);

        // Use Application log with generic source (more reliable)
        HANDLE event_source = RegisterEventSourceA(NULL, "Application");
        if (event_source != NULL) {
            WORD event_type;
            switch (kind) {
            case LOG_ERROR:
            case LOG_CRITICAL:
                event_type = EVENTLOG_ERROR_TYPE;
                break;
            case LOG_WARNING:
                event_type = EVENTLOG_WARNING_TYPE;
                break;
            default:
                event_type = EVENTLOG_INFORMATION_TYPE;
                break;
            }

            LPCSTR messages[] = { formatted_msg };
            // Using Event ID 0 will show the raw message without needing a message template.
            // this was wrong: ReportEventA(event_source, event_type, 0, 1000 + kind, NULL, 1, 0, messages, NULL);
            ReportEventA(event_source, event_type, 0, 0, NULL, 1, 0, messages, NULL);
            DeregisterEventSource(event_source);
        }

        // Also output to debug console in debug builds
#ifdef _DEBUG
        const char* level_str[] = { "INFO", "WARN", "ERROR", "CRITICAL" };
        printf("[dbj_ping %s] %s\n", level_str[kind], formatted_msg);
#endif

        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

#pragma endregion

#pragma region Configuration_Management

// Global variable for config path
static char g_config_path[MAX_PATH] = { 0 };

static bool initialize_config_path(void) {
    int result = 0;
    __try {
        HMODULE hModule = GetModuleHandleA("dbj_ping.dll");
        if (!hModule) {
            hModule = GetModuleHandleA(NULL); // Fall back to EXE
        }

        if (GetModuleFileNameA(hModule, g_config_path, sizeof(g_config_path))) {
            // Remove filename, keep directory
            char* last_slash = strrchr(g_config_path, '\\');
            if (last_slash) {
                *(last_slash + 1) = '\0';
            }
            strcat_s(g_config_path, sizeof(g_config_path), "dbj_ping.ini");

            dbj_log(LOG_INFO, "Config file path: %s", g_config_path);
            result = 1;
        }
        else {
            dbj_log(LOG_ERROR, "Failed to get module path for config file");
        }
    }
    __finally {
        // Nothing to cleanup here
    }

    return result != 0;
}

// Load configuration from INI file
static bool load_configuration(void) {
    int result = 0;
    __try {
        // Initialize config path first
        if (!initialize_config_path()) {
            dbj_log(LOG_ERROR, "Failed to initialize configuration path");
            __leave;
        }

        if (GetFileAttributesA(g_config_path) == INVALID_FILE_ATTRIBUTES) {
            dbj_log(LOG_INFO, "Configuration file not found, creating default");
            if (!create_default_config()) {
                dbj_log(LOG_ERROR, "Failed to create default configuration file");
                __leave;
            }
            // Continue to read the newly created configuration file
        }

        // Read configuration values
        g_config.timeout_ms = GetPrivateProfileIntA("Ping", "TimeoutMs", DEFAULT_CONFIG.timeout_ms, g_config_path);
        g_config.interval_ms = GetPrivateProfileIntA("Ping", "IntervalMs", DEFAULT_CONFIG.interval_ms, g_config_path);
        g_config.loss_threshold = GetPrivateProfileIntA("Thresholds", "LossThreshold", DEFAULT_CONFIG.loss_threshold, g_config_path);
        g_config.latency_threshold = GetPrivateProfileIntA("Thresholds", "LatencyThreshold", DEFAULT_CONFIG.latency_threshold, g_config_path);
        g_config.jitter_threshold = GetPrivateProfileIntA("Thresholds", "JitterThreshold", DEFAULT_CONFIG.jitter_threshold, g_config_path);
        g_config.max_retries = GetPrivateProfileIntA("Ping", "MaxRetries", DEFAULT_CONFIG.max_retries, g_config_path);

        g_config.enable_countermeasures = GetPrivateProfileIntA("Features", "EnableCountermeasures", DEFAULT_CONFIG.enable_countermeasures, g_config_path);
        g_config.enable_dns_switching = GetPrivateProfileIntA("Features", "EnableDnsSwitching", DEFAULT_CONFIG.enable_dns_switching, g_config_path);
        g_config.enable_route_refresh = GetPrivateProfileIntA("Features", "EnableRouteRefresh", DEFAULT_CONFIG.enable_route_refresh, g_config_path);
        g_config.enable_logging = GetPrivateProfileIntA("Features", "EnableLogging", DEFAULT_CONFIG.enable_logging, g_config_path);

        GetPrivateProfileStringA("Ping", "Target", DEFAULT_CONFIG.target, g_config.target, sizeof(g_config.target), g_config_path);

        // Load backup DNS servers
        g_config.backup_dns_count = 0;
        for (int i = 0; i < MAX_BACKUP_DNS; i++) {
            char key_name[32];
            snprintf(key_name, sizeof(key_name), "BackupDns%d", i + 1);
            char dns_server[16] = { 0 };
            GetPrivateProfileStringA("DNS", key_name, "", dns_server, sizeof(dns_server), g_config_path);

            if (strlen(dns_server) > 0) {
                strncpy_s(g_config.backup_dns[g_config.backup_dns_count], sizeof(g_config.backup_dns[0]), dns_server, _TRUNCATE);
                g_config.backup_dns_count++;
            }
        }

        if (g_config.backup_dns_count == 0) {
            // Use defaults if none loaded
            for (int i = 0; i < DEFAULT_CONFIG.backup_dns_count; i++) {
                strncpy_s(g_config.backup_dns[i], sizeof(g_config.backup_dns[0]), DEFAULT_CONFIG.backup_dns[i], _TRUNCATE);
            }
            g_config.backup_dns_count = DEFAULT_CONFIG.backup_dns_count;
        }

        dbj_log(LOG_INFO, "Configuration loaded successfully from: %s", g_config_path);
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }

    return result != 0;
}
#ifdef _DEBUG
#define WRITE_INI_OR_FAIL(section, key, value) \
    do { \
        if (!WritePrivateProfileStringA(section, key, value, g_config_path)) { \
            char current_dir[MAX_PATH]; \
            GetCurrentDirectoryA(sizeof(current_dir), current_dir); \
            DWORD file_attrs = GetFileAttributesA(g_config_path); \
            dbj_log(LOG_ERROR, "ERROR : " __FILE__ " : %d", __LINE__ ); \
            dbj_log(LOG_ERROR, "Failed to write INI [%s]%s=%s to %s", \
                   section, key, value, g_config_path); \
            dbj_log(LOG_ERROR, "Current directory: %s, File attributes: 0x%08X", \
                   current_dir, file_attrs); \
result = false ; \
            __leave; \
        } \
    } while(0)
#else // RELEASE
// Error handling macro for WritePrivateProfileStringA
#define WRITE_INI_OR_FAIL(section, key, value) \
    do { \
        if (!WritePrivateProfileStringA(section, key, value, file)) { \
            dbj_log(LOG_ERROR, "Failed to write INI [%s]%s=%s to %s (WritePrivateProfileStringA returned FALSE)", \
                   section, key, value, g_config_path); \
result = false ; \
            __leave; \
        } \
    } while(0)
#endif


// Create default configuration file
static bool create_default_config(void) {
    int result = true;
    __try {
        g_config = DEFAULT_CONFIG;

        // Write default configuration to INI file with error checking
        WRITE_INI_OR_FAIL("Ping", "Target", g_config.target);

        char temp_str[32];
        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.timeout_ms);
        WRITE_INI_OR_FAIL("Ping", "TimeoutMs", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.interval_ms);
        WRITE_INI_OR_FAIL("Ping", "IntervalMs", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.max_retries);
        WRITE_INI_OR_FAIL("Ping", "MaxRetries", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.loss_threshold);
        WRITE_INI_OR_FAIL("Thresholds", "LossThreshold", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.latency_threshold);
        WRITE_INI_OR_FAIL("Thresholds", "LatencyThreshold", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.jitter_threshold);
        WRITE_INI_OR_FAIL("Thresholds", "JitterThreshold", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_countermeasures);
        WRITE_INI_OR_FAIL("Features", "EnableCountermeasures", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_dns_switching);
        WRITE_INI_OR_FAIL("Features", "EnableDnsSwitching", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_route_refresh);
        WRITE_INI_OR_FAIL("Features", "EnableRouteRefresh", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_logging);
        WRITE_INI_OR_FAIL("Features", "EnableLogging", temp_str);

        // Write backup DNS servers
        for (DWORD i = 0; i < g_config.backup_dns_count; i++) {
            char key_name[32];
            sprintf_s(key_name, sizeof(key_name), "BackupDns%lu", i + 1);
            WRITE_INI_OR_FAIL("DNS", key_name, g_config.backup_dns[i]);
        }

        // Write comments to the INI file (these can fail silently)
        WritePrivateProfileStringA(NULL, "; dbj_ping Configuration", NULL, g_config_path);
        WritePrivateProfileStringA(NULL, "; TimeoutMs: Ping timeout in milliseconds", NULL, g_config_path);
        WritePrivateProfileStringA(NULL, "; IntervalMs: Interval between pings in milliseconds", NULL, g_config_path);
        WritePrivateProfileStringA(NULL, "; LossThreshold: Packet loss percentage to trigger countermeasures", NULL, g_config_path);
        WritePrivateProfileStringA(NULL, "; LatencyThreshold: RTT in ms to trigger latency countermeasures", NULL, g_config_path);
        WritePrivateProfileStringA(NULL, "; JitterThreshold: Jitter in ms to trigger stability countermeasures", NULL, g_config_path);

        dbj_log(LOG_INFO, "Default configuration file created: %s", g_config_path);
        result = true;
    }
    __finally {
        // Nothing to cleanup here
    }

    return result ;
}
// Save current configuration to INI file
// Save current configuration to INI file
static bool save_configuration(void) {
    int result = 0;
    __try {
        // Make sure we have a valid config path
        if (strlen(g_config_path) == 0) {
            if (!initialize_config_path()) {
                dbj_log(LOG_ERROR, "Failed to initialize configuration path for saving");
                __leave;
            }
        }

        char temp_str[32];

        WRITE_INI_OR_FAIL("Ping", "Target", g_config.target);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.timeout_ms);
        WRITE_INI_OR_FAIL("Ping", "TimeoutMs", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.interval_ms);
        WRITE_INI_OR_FAIL("Ping", "IntervalMs", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.max_retries);
        WRITE_INI_OR_FAIL("Ping", "MaxRetries", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.loss_threshold);
        WRITE_INI_OR_FAIL("Thresholds", "LossThreshold", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.latency_threshold);
        WRITE_INI_OR_FAIL("Thresholds", "LatencyThreshold", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%lu", g_config.jitter_threshold);
        WRITE_INI_OR_FAIL("Thresholds", "JitterThreshold", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_countermeasures);
        WRITE_INI_OR_FAIL("Features", "EnableCountermeasures", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_dns_switching);
        WRITE_INI_OR_FAIL("Features", "EnableDnsSwitching", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_route_refresh);
        WRITE_INI_OR_FAIL("Features", "EnableRouteRefresh", temp_str);

        sprintf_s(temp_str, sizeof(temp_str), "%d", g_config.enable_logging);
        WRITE_INI_OR_FAIL("Features", "EnableLogging", temp_str);

        // Save backup DNS servers (clear existing ones first)
        for (int i = 1; i <= MAX_BACKUP_DNS; i++) {
            char key_name[32];
            sprintf_s(key_name, sizeof(key_name), "BackupDns%d", i);
            if (i <= (int)g_config.backup_dns_count) {
                WRITE_INI_OR_FAIL("DNS", key_name, g_config.backup_dns[i - 1]);
            }
            else {
                // Clear unused DNS entries
                WritePrivateProfileStringA("DNS", key_name, NULL, g_config_path);
            }
        }

        dbj_log(LOG_INFO, "Configuration saved successfully to: %s", g_config_path);
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }

    return result != 0;
}
#pragma endregion

#pragma region Utility_Functions

// Initialize statistics
static void init_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.min_rtt = DBL_MAX;
    g_stats.max_rtt = 0.0;
    GetSystemTime(&g_stats.last_countermeasure);
}

// Resolve hostname to IP address
static DWORD resolve_hostname(const char* hostname, char* ip_buffer, size_t buffer_size) {
    DWORD result = ERROR_INVALID_PARAMETER;
    struct addrinfo* addrinfo_result = NULL;
    
    __try {
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_RAW;
        
        int ret = getaddrinfo(hostname, NULL, &hints, &addrinfo_result);
        if (ret != 0) {
            dbj_log(LOG_ERROR, "getaddrinfo failed for %s: %d", hostname, ret);
            __leave;
        }
        
        struct sockaddr_in* addr_in = (struct sockaddr_in*)addrinfo_result->ai_addr;
        inet_ntop(AF_INET, &addr_in->sin_addr, ip_buffer, (socklen_t)buffer_size);
        
        result = ERROR_SUCCESS;
    }
    __finally {
        if (addrinfo_result != NULL) {
            freeaddrinfo(addrinfo_result);
        }
    }
    
    return result;
}

#pragma endregion

#pragma region Ping_Implementation

// Perform single ping operation
static bool perform_ping(const char* target, ping_result_t* result) {
    int ping_result = 0;
    LPVOID reply_buffer = NULL;
    
    __try {
        memset(result, 0, sizeof(ping_result_t));
        GetSystemTime(&result->timestamp);
        
        // Resolve target to IP if needed
        char target_ip[16] = {0};
        if (resolve_hostname(target, target_ip, sizeof(target_ip)) != ERROR_SUCCESS) {
            result->success = false;
            result->status = IP_DEST_HOST_UNREACHABLE;
            __leave;
        }
        
        strncpy_s(result->target_ip, sizeof(result->target_ip), target_ip, _TRUNCATE);
        
        // Convert IP string to address
        ULONG dest_addr = inet_addr(target_ip);
        if (dest_addr == INADDR_NONE) {
            result->success = false;
            result->status = IP_BAD_DESTINATION;
            __leave;
        }
        
        // Prepare ping data
        char ping_data[PING_DATA_SIZE];
        memset(ping_data, 0xAA, sizeof(ping_data));
        
        // Allocate reply buffer
        DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + PING_DATA_SIZE;
        reply_buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, reply_size);
        if (!reply_buffer) {
            result->success = false;
            result->status = IP_NO_RESOURCES;
            __leave;
        }
        
        // Perform the ping
        DWORD reply_count = IcmpSendEcho(
            g_icmp_handle,
            dest_addr,
            ping_data,
            sizeof(ping_data),
            NULL,
            reply_buffer,
            reply_size,
            g_config.timeout_ms
        );
        
        if (reply_count > 0) {
            PICMP_ECHO_REPLY echo_reply = (PICMP_ECHO_REPLY)reply_buffer;
            result->success = (echo_reply->Status == IP_SUCCESS);
            result->status = echo_reply->Status;
            result->rtt_ms = echo_reply->RoundTripTime;
        } else {
            result->success = false;
            result->status = GetLastError();
        }
        
        ping_result = result->success ? 1 : 0;
    }
    __finally {
        if (reply_buffer) {
            HeapFree(GetProcessHeap(), 0, reply_buffer);
        }
    }
    
    return ping_result != 0;
}

#pragma endregion

#pragma region Network_Health_Analysis

// Analyze network health and trigger countermeasures if needed
static void analyze_network_health(void) {
    __try {
        if (!g_config.enable_countermeasures || g_stats.countermeasures_active) {
            __leave;
        }
        
        if (g_stats.packets_sent < 10) {
            __leave; // Need more data
        }
        
        // Calculate packet loss percentage
        double loss_percentage = (double)g_stats.packets_lost / g_stats.packets_sent * 100.0;
        
        // Check if countermeasures should be triggered
        bool trigger_needed = false;
        
        if (loss_percentage > g_config.loss_threshold) {
            dbj_log(LOG_WARNING, "High packet loss detected: %.1f%% (threshold: %lu%%)", 
                   loss_percentage, g_config.loss_threshold);
            trigger_needed = true;
        }
        
        if (g_stats.avg_rtt > g_config.latency_threshold) {
            dbj_log(LOG_WARNING, "High latency detected: %.1fms (threshold: %lums)", 
                   g_stats.avg_rtt, g_config.latency_threshold);
            trigger_needed = true;
        }
        
        if (g_stats.jitter > g_config.jitter_threshold) {
            dbj_log(LOG_WARNING, "High jitter detected: %.1fms (threshold: %lums)", 
                   g_stats.jitter, g_config.jitter_threshold);
            trigger_needed = true;
        }
        
        if (trigger_needed) {
            trigger_countermeasures();
        }
    }
    __finally {
        // Nothing to cleanup here
    }
}

#pragma endregion

#pragma region Countermeasures_Implementation

// Trigger countermeasures
static void trigger_countermeasures(void) {
    __try {
        EnterCriticalSection(&g_cs);
        
        if (g_stats.countermeasures_active) {
            LeaveCriticalSection(&g_cs);
            __leave;
        }
        
        g_stats.countermeasures_active = true;
        GetSystemTime(&g_stats.last_countermeasure);
        
        dbj_log(LOG_WARNING, "COUNTERMEASURES ACTIVATED");
        
        bool countermeasures_taken = false;
        
        // DNS switching countermeasure
        if (g_config.enable_dns_switching && switch_dns_server()) {
            dbj_log(LOG_INFO, "Countermeasure: DNS server switched");
            countermeasures_taken = true;
        }
        
        // Route refresh countermeasure
        if (g_config.enable_route_refresh && refresh_network_route()) {
            dbj_log(LOG_INFO, "Countermeasure: Network route refreshed");
            countermeasures_taken = true;
        }
        
        // DNS cache flush countermeasure
        if (flush_dns_cache()) {
            dbj_log(LOG_INFO, "Countermeasure: DNS cache flushed");
            countermeasures_taken = true;
        }
        
        if (!countermeasures_taken) {
            dbj_log(LOG_WARNING, "No countermeasures could be applied");
        }
        
        LeaveCriticalSection(&g_cs);
        
        // Reset countermeasures flag after 30 seconds
        Sleep(30000);
        g_stats.countermeasures_active = false;
    }
    __finally {
        // Ensure we leave critical section on any exit
        if (g_stats.countermeasures_active) {
            g_stats.countermeasures_active = false;
        }
    }
}

// Switch to next backup DNS server
static bool switch_dns_server(void) {
    int result = 0;
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    
    __try {
        if (g_stats.current_dns_index >= g_config.backup_dns_count - 1) {
            g_stats.current_dns_index = 0;
        } else {
            g_stats.current_dns_index++;
        }
        
        const char* new_dns = g_config.backup_dns[g_stats.current_dns_index];
        
        // Attempt to change DNS via netsh (requires elevated privileges)
        char command[256];
        snprintf(command, sizeof(command), 
                "netsh interface ip set dns \"Local Area Connection\" static %s", new_dns);
        
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            __leave;
        }
        
        hProcess = pi.hProcess;
        hThread = pi.hThread;
        
        WaitForSingleObject(pi.hProcess, 5000);
        
        dbj_log(LOG_INFO, "DNS switched to: %s", new_dns);
        result = 1;
    }
    __finally {
        if (hProcess) CloseHandle(hProcess);
        if (hThread) CloseHandle(hThread);
    }
    
    return result != 0;
}

// Refresh network route
static bool refresh_network_route(void) {
    int result = 0;
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    
    __try {
        // Flush ARP table
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        char command[] = "arp -d *";
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            __leave;
        }
        
        hProcess = pi.hProcess;
        hThread = pi.hThread;
        
        WaitForSingleObject(pi.hProcess, 5000);
        result = 1;
    }
    __finally {
        if (hProcess) CloseHandle(hProcess);
        if (hThread) CloseHandle(hThread);
    }
    
    return result != 0;
}

// Flush DNS cache
static bool flush_dns_cache(void) {
    int result = 0;
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    
    __try {
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        char command[] = "ipconfig /flushdns";
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            __leave;
        }
        
        hProcess = pi.hProcess;
        hThread = pi.hThread;
        
        WaitForSingleObject(pi.hProcess, 5000);
        result = 1;
    }
    __finally {
        if (hProcess) CloseHandle(hProcess);
        if (hThread) CloseHandle(hThread);
    }
    
    return result != 0;
}

#pragma endregion

#pragma region DLL_API_Functions

// DLL exported functions
PING_API DWORD __stdcall ping_initialize(void) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    WSADATA wsaData;
    
    __try {
        if (g_initialized) {
            result = ERROR_ALREADY_INITIALIZED;
            __leave;
        }
        
        InitializeCriticalSection(&g_cs);
        
        // Initialize WinSock
        int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsa_result != 0) {
            dbj_log(LOG_ERROR, "WSAStartup failed: %d", wsa_result);
            result = ERROR_NETWORK_UNREACHABLE;
            __leave;
        }
        
        // Create ICMP handle
        g_icmp_handle = IcmpCreateFile();
        if (g_icmp_handle == INVALID_HANDLE_VALUE) {
            dbj_log(LOG_ERROR, "IcmpCreateFile failed: %lu", GetLastError());
            WSACleanup();
            result = GetLastError();
            __leave;
        }
        
        // Load configuration
        if (!load_configuration()) {
            IcmpCloseHandle(g_icmp_handle);
            WSACleanup();
            result = ERROR_INVALID_PARAMETER;
            __leave;
        }
        
        init_stats();
        g_initialized = true;
        
        dbj_log(LOG_INFO, "dbj_ping DLL initialized successfully");
        result = ERROR_SUCCESS;
    }
    __finally {
        // Cleanup handled in __try block
    }
    
    return result;
}

PING_API DWORD __stdcall ping_execute(const char* target, ping_result_t* result) {
    DWORD api_result = ERROR_EXCEPTION_IN_SERVICE;
    
    __try {
        if (!g_initialized) {
            api_result = ERROR_NOT_READY;
            __leave;
        }
        
        if (!target || !result) {
            api_result = ERROR_INVALID_PARAMETER;
            __leave;
        }
        
        // Use configured target if none specified
        const char* ping_target = (strlen(target) > 0) ? target : g_config.target;
        
        bool success = perform_ping(ping_target, result);
        
        // Update statistics
        EnterCriticalSection(&g_cs);
        g_stats.packets_sent++;
        
        if (success) {
            g_stats.packets_received++;
            
            // Update RTT statistics
            double rtt = (double)result->rtt_ms;
            if (rtt < g_stats.min_rtt) g_stats.min_rtt = rtt;
            if (rtt > g_stats.max_rtt) g_stats.max_rtt = rtt;
            
            // Calculate running average
            g_stats.avg_rtt = ((g_stats.avg_rtt * (g_stats.packets_received - 1)) + rtt) / g_stats.packets_received;
            
            // Simple jitter calculation (standard deviation approximation)
            if (g_stats.packets_received > 1) {
                double diff = rtt - g_stats.avg_rtt;
                g_stats.jitter = (g_stats.jitter * 0.9) + (fabs(diff) * 0.1);
            }
        } else {
            g_stats.packets_lost++;
        }
        
        LeaveCriticalSection(&g_cs);
        
        // Analyze network health every 5 pings
        if (g_stats.packets_sent % 5 == 0) {
            analyze_network_health();
        }
        
        api_result = success ? ERROR_SUCCESS : ERROR_NETWORK_UNREACHABLE;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return api_result;
}

PING_API DWORD __stdcall ping_get_stats(ping_stats_t* stats) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    
    __try {
        if (!g_initialized || !stats) {
            result = ERROR_INVALID_PARAMETER;
            __leave;
        }
        
        EnterCriticalSection(&g_cs);
        memcpy(stats, &g_stats, sizeof(ping_stats_t));
        LeaveCriticalSection(&g_cs);
        
        result = ERROR_SUCCESS;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

PING_API DWORD __stdcall ping_get_config(ping_config_t* config) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    
    __try {
        if (!g_initialized || !config) {
            result = ERROR_INVALID_PARAMETER;
            __leave;
        }
        
        memcpy(config, &g_config, sizeof(ping_config_t));
        result = ERROR_SUCCESS;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

PING_API DWORD __stdcall ping_set_config(const ping_config_t* config) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    
    __try {
        if (!g_initialized || !config) {
            result = ERROR_INVALID_PARAMETER;
            __leave;
        }
        
        memcpy(&g_config, config, sizeof(ping_config_t));
        save_configuration();
        
        dbj_log(LOG_INFO, "Configuration updated");
        result = ERROR_SUCCESS;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

PING_API DWORD __stdcall ping_reset_stats(void) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    
    __try {
        if (!g_initialized) {
            result = ERROR_NOT_READY;
            __leave;
        }
        
        EnterCriticalSection(&g_cs);
        init_stats();
        LeaveCriticalSection(&g_cs);
        
        dbj_log(LOG_INFO, "Statistics reset");
        result = ERROR_SUCCESS;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

PING_API DWORD __stdcall ping_force_countermeasures(void) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    
    __try {
        if (!g_initialized) {
            result = ERROR_NOT_READY;
            __leave;
        }
        
        dbj_log(LOG_INFO, "Forcing countermeasures activation");
        trigger_countermeasures();
        result = ERROR_SUCCESS;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

PING_API void __stdcall ping_cleanup(void) {
    __try {
        if (!g_initialized) {
            __leave;
        }
        
        g_initialized = false;
        
        if (g_icmp_handle != INVALID_HANDLE_VALUE) {
            IcmpCloseHandle(g_icmp_handle);
            g_icmp_handle = INVALID_HANDLE_VALUE;
        }
        
        WSACleanup();
        DeleteCriticalSection(&g_cs);
        
        dbj_log(LOG_INFO, "dbj_ping DLL cleaned up");
    }
    __finally {
        // Nothing to cleanup here
    }
}

#pragma endregion

#pragma region DLL_Entry_Point

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    __try {
        switch (ul_reason_for_call) {
            case DLL_PROCESS_ATTACH:
                // Auto-initialize on process attach
                break;
            case DLL_THREAD_ATTACH:
                break;
            case DLL_THREAD_DETACH:
                break;
            case DLL_PROCESS_DETACH:
                ping_cleanup();
                break;
        }
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return TRUE;
}

#pragma endregion