>
> This is win32 program
>
> here is the power shell script to view the event log, emited from dbj_ping.dll 
```powershell
 Get-EventLog -LogName Application | Where-Object {$_.Message -like "*dbj_ping*"} | Select-Object TimeGenerated, EntryType, @{Name="FullMessage"; Expression={$_.ReplacementStrings -join " "}} | Format-Table -Wrap
 ```

# dbj_ping - Advanced Windows Ping DLL with Countermeasures

A high-performance Windows DLL that provides ping functionality with intelligent countermeasures for network issues. Built with C17, MSVC, and comprehensive SEH (Structured Exception Handling).

## 🎯 Key Features

### Core Functionality
- **Raw ICMP Ping**: High-performance ping using Windows ICMP API
- **C17 Standard**: Modern C17 compliance with MSVC extensions
- **SEH Integration**: Comprehensive structured exception handling
- **External Configuration**: INI file-based configuration with auto-generation
- **Windows Event Logging**: Built-in logging to Windows Event Log

### Intelligent Countermeasures
- **Automatic DNS Switching**: Cycles through backup DNS servers on issues
- **Network Route Refresh**: Flushes ARP tables and routing cache
- **DNS Cache Management**: Automatic DNS cache flushing
- **Real-time Analysis**: Monitors packet loss, latency, and jitter
- **Configurable Thresholds**: Customizable trigger points for countermeasures

### Advanced Features
- **Multi-threading Safe**: Thread-safe operations with critical sections
- **Resource Management**: Automatic cleanup and resource tracking
- **Performance Optimized**: `/kernel` and `/utf-8` compilation flags
- **Deployment Ready**: Automated deployment package creation

## 📦 Project Structure

```
dbj_ping\                   # Solution root
├── dbj_ping.sln           # Visual Studio solution
├── dbj_ping\              # DLL project
│   ├── dbj_ping.vcxproj   # DLL project file
│   ├── dbj_ping.c         # Main DLL implementation (single file)
│   ├── dbj_ping.h         # Public API header
│   ├── dbj_ping.def       # Export definitions
│   └── README.md          # DLL documentation
├── dbj_ping_test\         # Test application project
│   ├── dbj_ping_test.vcxproj
│   ├── dbj_ping_test.c    # Test application with minidump support
│   ├── minidump_writer.c  # Minidump creation functionality
│   ├── minidump_writer.h
│   └── README_TEST.md
├── bin\                   # Build outputs
│   ├── x64\{Debug,Release}\
│   └── Win32\{Debug,Release}\
├── obj\                   # Intermediate files
├── deployment\            # Deployment package
│   ├── dbj_ping.dll
│   ├── dbj_ping.lib
│   └── dbj_ping.h
└── minidumps\            # Created by test app for crash dumps
```

## 🔧 Build Instructions

### Prerequisites
- **Visual Studio 2019/2022** with C++ workload
- **Windows SDK 10.0** or later
- **MSVC v143 toolset** (Visual Studio 2022) or v142 (Visual Studio 2019)

### Building with Visual Studio

1. **Open Solution**:
   ```
   Open dbj_ping.sln in Visual Studio
   ```

2. **Select Configuration**:
   - **Debug**: For development and testing
   - **Release**: For production deployment

3. **Select Platform**:
   - **x64**: Recommended for modern systems
   - **Win32**: For legacy 32-bit support

4. **Build**:
   ```
   Build → Build Solution (Ctrl+Shift+B)
   ```

### Building from Command Line

1. **Setup Environment**:
   ```batch
   "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
   ```

2. **Build DLL**:
   ```batch
   cd dbj_ping
   cl /std:c17 /EHa /kernel /utf-8 /DDBJ_PING_EXPORTS /LD dbj_ping.c ^
      /link ws2_32.lib iphlpapi.lib /DEF:dbj_ping.def /OUT:dbj_ping.dll
   ```

3. **Build Test Application**:
   ```batch
   cd ..\dbj_ping_test
   cl /std:c17 /EHa /utf-8 dbj_ping_test.c minidump_writer.c ^
      dbj_ping.lib dbghelp.lib /OUT:dbj_ping_test.exe
   ```

