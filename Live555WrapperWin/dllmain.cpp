/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2024, Live Networks, Inc.  All rights reserved
// A demo application, showing how to create and run a RTSP client (that can potentially receive multiple streams concurrently).
//
// NOTE: This code - although it builds a running application - is intended only to illustrate how to develop your own RTSP
// client application.  For a full-featured RTSP client application - with much more functionality, and many options - see
// "openRTSP": http://www.live555.com/openRTSP/

#include "liveMedia.hh"
#include "LoggingUsageEnvironment.hh"
#include <thread>
#include <mutex>
#include "RTSPService.hh"
#include <vector>

typedef std::vector<u_int8_t>(*DataPostprocessor)(const unsigned int size, const u_int8_t* unit);

enum StreamId {
	SID_GAZE = 0,
	SID_WORLD = 1
};

enum StreamStatus {
	UNINITIALIZED = 0,
	CLIENT_CREATED = 1,
	DESCRIBE_SUCCESS = 2,
	SETUP_SUCCESS = 3,
	PLAY_SUCCESS = 4,
	SHUTDOWN = 5
};

enum RTPPayloadFormat {
	PF_GAZE = 99,
	PF_WORLD = 96
};

// Forward function definitions:
static float bytesToFloat(const u_int8_t* bytes);
static bool isLittleEndian();
static std::vector<u_int8_t> processNalUnit(const unsigned int size, const u_int8_t* unit);

// RTSP 'response handlers':
static void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
static void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
static void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);

// Other event handler functions:
static void subsessionAfterPlaying(void* clientData); // called when a stream's subsession (e.g., audio or video substream) ends
static void subsessionByeHandler(void* clientData, char const* reason);
// called when a RTCP "BYE" is received for a subsession
static void streamTimerHandler(void* clientData);
// called at the end of a stream's expected duration (if the stream has not already signaled its end using a RTCP "BYE")

// The main streaming routine (for each "rtsp://" URL):
static RTSPClient* openURL(UsageEnvironment& env, char const* progName, char const* rtspURL, unsigned int id, RawDataCallback dataCallback);

// Used to iterate through each stream's 'subsessions', setting up each one:
static void setupNextSubsession(RTSPClient* rtspClient);

// Used to shut down and close a stream (including its "RTSPClient" object):
static void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

// A function that outputs a string that identifies each stream (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const RTSPClient& rtspClient) {
	return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

// A function that outputs a string that identifies each subsession (for debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env, const MediaSubsession& subsession) {
	return env << subsession.mediumName() << "/" << subsession.codecName();
}

/*
void usage(UsageEnvironment& env, char const* progName) {
	env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
	env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}
*/

// Define a class to hold per-stream state that we maintain throughout each stream's lifetime:

class StreamClientState {
public:
	StreamClientState();
	virtual ~StreamClientState();

public:
	MediaSubsessionIterator* iter;
	MediaSession* session;
	MediaSubsession* subsession;
	TaskToken streamTimerTask;
	double duration;
};

// If you're streaming just a single stream (i.e., just from a single URL, once), then you can define and use just a single
// "StreamClientState" structure, as a global variable in your application.  However, because - in this demo application - we're
// showing how to play multiple streams, concurrently, we can't do that.  Instead, we have to have a separate "StreamClientState"
// structure for each "RTSPClient".  To do this, we subclass "RTSPClient", and add a "StreamClientState" field to the subclass:

class ourRTSPClient : public RTSPClient {
public:
	static ourRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL, unsigned id, RawDataCallback dataCallback,
		int verbosityLevel = 0,
		char const* applicationName = NULL,
		portNumBits tunnelOverHTTPPortNum = 0);
	RawDataCallback dataCallback = NULL;

protected:
	ourRTSPClient(UsageEnvironment& env, char const* rtspURL, unsigned id, RawDataCallback dataCallback,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum);
	// called only by createNew();
	virtual ~ourRTSPClient();

public:
	StreamClientState scs;
	unsigned id;
};

// Define a data sink (a subclass of "MediaSink") to receive the data for each subsession (i.e., each audio or video 'substream').
// In practice, this might be a class (or a chain of classes) that decodes and then renders the incoming audio or video.
// Or it might be a "FileSink", for outputting the received data into a file (as is done by the "openRTSP" application).

