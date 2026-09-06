#include "pch.h"
#include "log.h"

void Log(const char* fmt, ...)
{
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char buf[512];
    _snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "[SLCap] %s\n", msg);
    OutputDebugStringA(buf);
}