### Compilation Flags Explained

- **`/std:c17`**: Use C17 standard
- **`/EHa`**: Enable structured exception handling (SEH)
- **`/kernel`**: Generate kernel-mode compatible code
- **`/utf-8`**: Source and execution character sets are UTF-8
- **`/DDBJ_PING_EXPORTS`**: Define export macro for DLL

### Required Libraries

- **`ws2_32.lib`**: Winsock 2 for network operations
- **`iphlpapi.lib`**: IP Helper API for network management
- **`dbghelp.lib`**: Debug help for minidump creation (test app only)

## 🚀 Usage

### Basic API Usage

```c
#include "dbj_ping.h"

int main() {
    // Initialize the ping subsystem
    if (ping_initialize() != ERROR_SUCCESS) {
        return -1;
    }
    
    // Execute a ping
    ping_result_t result;
    DWORD status = ping_execute("8.8.8.8", &result);
    
    if (status == ERROR_SUCCESS && result.success) {
        printf("Ping successful: %lu ms\n", result.rtt_ms);
    }
    
    // Cleanup
    ping_cleanup();
    return 0;
}
```

### Configuration File (dbj_ping.ini)

```ini
[Ping]
Target=8.8.8.8
TimeoutMs=3000
IntervalMs=1000
MaxRetries=3

[Thresholds]
LossThreshold=30
LatencyThreshold=500
JitterThreshold=100

[Features]
EnableCountermeasures=1
EnableDnsSwitching=1
EnableRouteRefresh=1
EnableLogging=1

[DNS]
BackupDns1=8.8.8.8
BackupDns2=1.1.1.1
BackupDns3=9.9.9.9
# ... up to 8 backup DNS servers
```

### Running the Test Application

```batch
# Basic test with default target
dbj_ping_test.exe

# Test specific target
dbj_ping_test.exe google.com

# Test with IP address  
dbj_ping_test.exe 1.1.1.1
```

## 📊 API Reference

### Core Functions

```c
// Initialize the ping subsystem
DWORD ping_initialize(void);

// Execute a single ping
DWORD ping_execute(const char* target, ping_result_t* result);

// Get current statistics
DWORD ping_get_stats(ping_stats_t* stats);

// Get/Set configuration
DWORD ping_get_config(ping_config_t* config);
DWORD ping_set_config(const ping_config_t* config);

// Utility functions
DWORD ping_reset_stats(void);
DWORD ping_force_countermeasures(void);
void ping_cleanup(void);
```

### Data Structures

```c
typedef struct {
    bool success;
    DWORD rtt_ms;
    DWORD status;
    char target_ip[16];
    SYSTEMTIME timestamp;
} ping_result_t;

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
```

## 🛡️ Countermeasures System

### Automatic Triggers

The DLL automatically triggers countermeasures when:

1. **Packet Loss** > configured threshold (default: 30%)
2. **Average Latency** > configured threshold (default: 500ms)
3. **Jitter** > configured threshold (default: 100ms)

### Available Countermeasures

1. **DNS Server Switching**
   - Cycles through up to 8 backup DNS servers
   - Uses `netsh` to change system DNS settings
   - Requires administrator privileges

2. **Network Route Refresh**
   - Flushes ARP table (`arp -d *`)
   - Clears routing cache
   - Forces network stack refresh

3. **DNS Cache Flushing**
   - Executes `ipconfig /flushdns`
   - Clears local DNS resolver cache
   - Improves DNS resolution reliability

### Countermeasure Cooldown

- **30-second cooldown** prevents recursive activation
- **Thread-safe implementation** using critical sections
- **Event logging** for all countermeasure activities

## 🔍 Error Handling & SEH Pattern

### SEH Implementation

The project implements a consistent SEH pattern:

```c
int function_name(void) {
    int result = 0;
    HANDLE resource = NULL;
    
    __try {
        // Function logic here
        // Use __leave for early exits
        if (error_condition) __leave;
        
        result = 1;  // Success
    }
    __finally {
        // Cleanup resources
        if (resource) CloseHandle(resource);
    }
    
    return result;
}
```