class CallbackSink : public MediaSink {
public:
	static CallbackSink* createNew(UsageEnvironment& env,
		MediaSubsession& subsession, // identifies the kind of data that's being received
		RawDataCallback dataCallback,
		DataPostprocessor dataPostprocessor,
		char const* streamId = NULL); // identifies the stream itself (optional)

private:
	CallbackSink(UsageEnvironment& env, MediaSubsession& subsession, RawDataCallback dataCallback, DataPostprocessor dataPostprocessor, char const* streamId);
	// called only by "createNew()"
	virtual ~CallbackSink();

	static void afterGettingFrame(void* clientData, unsigned frameSize,
		unsigned numTruncatedBytes,
		struct timeval presentationTime,
		unsigned durationInMicroseconds);
	void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
		struct timeval presentationTime, unsigned durationInMicroseconds);

private:
	// redefined virtual functions:
	virtual Boolean continuePlaying();
	RawDataCallback dataCallback;
	DataPostprocessor dataPostprocessor;

private:
	u_int8_t* fReceiveBuffer;
	MediaSubsession& fSubsession;
	char* fStreamId;
};

#define RTSP_MAX_CLIENT_COUNT 2 //potentially gaze, imu, eyes, world - currently gaze and world only
#define RTSP_CLIENT_VERBOSITY_LEVEL 1 // by default, print verbose output from each "RTSPClient"
static std::atomic<unsigned> rtspClientStatus[RTSP_MAX_CLIENT_COUNT] = { 0 };

static RTSPClient* openURL(UsageEnvironment& env, char const* progName, char const* rtspURL, unsigned int id, RawDataCallback dataCallback) {
	// Begin by creating a "RTSPClient" object.  Note that there is a separate "RTSPClient" object for each stream that we wish
	// to receive (even if more than stream uses the same "rtsp://" URL).
	RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, id, dataCallback, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
	if (rtspClient == NULL) {
		env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
		return NULL;
	}

	rtspClientStatus[((ourRTSPClient*)rtspClient)->id] = StreamStatus::CLIENT_CREATED;

	// Next, send a RTSP "DESCRIBE" command, to get a SDP description for the stream.
	// Note that this command - like all RTSP commands - is sent asynchronously; we do not block, waiting for a response.
	// Instead, the following function call returns immediately, and we handle the RTSP response later, from within the event loop:
	rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
	return rtspClient;
}


// Implementation of the RTSP 'response handlers':

static void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		ourRTSPClient* oRtspClient = (ourRTSPClient*)rtspClient;
		StreamClientState& scs = oRtspClient->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
			delete[] resultString;
			break;
		}

		char* const sdpDescription = resultString;
		env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

		// Create a media session object from this SDP description:
		scs.session = MediaSession::createNew(env, sdpDescription);
		delete[] sdpDescription; // because we don't need it anymore
		if (scs.session == NULL) {
			env << *rtspClient << "Failed to create a MediaSession object from the SDP description: " << env.getResultMsg() << "\n";
			break;
		}
		else if (!scs.session->hasSubsessions()) {
			env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
			break;
		}

		rtspClientStatus[oRtspClient->id] = StreamStatus::DESCRIBE_SUCCESS;

		// Then, create and set up our data source objects for the session.  We do this by iterating over the session's 'subsessions',
		// calling "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command, on each one.
		// (Each 'subsession' will have its own data source.)
		scs.iter = new MediaSubsessionIterator(*scs.session);
		setupNextSubsession(rtspClient);
		return;
	} while (0);

	// An unrecoverable error occurred with this stream.
	shutdownStream(rtspClient);
}

// By default, we request that the server stream its data using RTP/UDP.
// If, instead, you want to request that the server stream via RTP-over-TCP, change the following to True:
#define REQUEST_STREAMING_OVER_TCP False

