/*
 * dbj_ping.h - Header file for dbj_ping DLL
 * Place this file in the dbj_ping project directory
 */

#ifndef DBJ_PING_H
#define DBJ_PING_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Constants
#define MAX_TARGET_LEN 256
#define MAX_BACKUP_DNS 8
#define MAX_LOG_MSG 0xFF

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

// Initialize the ping subsystem
PING_API DWORD __stdcall ping_initialize(void);

// Execute a single ping
PING_API DWORD __stdcall ping_execute(const char* target, ping_result_t* result);

// Get current statistics
PING_API DWORD __stdcall ping_get_stats(ping_stats_t* stats);

// Get current configuration
PING_API DWORD __stdcall ping_get_config(ping_config_t* config);

// Set new configuration
PING_API DWORD __stdcall ping_set_config(const ping_config_t* config);

// Reset statistics
PING_API DWORD __stdcall ping_reset_stats(void);

// Force countermeasures activation (for testing)
PING_API DWORD __stdcall ping_force_countermeasures(void);

// Cleanup and release resources
PING_API void __stdcall ping_cleanup(void);

// Logging function (must be implemented by user)
void dbj_log(log_kind_t kind, const char msg[MAX_LOG_MSG], ...);

// Error codes (Windows standard + custom)
#define PING_ERROR_BASE 0x80040000
#define PING_ERROR_NETWORK_UNREACHABLE (PING_ERROR_BASE + 1)
#define PING_ERROR_TIMEOUT (PING_ERROR_BASE + 2)
#define PING_ERROR_INVALID_CONFIG (PING_ERROR_BASE + 3)

#ifdef __cplusplus
}
#endif

#endif // DBJ_PING_H

