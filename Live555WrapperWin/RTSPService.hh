#pragma once

#ifdef LIVE555WRAPPERWIN_EXPORTS
#define LIVE555WRAPPERWIN_API __declspec(dllexport)
#else
#define LIVE555WRAPPERWIN_API __declspec(dllimport)
#endif

typedef unsigned char u_int8_t;
typedef void (*LogCallback)(const char* message);
typedef void (*RawDataCallback)(int64_t timestampMs, unsigned int dataSize, const u_int8_t* data);

extern "C" LIVE555WRAPPERWIN_API void CStart(const char* url, LogCallback callback, RawDataCallback gazeCallback, RawDataCallback worldCallback);
extern "C" LIVE555WRAPPERWIN_API void CStop();