static void setupNextSubsession(RTSPClient* rtspClient) {
	UsageEnvironment& env = rtspClient->envir(); // alias
	ourRTSPClient* oRtspClient = (ourRTSPClient*)rtspClient;
	StreamClientState& scs = oRtspClient->scs; // alias

	scs.subsession = scs.iter->next();
	if (scs.subsession != NULL) {
		if (!scs.subsession->initiate(0)) {
			env << *rtspClient << "Failed to initiate the \"" << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
			setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
		}
		else {
			env << *rtspClient << "Initiated the \"" << *scs.subsession << "\" subsession (";
			if (scs.subsession->rtcpIsMuxed()) {
				env << "client port " << scs.subsession->clientPortNum();
			}
			else {
				env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum() + 1;
			}
			env << ")\n";

			// Continue setting up this subsession, by sending a RTSP "SETUP" command:
			rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, REQUEST_STREAMING_OVER_TCP);
		}
		return;
	}

	rtspClientStatus[oRtspClient->id] = StreamStatus::SETUP_SUCCESS;

	// We've finished setting up all of the subsessions.  Now, send a RTSP "PLAY" command to start the streaming:
	if (scs.session->absStartTime() != NULL) {
		// Special case: The stream is indexed by 'absolute' time, so send an appropriate "PLAY" command:
		rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(), scs.session->absEndTime());
	}
	else {
		scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
		rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
	}
}

static void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		ourRTSPClient* oRtspClient = (ourRTSPClient*)rtspClient;
		StreamClientState& scs = oRtspClient->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: " << resultString << "\n";
			break;
		}

		env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
		if (scs.subsession->rtcpIsMuxed()) {
			env << "client port " << scs.subsession->clientPortNum();
		}
		else {
			env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum() + 1;
		}
		env << ")\n";

		// Having successfully setup the subsession, create a data sink for it, and call "startPlaying()" on it.
		// (This will prepare the data sink to receive data; the actual flow of data from the client won't start happening until later,
		// after we've sent a RTSP "PLAY" command.)

		DataPostprocessor dataPostprocessor = NULL;
		switch (scs.subsession->rtpPayloadFormat())
		{
		case(RTPPayloadFormat::PF_GAZE):
			break;
		case(RTPPayloadFormat::PF_WORLD):
			//send SPS and PPS first
			unsigned int n;
			SPropRecord* record = parseSPropParameterSets(scs.subsession->fmtp_spropparametersets(), n);
			for (size_t i = 0; i < n; i++)
			{
				std::vector<u_int8_t> processed = processNalUnit(record[i].sPropLength, record[i].sPropBytes);
				oRtspClient->dataCallback(0, processed.size(), processed.data());
			}
			delete[] record;
			dataPostprocessor = processNalUnit;
			break;
		}

		scs.subsession->sink = CallbackSink::createNew(env, *scs.subsession, oRtspClient->dataCallback, dataPostprocessor, rtspClient->url());
		// perhaps use your own custom "MediaSink" subclass instead
		if (scs.subsession->sink == NULL) {
			env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
				<< "\" subsession: " << env.getResultMsg() << "\n";
			break;
		}

		env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
		scs.subsession->miscPtr = rtspClient; // a hack to let subsession handler functions get the "RTSPClient" from the subsession 
		scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
			subsessionAfterPlaying, scs.subsession);
		// Also set a handler to be called if a RTCP "BYE" arrives for this subsession:
		if (scs.subsession->rtcpInstance() != NULL) {
			scs.subsession->rtcpInstance()->setByeWithReasonHandler(subsessionByeHandler, scs.subsession);
		}
	} while (0);
	delete[] resultString;

	// Set up the next subsession, if any:
	setupNextSubsession(rtspClient);
}

static void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
	Boolean success = False;

	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		ourRTSPClient* oRtspClient = (ourRTSPClient*)rtspClient;
		StreamClientState& scs = oRtspClient->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
			break;
		}

		// Set a timer to be handled at the end of the stream's expected duration (if the stream does not already signal its end
		// using a RTCP "BYE").  This is optional.  If, instead, you want to keep the stream active - e.g., so you can later
		// 'seek' back within it and do another RTSP "PLAY" - then you can omit this code.
		// (Alternatively, if you don't want to receive the entire stream, you could set this timer for some shorter value.)
		if (scs.duration > 0) {
			unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's expected duration.  (This is optional.)
			scs.duration += delaySlop;
			unsigned uSecsToDelay = (unsigned)(scs.duration * 1000000);
			scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
		}

		env << *rtspClient << "Started playing session";
		if (scs.duration > 0) {
			env << " (for up to " << scs.duration << " seconds)";
		}
		env << "...\n";

		rtspClientStatus[oRtspClient->id] = StreamStatus::PLAY_SUCCESS;
		success = True;
	} while (0);
	delete[] resultString;

	if (!success) {
		// An unrecoverable error occurred with this stream.
		shutdownStream(rtspClient);
	}
}