### Exception Handling in Main

```c
int main() {
    __try {
        // Main logic
        return 0;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Create minidump
        minidump_create_on_exception(GetExceptionInformation());
        return -1;
    }
}
```

## 📝 Logging System

### Windows Event Log Integration

```c
// Logging function (implemented by user)
void dbj_log(log_kind_t kind, const char msg[0xFF], ...);

// Log levels
typedef enum {
    LOG_INFO = 0,
    LOG_WARNING = 1,
    LOG_ERROR = 2,
    LOG_CRITICAL = 3
} log_kind_t;
```

### Event Log Configuration

Events are logged to the Windows Event Log under the source name **"dbj_ping"**. View logs using:

- **Event Viewer**: Windows Logs → Application
- **PowerShell**: `Get-EventLog -LogName Application -Source dbj_ping`
- **Command Line**: `wevtutil qe Application /q:"*[System[Provider[@Name='dbj_ping']]]"`

## 🧪 Testing & Debugging

### Test Application Features

- **Interactive Mode**: Real-time ping monitoring
- **Statistics Display**: Every 10 pings or on demand
- **Countermeasure Testing**: Force activation for testing
- **Minidump Creation**: Automatic crash dump generation
- **Error Simulation**: Test various failure scenarios

### Debugging Tools

1. **Visual Studio Debugger**: Full debugging support
2. **WinDbg**: For analyzing minidumps
3. **Event Viewer**: Monitor logging output
4. **Performance Toolkit**: Analyze performance characteristics

### Common Testing Scenarios

```batch
# Test basic functionality
dbj_ping_test.exe 8.8.8.8

# Test with unreachable host (triggers countermeasures)
dbj_ping_test.exe 192.0.2.1

# Test DNS resolution
dbj_ping_test.exe google.com

# Test with invalid hostname
dbj_ping_test.exe invalid.hostname.test
```

## 📋 Deployment

### Deployment Package

The build process automatically creates a deployment package in the `deployment\` directory:

```
deployment\
├── dbj_ping.dll      # Main DLL
├── dbj_ping.lib      # Import library
└── dbj_ping.h        # Header file
```

### Installation

1. **Copy Files**: Place `dbj_ping.dll` in application directory or system PATH
2. **Import Library**: Link against `dbj_ping.lib` in your project
3. **Include Header**: Include `dbj_ping.h` in your source code
4. **Configuration**: Create or modify `dbj_ping.ini` as needed

### System Requirements

- **Windows 7** or later (Windows 10 recommended)
- **Visual C++ Redistributable** (for Release builds)
- **Administrator privileges** (for some countermeasures)
- **Network connectivity** (for ping operations)

## 🤝 Contributing

### Development Guidelines

1. **Follow SEH Pattern**: Use `__try`/`__finally` consistently
2. **Use `#region`**: Organize code with regions for readability
3. **C17 Compliance**: Ensure all code compiles with `/std:c17`
4. **Error Handling**: Always handle errors gracefully
5. **Resource Cleanup**: Use RAII patterns in `__finally` blocks

### Coding Style (if any)

- **Function Names**: Use `snake_case` for internal functions
- **API Functions**: Use `ping_` prefix for exported functions
- **Constants**: Use `UPPER_CASE` for preprocessor constants
- **Types**: Use `_t` suffix for typedef names

## 📄 License

This project is provided as-is for educational and development purposes.

## 🐛 Known Issues

1. **Administrator Rights**: Some countermeasures require elevated privileges
2. **Firewall Conflicts**: ICMP may be blocked by firewall software
3. **Antivirus Interference**: Network operations may trigger security software

## 🔮 Future Enhancements

- [ ] IPv6 support
- [ ] Additional countermeasure strategies
- [ ] Network topology discovery
- [ ] Performance metrics export
- [ ] REST API interface
- [ ] PowerShell module wrapper

---

**Built using C17, MSVC, and Windows APIs, with Claude as demanding but trusty young apprentice**