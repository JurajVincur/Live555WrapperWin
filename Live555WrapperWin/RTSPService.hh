#pragma once

#ifdef LIVE555WRAPPERWIN_EXPORTS
#define LIVE555WRAPPERWIN_API __declspec(dllexport)
#else
#define LIVE555WRAPPERWIN_API __declspec(dllimport)
#endif

typedef void (*LogCallback)(const char*);

extern "C" LIVE555WRAPPERWIN_API void CStart(const char* url, LogCallback callback);
extern "C" LIVE555WRAPPERWIN_API void CStop();
extern "C" LIVE555WRAPPERWIN_API void CGetGazePoint(float* out);