// Implementation of the other event handlers:

static void subsessionAfterPlaying(void* clientData) {
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

	// Begin by closing this subsession's stream:
	Medium::close(subsession->sink);
	subsession->sink = NULL;

	// Next, check whether *all* subsessions' streams have now been closed:
	MediaSession& session = subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL) return; // this subsession is still active
	}

	// All subsessions' streams have now been closed, so shutdown the client:
	shutdownStream(rtspClient);
}

static void subsessionByeHandler(void* clientData, char const* reason) {
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
	UsageEnvironment& env = rtspClient->envir(); // alias

	env << *rtspClient << "Received RTCP \"BYE\"";
	if (reason != NULL) {
		env << " (reason:\"" << reason << "\")";
		delete[](char*)reason;
	}
	env << " on \"" << *subsession << "\" subsession\n";

	// Now act as if the subsession had closed:
	subsessionAfterPlaying(subsession);
}

static void streamTimerHandler(void* clientData) {
	ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
	StreamClientState& scs = rtspClient->scs; // alias

	scs.streamTimerTask = NULL;

	// Shut down the stream:
	shutdownStream(rtspClient);
}

static void shutdownStream(RTSPClient* rtspClient, int exitCode) {
	UsageEnvironment& env = rtspClient->envir(); // alias
	ourRTSPClient* oRtspClient = (ourRTSPClient*)rtspClient;
	StreamClientState& scs = oRtspClient->scs; // alias

	// First, check whether any subsessions have still to be closed:
	if (scs.session != NULL) {
		Boolean someSubsessionsWereActive = False;
		MediaSubsessionIterator iter(*scs.session);
		MediaSubsession* subsession;

		while ((subsession = iter.next()) != NULL) {
			if (subsession->sink != NULL) {
				Medium::close(subsession->sink);
				subsession->sink = NULL;

				if (subsession->rtcpInstance() != NULL) {
					subsession->rtcpInstance()->setByeHandler(NULL, NULL); // in case the server sends a RTCP "BYE" while handling "TEARDOWN"
				}

				someSubsessionsWereActive = True;
			}
		}

		if (someSubsessionsWereActive) {
			// Send a RTSP "TEARDOWN" command, to tell the server to shutdown the stream.
			// Don't bother handling the response to the "TEARDOWN".
			rtspClient->sendTeardownCommand(*scs.session, NULL);
		}
	}

	rtspClientStatus[oRtspClient->id] = StreamStatus::SHUTDOWN;

	env << *rtspClient << "Closing the stream.\n";
	Medium::close(rtspClient);
	// Note that this will also cause this stream's "StreamClientState" structure to get reclaimed.
}


// Implementation of "ourRTSPClient":

ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL, unsigned id, RawDataCallback dataCallback,
	int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
	return new ourRTSPClient(env, rtspURL, id, dataCallback, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL, unsigned id, RawDataCallback dataCallback,
	int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum)
	: RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) {
	this->id = id;
	this->dataCallback = dataCallback;
}

ourRTSPClient::~ourRTSPClient() {
}


// Implementation of "StreamClientState":

StreamClientState::StreamClientState()
	: iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
	delete iter;
	if (session != NULL) {
		// We also need to delete "session", and unschedule "streamTimerTask" (if set)
		UsageEnvironment& env = session->envir(); // alias

		env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
		Medium::close(session);
	}
}


// Implementation of "CallbackSink":

// Even though we're not going to be doing anything with the incoming data, we still need to receive it.
// Define the size of the buffer that we'll use:
#define CALLBACK_SINK_RECEIVE_BUFFER_SIZE 100000

CallbackSink* CallbackSink::createNew(UsageEnvironment& env, MediaSubsession& subsession, RawDataCallback dataCallback, DataPostprocessor dataPostprocessor, char const* streamId) {
	return new CallbackSink(env, subsession, dataCallback, dataPostprocessor, streamId);
}

CallbackSink::CallbackSink(UsageEnvironment& env, MediaSubsession& subsession, RawDataCallback dataCallback, DataPostprocessor dataPostprocessor, char const* streamId)
	: MediaSink(env),
	fSubsession(subsession) {
	fStreamId = strDup(streamId);
	fReceiveBuffer = new u_int8_t[CALLBACK_SINK_RECEIVE_BUFFER_SIZE];
	this->dataCallback = dataCallback;
	this->dataPostprocessor = dataPostprocessor;
}

