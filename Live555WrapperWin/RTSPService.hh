#pragma once

#ifdef __ANDROID__
#define LIVE555WRAPPERWIN_API ""
#else
#ifdef LIVE555WRAPPERWIN_EXPORTS
#define LIVE555WRAPPERWIN_API __declspec(dllexport)
#else
#define LIVE555WRAPPERWIN_API __declspec(dllimport)
#endif
#endif

typedef unsigned char u_int8_t;
typedef void (*LogCallback)(const char* message);
typedef void (*RawDataCallback)(int64_t timestampMs, u_int8_t streamId, u_int8_t payloadFormat, unsigned int dataSize, const u_int8_t* data);

extern "C" LIVE555WRAPPERWIN_API void CBytesToGazePoint(const u_int8_t* bytes, float* out);
extern "C" LIVE555WRAPPERWIN_API short CStartWorker(const char* url, u_int8_t streamMask, LogCallback logCallback, RawDataCallback dataCallback);
extern "C" LIVE555WRAPPERWIN_API void CStopWorker(u_int8_t id);
extern "C" LIVE555WRAPPERWIN_API void CStop();