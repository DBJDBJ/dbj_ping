/*
 * dbj_ping.exe - Command Line Ping Utility
 * Standard ping behavior using dbj_ping DLL
 * Usage: dbj_ping.exe [options] target
 */

#pragma region Headers_and_Definitions

#include <stdio.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <conio.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include "dbj_ping.h"

#pragma comment(lib, "dbj_ping.lib")
#pragma comment(lib, "iphlpapi.lib")

typedef struct {
    char target[256];
    int count;              // -n count
    int timeout;            // -w timeout
    int ttl;                // -i TTL
    int size;               // -l size
    bool no_fragment;       // -f
    bool resolve_addresses; // -a
    bool quiet;             // -q
    bool flood;             // -f (flood mode)
    bool verbose;           // -v
    int interval;           // -i interval (in ms)
    bool infinite;          // continuous ping
    bool help;              // -h or -?
} ping_options_t;

static volatile bool g_interrupted = false;
static ping_options_t g_options = { 0 };
static ping_stats_t g_final_stats = { 0 };

#pragma endregion

#pragma region Signal_Handling

void signal_handler(int signal) {
    if (signal == SIGINT) {
        g_interrupted = true;
        printf("\n");
    }
}

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_interrupted = true;
        printf("\n");
        return TRUE;
    default:
        return FALSE;
    }
}

#pragma endregion

#pragma region Logging_Implementation

// Simple console logging for CLI app
void dbj_log(log_kind_t kind, const char msg[MAX_LOG_MSG], ...) {
    if (g_options.quiet) return;

    va_list args;
    va_start(args, msg);
    if (!g_options.quiet && g_options.verbose) {
        const char* level_str[] = { "INFO", "WARN", "ERROR", "CRITICAL" };
        printf("[%s] ", level_str[kind]);
        vprintf(msg, args);
        printf("\n");
    }
    va_end(args);
}

#pragma endregion

#pragma region Help_and_Usage

void print_usage(void) {
    printf("Usage: dbj_ping [options] target_name\n\n");
    printf("Options:\n");
    printf("    -t             Ping the specified host until stopped\n");
    printf("    -a             Resolve addresses to hostnames\n");
    printf("    -n count       Number of echo requests to send (default: 4)\n");
    printf("    -l size        Send buffer size (default: 32)\n");
    printf("    -f             Set Don't Fragment flag in packet\n");
    printf("    -i TTL         Time To Live (default: 128)\n");
    printf("    -v TTL         Type Of Service (treated as TTL for compatibility)\n");
    printf("    -w timeout     Timeout in milliseconds to wait for each reply (default: 3000)\n");
    printf("    -R             Use routing header to test reverse route\n");
    printf("    -S srcaddr     Source address to use\n");
    printf("    -c count       Number of pings (Unix-style)\n");
    printf("    -i interval    Interval between pings in seconds (Unix-style)\n");
    printf("    -q             Quiet output\n");
    printf("    -v             Verbose output\n");
    printf("    -h, -?, --help Show this help\n\n");
    printf("Examples:\n");
    printf("    dbj_ping google.com\n");
    printf("    dbj_ping -n 10 8.8.8.8\n");
    printf("    dbj_ping -t -i 500 example.com\n");
    printf("    dbj_ping -w 5000 -l 1024 192.168.1.1\n");
}

void print_version(void) {
    printf("dbj_ping version 1.0.0\n");
    printf("Advanced Windows Ping Utility with Countermeasures\n");
    printf("Copyright (c) 2025\n\n");
}

#pragma endregion

#pragma region Command_Line_Parsing

bool parse_arguments(int argc, char* argv[]) {
    // Set defaults
    g_options.count = 4;
    g_options.timeout = 3000;
    g_options.ttl = 128;
    g_options.size = 32;
    g_options.interval = 1000;
    g_options.infinite = false;
    g_options.resolve_addresses = false;
    g_options.no_fragment = false;
    g_options.quiet = false;
    g_options.verbose = false;
    g_options.help = false;

    if (argc < 2) {
        print_usage();
        return false;
    }

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' || argv[i][0] == '/') {
            char* arg = argv[i] + 1;

            if (strcmp(arg, "t") == 0) {
                g_options.infinite = true;
            }
            else if (strcmp(arg, "a") == 0) {
                g_options.resolve_addresses = true;
            }
            else if (strcmp(arg, "f") == 0) {
                g_options.no_fragment = true;
            }
            else if (strcmp(arg, "q") == 0) {
                g_options.quiet = true;
            }
            else if (strcmp(arg, "v") == 0) {
                g_options.verbose = true;
            }
            else if (strcmp(arg, "h") == 0 || strcmp(arg, "?") == 0 || strcmp(arg, "-help") == 0) {
                g_options.help = true;
                return true;
            }
            else if (strcmp(arg, "n") == 0 || strcmp(arg, "c") == 0) {
                if (i + 1 < argc) {
                    g_options.count = atoi(argv[++i]);
                    if (g_options.count <= 0) g_options.count = 4;
                }
                else {
                    printf("Error: -%s requires a number\n", arg);
                    return false;
                }
            }
            else if (strcmp(arg, "l") == 0) {
                if (i + 1 < argc) {
                    g_options.size = atoi(argv[++i]);
                    if (g_options.size < 0) g_options.size = 32;
                    if (g_options.size > 65500) g_options.size = 65500;
                }
                else {
                    printf("Error: -l requires a size\n");
                    return false;
                }
            }
            else if (strcmp(arg, "w") == 0) {
                if (i + 1 < argc) {
                    g_options.timeout = atoi(argv[++i]);
                    if (g_options.timeout < 1) g_options.timeout = 1000;
                }
                else {
                    printf("Error: -w requires a timeout value\n");
                    return false;
                }
            }
            else if (strcmp(arg, "i") == 0) {
                if (i + 1 < argc) {
                    char* endptr;
                    double interval_sec = strtod(argv[++i], &endptr);
                    if (endptr != argv[i] && interval_sec > 0) {
                        g_options.interval = (int)(interval_sec * 1000);
                    }
                    else {
                        g_options.ttl = atoi(argv[i]); // TTL interpretation
                        if (g_options.ttl <= 0) g_options.ttl = 128;
                        if (g_options.ttl > 255) g_options.ttl = 255;
                    }
                }
                else {
                    printf("Error: -i requires a value\n");
                    return false;
                }
            }
            else if (strcmp(arg, "version") == 0) {
                print_version();
                return false;
            }
            else {
                printf("Error: Unknown option -%s\n", arg);
                return false;
            }
        }
        else {
            // This should be the target
            if (strlen(g_options.target) == 0) {
                strncpy_s(g_options.target, sizeof(g_options.target), argv[i], _TRUNCATE);
            }
            else {
                printf("Error: Multiple targets specified\n");
                return false;
            }
        }
    }

    if (strlen(g_options.target) == 0 && !g_options.help) {
        printf("Error: No target specified\n");
        return false;
    }

    return true;
}