CallbackSink::~CallbackSink() {
	delete[] fReceiveBuffer;
	delete[] fStreamId;
}

void CallbackSink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
	struct timeval presentationTime, unsigned durationInMicroseconds) {
	CallbackSink* sink = (CallbackSink*)clientData;
	sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// If you don't want to see debugging output for each received frame, then comment out the following line:
// #define DEBUG_PRINT_EACH_RECEIVED_FRAME 1

void CallbackSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
	struct timeval presentationTime, unsigned /*durationInMicroseconds*/) {
	// We've just received a frame of data.  (Optionally) print out information about it:

	if (dataPostprocessor != NULL) {
		std::vector<u_int8_t> processed = dataPostprocessor(frameSize, fReceiveBuffer);
		frameSize = processed.size();
		std::copy(processed.begin(), processed.end(), fReceiveBuffer);
	}
	dataCallback(presentationTime.tv_sec * 1000ll + presentationTime.tv_usec / 1000, frameSize, fReceiveBuffer);

#ifdef DEBUG_PRINT_EACH_RECEIVED_FRAME
	if (fStreamId != NULL) envir() << "Stream \"" << fStreamId << "\"; ";
	envir() << fSubsession.mediumName() << "/" << fSubsession.codecName() << ":\tReceived " << frameSize << " bytes";
	if (numTruncatedBytes > 0) envir() << " (with " << numTruncatedBytes << " bytes truncated)";
	char uSecsStr[6 + 1]; // used to output the 'microseconds' part of the presentation time
	sprintf_s(uSecsStr, "%06u", (unsigned)presentationTime.tv_usec);
	envir() << ".\tPresentation time: " << (int)presentationTime.tv_sec << "." << uSecsStr;
	if (fSubsession.rtpSource() != NULL && !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
		envir() << "!"; // mark the debugging output to indicate that this presentation time is not RTCP-synchronized
	}
#ifdef DEBUG_PRINT_NPT
	envir() << "\tNPT: " << fSubsession.getNormalPlayTime(presentationTime);
#endif
	envir() << "\n";
#endif

	// Then continue, to request the next frame of data:
	continuePlaying();
}

Boolean CallbackSink::continuePlaying() {
	if (fSource == NULL) return False; // sanity check (should not happen)

	// Request the next frame of data from our input source.  "afterGettingFrame()" will get called later, when it arrives:
	fSource->getNextFrame(fReceiveBuffer, CALLBACK_SINK_RECEIVE_BUFFER_SIZE,
		afterGettingFrame, this,
		onSourceClosure, this);
	return True;
}

class RTSPClientService
{
public:
	~RTSPClientService();
	void Start(const char* baseUrl, LogCallback logCallback, RawDataCallback gazeCallback, RawDataCallback worldCallback);
	void Stop();

private:
	std::string baseUrl; //rtsp://192.168.1.27:8086
	RawDataCallback dataCallbacks[RTSP_MAX_CLIENT_COUNT] = { NULL };
	TaskScheduler* scheduler = NULL;
	UsageEnvironment* env = NULL;
	volatile char watchVariable = 0;
	std::thread workerThread;

	void DoWork();
};

RTSPClientService::~RTSPClientService()
{
	Stop();
}

void RTSPClientService::Start(const char* baseUrl, LogCallback logCallback, RawDataCallback gazeCallback, RawDataCallback worldCallback)
{
	if (workerThread.joinable()) {
		Stop();
	}
	this->baseUrl = std::string(baseUrl);
	this->dataCallbacks[StreamId::SID_GAZE] = gazeCallback;
	this->dataCallbacks[StreamId::SID_WORLD] = worldCallback;
	watchVariable = 0;
	scheduler = BasicTaskScheduler::createNew();
	env = LoggingUsageEnvironment::createNew(*scheduler, logCallback);
	workerThread = std::thread(&RTSPClientService::DoWork, this);
}

void RTSPClientService::Stop()
{
	watchVariable = 1;
	if (workerThread.joinable())
	{
		workerThread.join();
	}
	if (env != NULL) {
		env->reclaim();
		env = NULL;
	}
	if (scheduler != NULL) {
		delete scheduler;
		scheduler = NULL;
	}
}

