/*
 * dbj_ping_test.c - Test application for dbj_ping DLL
 * Demonstrates proper SEH pattern with minidump creation
 * Compile with: /std:c17 /EHa /utf-8
 */

#pragma region Headers_and_Definitions

#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <time.h>
#include <stdarg.h>
#include "dbj_ping.h"
#include "minidump_writer.h"

#pragma comment(lib, "dbj_ping.lib")

#define TEST_TARGET_DEFAULT "8.8.8.8"
#define STATS_DISPLAY_INTERVAL 10

#pragma endregion

#pragma region Logging_Implementation

// Implementation of dbj_log for test application
void dbj_log(log_kind_t kind, const char msg[MAX_LOG_MSG], ...) {
    int result = 0;
    __try {
        const char* level_str[] = {"INFO", "WARN", "ERROR", "CRITICAL"};
        
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        printf("[%02d:%02d:%02d.%03d] %s: ", 
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
               level_str[kind]);
        
        va_list args;
        va_start(args, msg);
        vprintf(msg, args);
        va_end(args);
        printf("\n");
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

#pragma endregion

#pragma region Display_Functions

static void print_config(const ping_config_t* config) {
    int result = 0;
    __try {
        printf("\n=== Current Configuration ===\n");
        printf("Target: %s\n", config->target);
        printf("Timeout: %lu ms\n", config->timeout_ms);
        printf("Interval: %lu ms\n", config->interval_ms);
        printf("Loss Threshold: %lu%%\n", config->loss_threshold);
        printf("Latency Threshold: %lu ms\n", config->latency_threshold);
        printf("Jitter Threshold: %lu ms\n", config->jitter_threshold);
        printf("Max Retries: %lu\n", config->max_retries);
        printf("Countermeasures: %s\n", config->enable_countermeasures ? "Enabled" : "Disabled");
        printf("DNS Switching: %s\n", config->enable_dns_switching ? "Enabled" : "Disabled");
        printf("Route Refresh: %s\n", config->enable_route_refresh ? "Enabled" : "Disabled");
        printf("Logging: %s\n", config->enable_logging ? "Enabled" : "Disabled");
        printf("Backup DNS Servers: %lu configured\n", config->backup_dns_count);
        
        for (DWORD i = 0; i < config->backup_dns_count && i < 4; i++) {
            printf("  DNS %lu: %s\n", i + 1, config->backup_dns[i]);
        }
        if (config->backup_dns_count > 4) {
            printf("  ... and %lu more\n", config->backup_dns_count - 4);
        }
        printf("==============================\n\n");
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

static void print_stats(const ping_stats_t* stats) {
    int result = 0;
    __try {
        double loss_percent = (stats->packets_sent > 0) ? 
            ((double)stats->packets_lost / stats->packets_sent * 100.0) : 0.0;
        
        printf("\n=== Ping Statistics ===\n");
        printf("Packets Sent: %lu\n", stats->packets_sent);
        printf("Packets Received: %lu\n", stats->packets_received);
        printf("Packets Lost: %lu (%.1f%%)\n", stats->packets_lost, loss_percent);
        
        if (stats->packets_received > 0) {
            printf("RTT Min/Avg/Max: %.1f/%.1f/%.1f ms\n", 
                   stats->min_rtt, stats->avg_rtt, stats->max_rtt);
            printf("Jitter: %.1f ms\n", stats->jitter);
        }
        
        printf("Countermeasures Active: %s\n", stats->countermeasures_active ? "Yes" : "No");
        printf("Current DNS Index: %lu\n", stats->current_dns_index);
        printf("Last Countermeasure: %02d/%02d/%04d %02d:%02d:%02d\n",
               stats->last_countermeasure.wMonth,
               stats->last_countermeasure.wDay,
               stats->last_countermeasure.wYear,
               stats->last_countermeasure.wHour,
               stats->last_countermeasure.wMinute,
               stats->last_countermeasure.wSecond);
        printf("=======================\n\n");
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

static void print_result(const ping_result_t* ping_result) {
    int result = 0;
    __try {
        printf("[%02d:%02d:%02d.%03d] ",
               ping_result->timestamp.wHour,
               ping_result->timestamp.wMinute,
               ping_result->timestamp.wSecond,
               ping_result->timestamp.wMilliseconds);
               
        if (ping_result->success) {
            printf("Reply from %s: time=%lu ms\n", ping_result->target_ip, ping_result->rtt_ms);
        } else {
            printf("Request failed to %s: status=0x%08lX\n", ping_result->target_ip, ping_result->status);
        }
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

static void print_help(void) {
    int result = 0;
    __try {
        printf("\n=== dbj_ping Test Application Help ===\n");
        printf("Commands during ping test:\n");
        printf("  'q' + Enter  - Quit application\n");
        printf("  's' + Enter  - Show detailed statistics\n");
        printf("  'c' + Enter  - Force countermeasures activation\n");
        printf("  'r' + Enter  - Reset statistics\n");
        printf("  'h' + Enter  - Show this help\n");
        printf("  'p' + Enter  - Pause/Resume pinging\n");
        printf("=====================================\n\n");
        
        result = 1;
    }
    __finally {
        // Nothing to cleanup here
    }
}

#pragma endregion

#pragma region Test_Functions

static DWORD test_basic_functionality(const char* target) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    ping_result_t ping_result = {0};
    
    __try {
        printf("Testing basic ping functionality...\n");
        
        // Test single ping
        DWORD ping_status = ping_execute(target, &ping_result);
        if (ping_status == ERROR_SUCCESS) {
            printf("✓ Basic ping test successful\n");
            print_result(&ping_result);
        } else {
            printf("✗ Basic ping test failed: error %lu\n", ping_status);
            __leave;
        }
        
        // Test statistics retrieval
        ping_stats_t stats;
        DWORD stats_status = ping_get_stats(&stats);
        if (stats_status == ERROR_SUCCESS) {
            printf("✓ Statistics retrieval successful\n");
        } else {
            printf("✗ Statistics retrieval failed: error %lu\n", stats_status);
            __leave;
        }
        
        // Test configuration retrieval
        ping_config_t config;
        DWORD config_status = ping_get_config(&config);
        if (config_status == ERROR_SUCCESS) {
            printf("✓ Configuration retrieval successful\n");
        } else {
            printf("✗ Configuration retrieval failed: error %lu\n", config_status);
            __leave;
        }
        
        result = ERROR_SUCCESS;
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

static DWORD interactive_ping_test(const char* target) {
    DWORD result = ERROR_EXCEPTION_IN_SERVICE;
    DWORD last_ping_time = 0;
    int ping_count = 0;
    bool paused = false;
    ping_config_t config = {0};
    
    __try {
        // Get current configuration
        if (ping_get_config(&config) != ERROR_SUCCESS) {
            printf("Failed to get configuration\n");
            __leave;
        }
        
        printf("Starting interactive ping test to: %s\n", target);
        print_help();
        
        last_ping_time = GetTickCount();
        
        while (true) {
            DWORD current_time = GetTickCount();
            
            // Check for keyboard input
            if (_kbhit()) {
                char ch = _getch();
                switch (ch) {
                    case 'q':
                    case 'Q':
                        printf("\nExiting interactive test...\n");
                        result = ERROR_SUCCESS;
                        __leave;
                        
                    case 's':
                    case 'S': {
                        ping_stats_t stats;
                        if (ping_get_stats(&stats) == ERROR_SUCCESS) {
                            print_stats(&stats);
                        }
                        break;
                    }
                    
                    case 'c':
                    case 'C':
                        printf("\nForcing countermeasures activation...\n");
                        ping_force_countermeasures();
                        break;
                        
                    case 'r':
                    case 'R':
                        printf("\nResetting statistics...\n");
                        ping_reset_stats();
                        ping_count = 0;
                        break;
                        
                    case 'h':
                    case 'H':
                        print_help();
                        break;
                        
                    case 'p':
                    case 'P':
                        paused = !paused;
                        printf("\nPinging %s\n", paused ? "PAUSED" : "RESUMED");
                        break;
                }
            }
            
            // Perform ping at specified interval
            if (!paused && (current_time - last_ping_time >= config.interval_ms)) {
                ping_result_t ping_result;
                DWORD ping_status = ping_execute(target, &ping_result);
                
                ping_count++;
                print_result(&ping_result);
                
                // Print stats every STATS_DISPLAY_INTERVAL pings
                if (ping_count % STATS_DISPLAY_INTERVAL == 0) {
                    ping_stats_t stats;
                    if (ping_get_stats(&stats) == ERROR_SUCCESS) {
                        printf("\n--- Quick Stats (ping #%d) ---\n", ping_count);
                        double loss = (stats.packets_sent > 0) ? 
                            ((double)stats.packets_lost / stats.packets_sent * 100.0) : 0.0;
                        printf("Loss: %.1f%%, Avg RTT: %.1fms, Jitter: %.1fms\n",
                               loss, stats.avg_rtt, stats.jitter);
                        if (stats.countermeasures_active) {
                            printf("*** COUNTERMEASURES ACTIVE ***\n");
                        }
                        printf("------------------------------\n\n");
                    }
                }
                
                last_ping_time = current_time;
            }
            
            Sleep(50); // Small delay to prevent excessive CPU usage
        }
    }
    __finally {
        // Nothing to cleanup here
    }
    
    return result;
}

#pragma endregion

#pragma region Main_Function

int main(int argc, char* argv[]) {
    __try {
        printf("dbj_ping DLL Test Application\n");
        printf("============================\n\n");
        
        // Initialize minidump writer
        if (!minidump_initialize()) {
            printf("Warning: Failed to initialize minidump writer\n");
        }
        
        // Initialize the DLL
        printf("Initializing dbj_ping DLL...\n");
        DWORD init_result = ping_initialize();
        if (init_result != ERROR_SUCCESS) {
            printf("Failed to initialize dbj_ping DLL: error %lu\n", init_result);
            return -1;
        }
        
        printf("✓ dbj_ping DLL initialized successfully.\n\n");
        
        // Get and display current configuration
        ping_config_t config;
        if (ping_get_config(&config) == ERROR_SUCCESS) {
            print_config(&config);
        }
        
        // Determine target
        char target[MAX_TARGET_LEN] = {0};
        if (argc > 1) {
            strncpy_s(target, sizeof(target), argv[1], _TRUNCATE);
        } else {
            strcpy_s(target, sizeof(target), config.target[0] ? config.target : TEST_TARGET_DEFAULT);
        }
        
        printf("Target: %s\n\n", target);
        
        // Run basic functionality test
        DWORD test_result = test_basic_functionality(target);
        if (test_result != ERROR_SUCCESS) {
            printf("Basic functionality test failed\n");
            ping_cleanup();
            minidump_cleanup();
            return -1;
        }
        
        printf("\n✓ All basic tests passed!\n\n");
        
        // Ask user if they want to run interactive test
        printf("Run interactive ping test? (y/n): ");
        char choice = _getch();
        printf("%c\n\n", choice);
        
        if (choice == 'y' || choice == 'Y') {
            DWORD interactive_result = interactive_ping_test(target);
            if (interactive_result != ERROR_SUCCESS) {
                printf("Interactive test encountered an error: %lu\n", interactive_result);
            }
        }
        
        // Final statistics
        printf("\n=== Final Test Results ===\n");
        ping_stats_t final_stats;
        if (ping_get_stats(&final_stats) == ERROR_SUCCESS) {
            print_stats(&final_stats);
        }
        
        printf("Test completed successfully.\n");
        
        // Cleanup
        ping_cleanup();
        minidump_cleanup();
        
        return 0;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // This is the only place where __except is used (in main)
        // Create minidump on unhandled exception
        EXCEPTION_POINTERS* exception_info = GetExceptionInformation();
        
        printf("\n*** UNHANDLED EXCEPTION DETECTED ***\n");
        printf("Exception Code: 0x%08X\n", exception_info->ExceptionRecord->ExceptionCode);
        printf("Exception Address: 0x%p\n", exception_info->ExceptionRecord->ExceptionAddress);
        
        if (minidump_create_on_exception(exception_info)) {
            printf("Minidump created successfully.\n");
        } else {
            printf("Failed to create minidump.\n");
        }
        
        printf("Application will now terminate.\n");
        
        // Cleanup on exception
        ping_cleanup();
        minidump_cleanup();
        
        return -1;
    }
}

#pragma endregion