#pragma endregion

#pragma region Ping_Execution

void print_ping_header(void) {
    if (g_options.quiet) return;

    printf("\nPinging %s", g_options.target);
    if (g_options.size != 32) {
        printf(" with %d bytes of data", g_options.size);
    }
    if (g_options.infinite) {
        printf(":\n\n");
    }
    else {
        printf(" (%d times):\n\n", g_options.count);
    }
}

void print_ping_result(const ping_result_t* result, int sequence) {
    if (g_options.quiet) return;

    if (result->success) {
        printf("Reply from %s: bytes=%d time=%lums TTL=%d\n",
            result->target_ip, g_options.size, result->rtt_ms, g_options.ttl);
    }
    else {
        switch (result->status) {
        case IP_DEST_HOST_UNREACHABLE:
            printf("Destination host unreachable.\n");
            break;
        case IP_DEST_NET_UNREACHABLE:
            printf("Destination net unreachable.\n");
            break;
        case IP_REQ_TIMED_OUT:
            printf("Request timed out.\n");
            break;
        case IP_BAD_DESTINATION:
            printf("Bad destination.\n");
            break;
        default:
            printf("General failure (status: 0x%08lX).\n", result->status);
            break;
        }
    }
}

void print_statistics(void) {
    if (g_options.quiet) return;

    printf("\nPing statistics for %s:\n", g_options.target);

    double loss_percent = 0.0;
    if (g_final_stats.packets_sent > 0) {
        loss_percent = ((double)g_final_stats.packets_lost / g_final_stats.packets_sent) * 100.0;
    }

    printf("    Packets: Sent = %lu, Received = %lu, Lost = %lu (%.0f%% loss),\n",
        g_final_stats.packets_sent, g_final_stats.packets_received,
        g_final_stats.packets_lost, loss_percent);

    if (g_final_stats.packets_received > 0) {
        printf("Approximate round trip times in milli-seconds:\n");
        printf("    Minimum = %.0fms, Maximum = %.0fms, Average = %.0fms\n",
            g_final_stats.min_rtt, g_final_stats.max_rtt, g_final_stats.avg_rtt);
    }

    if (g_final_stats.countermeasures_active) {
        printf("Note: Network countermeasures were activated during this session.\n");
    }
}

int execute_ping(void) {
    print_ping_header();

    int pings_sent = 0;
    int max_pings = g_options.infinite ? INT_MAX : g_options.count;

    while (pings_sent < max_pings && !g_interrupted) {
        ping_result_t result;
        DWORD status = ping_execute(g_options.target, &result);

        pings_sent++;
        print_ping_result(&result, pings_sent);

        // Check if we should continue
        if (pings_sent >= max_pings || g_interrupted) {
            break;
        }

        // Wait for next ping
        DWORD wait_start = GetTickCount();
        while ((GetTickCount() - wait_start) < (DWORD)g_options.interval && !g_interrupted) {
            Sleep(50);
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 3 || ch == 27) { // Ctrl+C or ESC
                    g_interrupted = true;
                    break;
                }
            }
        }
    }

    // Get final statistics
    ping_get_stats(&g_final_stats);
    print_statistics();

    return (g_final_stats.packets_received > 0) ? 0 : 1;
}

#pragma endregion

#pragma region Main_Function

int main(int argc, char* argv[]) {
    __try {
        // Set up signal handling
        signal(SIGINT, signal_handler);
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

        // Parse command line arguments
        if (!parse_arguments(argc, argv)) {
            return 1;
        }

        if (g_options.help) {
            print_version();
            print_usage();
            return 0;
        }

        // Initialize the DLL
        DWORD init_result = ping_initialize();
        if (init_result != ERROR_SUCCESS) {
            printf("Error: Failed to initialize ping subsystem (error %lu)\n", init_result);
            return 1;
        }

        // Configure the DLL based on our options
        ping_config_t config;
        if (ping_get_config(&config) == ERROR_SUCCESS) {
            strncpy_s(config.target, sizeof(config.target), g_options.target, _TRUNCATE);
            config.timeout_ms = g_options.timeout;
            config.interval_ms = g_options.interval;

            // Disable countermeasures for standard ping behavior
            config.enable_countermeasures = false;
            config.enable_logging = g_options.verbose;

            ping_set_config(&config);
        }

        // Execute the ping sequence
        int result = execute_ping();

        // Cleanup
        ping_cleanup();

        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("\nFatal error: Unhandled exception (0x%08X)\n", GetExceptionCode());
        ping_cleanup();
        return -1;
    }
}

#pragma endregion