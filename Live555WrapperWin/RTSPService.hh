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

enum EyeEventsDataType {
	EEDT_SACCADE = 0,
	EEDT_FIXATION = 1,
	EEDT_SACCADE_ONSET = 2,
	EEDT_FIXATION_ONSET = 3,
	EEDT_BLINK = 4,
	EEDT_KEEPALIVE = 5
};

enum EtDataType : int {
	EDT_GAZE_DATA = 0,
	EDT_DUAL_MONOCULAR_GAZE_DATA = 1,
	EDT_EYE_STATE_GAZE_DATA = 2,
	EDT_EYE_STATE_EYELID_GAZE_DATA = 3,
	EDT_UNKNOWN = -1
};

enum StreamId {
	SID_IMU = 0,
	SID_WORLD = 1,
	SID_GAZE = 2,
	SID_EYE_EVENTS = 3,
	SID_EYES = 4
};

enum StreamStatus {
	SST_UNINITIALIZED = 0,
	SST_CLIENT_CREATED = 1,
	SST_DESCRIBE_SUCCESS = 2,
	SST_SETUP_SUCCESS = 3,
	SST_PLAY_SUCCESS = 4,
	SST_SHUTDOWN = 5
};

enum RTPPayloadFormat {
	PF_VIDEO = 96,
	PF_AUDIO = 97,
	PF_GAZE = 99,
	PF_IMU = 100,
	PF_EYE_EVENTS = 101
};

extern "C" LIVE555WRAPPERWIN_API int pl_bytes_to_eye_tracking_data(
	const u_int8_t* bytes,
	const unsigned int size,
	float* gazePoint, bool* worn,
	float* gazePointDualRight,
	float* eyeStateLeft, float* eyeStateRight,
	float* eyelidLeft, float* eyelidRight
);
extern "C" LIVE555WRAPPERWIN_API int pl_bytes_to_eye_event_data(
	const u_int8_t* bytes,
	const unsigned int size,
	int* eventType, long long* startTime,
	long long* endTime,
	float* gazeEvent
);
extern "C" LIVE555WRAPPERWIN_API int pl_bytes_to_imu_data(
	const u_int8_t* bytes,
	unsigned int size,
	unsigned long long* tsNs,
	float* accelData,
	float* gyroData,
	float* quatData
);
extern "C" LIVE555WRAPPERWIN_API short pl_start_worker(const char* url, u_int8_t streamMask, LogCallback logCallback, RawDataCallback dataCallback);
extern "C" LIVE555WRAPPERWIN_API void pl_stop_worker(u_int8_t id);
extern "C" LIVE555WRAPPERWIN_API void pl_stop_service();