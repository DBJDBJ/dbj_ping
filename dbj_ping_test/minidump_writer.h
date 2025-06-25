/*
 * minidump_writer.h - Header for minidump creation functionality
 * Place this file in the dbj_ping_test project directory
 */

#ifndef MINIDUMP_WRITER_H
#define MINIDUMP_WRITER_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize minidump writer
bool minidump_initialize(void);

// Create minidump on exception
bool minidump_create_on_exception(EXCEPTION_POINTERS* exception_info);

// Create minidump manually (for testing)
bool minidump_create_manual(const char* reason);

// Cleanup minidump writer
void minidump_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // MINIDUMP_WRITER_H