void RTSPClientService::DoWork()
{
	const char* urlParameters[RTSP_MAX_CLIENT_COUNT] = { "/?camera=gaze&audioenable=off", "/?camera=world&audioenable=off" };
	RTSPClient* clients[RTSP_MAX_CLIENT_COUNT] = { NULL };
	for (size_t i = 0; i < RTSP_MAX_CLIENT_COUNT; i++)
	{
		if (dataCallbacks[i] != NULL) {
			clients[i] = openURL(*env, "Live555RTSPClient", (baseUrl + urlParameters[i]).c_str(), i, dataCallbacks[i]);
		}
	}

	env->taskScheduler().doEventLoop(&watchVariable);

	for (size_t i = 0; i < RTSP_MAX_CLIENT_COUNT; i++)
	{
		unsigned clientStatus = rtspClientStatus[i];
		if (clientStatus != StreamStatus::UNINITIALIZED && clientStatus != StreamStatus::SHUTDOWN) {
			shutdownStream(clients[i]);
		}
		clients[i] = NULL;
	}
}

static bool isLittleEndian() {
	uint16_t number = 1; //0x0001
	u_int8_t* firstByte = (u_int8_t*)&number;
	return firstByte[0] == 1;
}

static float bytesToFloat(const u_int8_t* bytes) {

	static_assert(sizeof(float) == 4, "This code requires float to be 4 bytes");

	float result;
	u_int8_t reorderedBytes[4];

	if (isLittleEndian()) {
		reorderedBytes[0] = bytes[3];
		reorderedBytes[1] = bytes[2];
		reorderedBytes[2] = bytes[1];
		reorderedBytes[3] = bytes[0];
	}
	else {
		std::memcpy(reorderedBytes, bytes, 4);
	}

	std::memcpy(&result, reorderedBytes, sizeof(result));
	return result;
}

static std::vector<u_int8_t> processNalUnit(const unsigned int size, const u_int8_t* unit) {
	const u_int8_t startCode[4] = { 0x00, 0x00, 0x00, 0x01 };
	std::vector<u_int8_t> result(startCode, startCode + sizeof(startCode));
	size_t offset = 0;

	u_int8_t firstByte = unit[0];

	// Check forbidden_zero_bit (first bit of the first byte must be 0)
	bool isFirstBitOne = firstByte & 0b10000000;
	if (isFirstBitOne) {
		throw std::invalid_argument("First bit must be zero (forbidden_zero_bit)");
	}

	// Extract the NAL type (lower 5 bits of the first byte)
	u_int8_t nalType = firstByte & 0b00011111;

	if (nalType == 28) {
		// Fragmentation Unit (FU-A)
		// Ensure the unit is long enough to have a second byte (FU header)
		if (size < 2) {
			throw std::invalid_argument("NAL unit too short for FU-A header");
		}

		u_int8_t fuHeader = unit[1];
		offset = 2;  // Skip first two bytes (NAL header and FU header)

		// Check the Start bit of the FU header (bit 8 of the second byte)
		bool isFuStartBitOne = fuHeader & 0b10000000;

		if (isFuStartBitOne) {
			// Reconstruct the original NAL unit header from the FU-A headers
			u_int8_t firstByteBits1to3 = firstByte & 0b11100000;   // First 3 bits of the original NAL unit
			u_int8_t fuHeaderBits4to8 = fuHeader & 0b00011111;     // Last 5 bits of FU header (original NAL type)

			u_int8_t reconstructedHeader = firstByteBits1to3 | fuHeaderBits4to8;
			result.push_back(reconstructedHeader);  // Append the reconstructed NAL header to the start code
		}
		else {
			// Do not prepend the start code, we are in the middle of a fragmented NAL unit
			result.clear();
		}
	}

	// Append the rest of the payload (after the FU headers) to the start code (or directly if no FU)
	result.insert(result.end(), unit + offset, unit + size);

	return result;
}

static RTSPClientService service;

void CStart(const char* url, LogCallback logCallback, RawDataCallback gazeCallback, RawDataCallback worldCallback) {
	service.Start(url, logCallback, gazeCallback, worldCallback);
}

void CStop() {
	service.Stop();
}

