#pragma once

#ifdef LIVE555WRAPPERWIN_EXPORTS
#define LIVE555WRAPPERWIN_API __declspec(dllexport)
#else
#define LIVE555WRAPPERWIN_API __declspec(dllimport)
#endif

#include "BasicUsageEnvironment.hh"
#include "RTSPService.hh"

class LoggingUsageEnvironment : public BasicUsageEnvironment {
public:
	static LoggingUsageEnvironment* createNew(TaskScheduler& taskScheduler, LogCallback callback);

	virtual UsageEnvironment& operator<<(char const* str);
	virtual UsageEnvironment& operator<<(int i);
	virtual UsageEnvironment& operator<<(unsigned u);
	virtual UsageEnvironment& operator<<(double d);
	virtual UsageEnvironment& operator<<(void* p);

protected:
	LoggingUsageEnvironment(TaskScheduler& taskScheduler, LogCallback callback);
	virtual void writeFormatted(const char* format, ...);

	static constexpr size_t BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];
	size_t bufferOffset = 0;
	LogCallback callback = NULL;
};