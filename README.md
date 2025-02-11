## Building Live555 library
1. clone https://github.com/melchi45/live555
2. cd live555
3. mkdir build
4. cd build
5. cmake .. -B vs2022 -G "Visual Studio 17 2022" -DLIVE555_ENABLE_OPENSSL=OFF -DLIVE555_BUILD_EXAMPLES=OFF -DLIVE555_MONOLITH_BUILD=ON<br/>
6. open generated VS solution that can be found in build folder and build the project
7. built dll can be found in build\vs2022\Debug

## Building the wrapper
1. clone this repository (Live555WrapperWin and live555 repositories should be in the same folder)
2. open VS solution
3. build

## C# test program - getting raw data
1. copy live555.dll and Live555WrapperWin.dll into the project folder
2. use following sample
```csharp
using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace Live555Console
{
    internal class Program
    {
        private static class Live555Wrapper
        {
            [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
            public delegate void LogCallback([MarshalAs(UnmanagedType.LPStr)] string message);

            [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
            public delegate void RawDataCallback(long timestampMs, uint dataSize, IntPtr data);

            [DllImport("Live555WrapperWin")]
            public static extern void CStart([MarshalAs(UnmanagedType.LPStr)] string url, LogCallback logCallback, RawDataCallback gazeCallback, RawDataCallback worldCallback);

            [DllImport("Live555WrapperWin")]
            public static extern void CStop();

        }

        static void LogCallback(string message)
        {
            Console.WriteLine(message);
        }
        static void GazeCallback(long timestamp, uint dataSize, IntPtr data)
        {
            Console.WriteLine($"Gaze data received at: {timestamp} of length: {dataSize}");
        }

        static void WorldCallback(long timestamp, uint dataSize, IntPtr data)
        {
            Console.WriteLine($"World data received at: {timestamp} of length: {dataSize}");
        }

        static void Main(string[] args)
        {
            Live555Wrapper.CStart("rtsp://192.168.1.27:8086", LogCallback, GazeCallback, WorldCallback);

            Thread.Sleep(10000);

            Live555Wrapper.CStop();
        }
    }
}
```

## Python test program - frame and gaze
1. copy live555.dll and Live555WrapperWin.dll into the ./libs folder
2. use following sample
```python
import time
import os
from ctypes import c_char_p, c_int64, c_uint, POINTER, c_uint8, cdll, cast, CFUNCTYPE
from collections import deque

class MatchingConsumer:
    def __init__(self, frame_queue_limit=20, gaze_queue_limit=200, tolerance=0.005): #TODO tolerance check not enough need was behind
        self.frame_queue = deque()
        self.gaze_queue = deque()
        self.current_frame = None
        self.current_gaze = None
        self.frame_queue_limit = frame_queue_limit
        self.gaze_queue_limit = gaze_queue_limit
        self.tolerance = tolerance
        return
        
    def try_consume(self, queue):
        return queue.popleft() if len(queue) > 0 else None
        
    def next_match(self):
        out_frame = None
        out_gaze = None

        #try consume next if needed
        if self.current_frame is None:
            self.current_frame = self.try_consume(self.frame_queue)
        if self.current_gaze is None:
            self.current_gaze = self.try_consume(self.gaze_queue)

        if self.current_frame is not None:
            #4 possible states, frame should wait, gaze should wait, match was found or no data
            frame_wait = False
            gaze_wait = False
            match_found = False
            if self.current_gaze is None:
                frame_wait = True
                print("No gaze")
            else:
                #got frame and gaze, need to compare times
                pts_frame = self.current_frame[0]
                was_frame_ahead = False
                while self.current_gaze is not None:
                    pts_gaze = self.current_gaze[0]
                    if abs(pts_frame - pts_gaze) < self.tolerance:
                        match_found = True
                        break
                    elif pts_frame > pts_gaze:
                        #frame ahead
                        #take next gaze
                        print("Got frame", self.current_frame[0], "Checking gaze", self.current_gaze[0])
                        self.current_gaze = self.try_consume(self.gaze_queue)
                        was_frame_ahead = True
                    elif was_frame_ahead is True:
                        #gaze ahead but prev frame was ahead
                        match_found = True
                        break
                    else:
                        #gaze ahead take another frame
                        gaze_wait = True
                        break
                else:
                    #run out of gaze data
                    frame_wait = True
                    print("Checked all gaze data")
            
            if match_found is True:
                out_frame = self.current_frame
                out_gaze = self.current_gaze
                self.current_frame = None
                self.current_gaze = None
                print("match found", out_frame[0], out_gaze[0])
                print("frame queue len", len(self.frame_queue), "gaze queue len", len(self.gaze_queue))
            elif frame_wait is True:
                #we should wait with frame but if buffer full release it
                if len(self.frame_queue) > self.frame_queue_limit:
                    out_frame = self.current_frame
                    self.current_frame = None
                    print("frame release, buffer full")
                else:
                    print("frame waiting", self.current_frame[0], self.current_gaze[0] if self.current_gaze is not None else "NO DATA")
                self.current_gaze = None
            elif gaze_wait is True:
                out_frame = self.current_frame
                self.current_frame = None
                print("gaze waiting")
            else:
                #no data
                pass

        #keep gaze buffer size under control
        while len(self.gaze_queue) > self.gaze_queue_limit:
            self.current_gaze = self.gaze_queue.popleft()
            
        return out_frame, out_gaze

class NeonClient:
    
    def __init__(self, url):
        self.lib = cdll.LoadLibrary(os.path.join(os.path.dirname(__file__), r"libs\Live555WrapperWin.dll"))
        
        self._logCallbackType = CFUNCTYPE(None, c_char_p)
        self._rawDataCallbackType = CFUNCTYPE(None, c_int64, c_uint, POINTER(c_uint8))
        self._logCallbackFunc = None
        self._gazeCallbackFunc = None
        self._worldCallbackFunc = None
        
        self.lib.CStart.argtypes = [c_char_p, self._logCallbackType, self._rawDataCallbackType, self._rawDataCallbackType]
        self.lib.CStart.restype = None
        self.lib.CStop.argtypes = []
        self.lib.CStop.restype = None
        
        self.url = url
        return
        
    def start(self, logCallback, gazeCallback, worldCallback):
        self._logCallbackFunc = self._logCallbackType(logCallback) if logCallback is not None else cast(logCallback, self._logCallbackType)
        self._gazeCallbackFunc = self._rawDataCallbackType(gazeCallback) if gazeCallback is not None else cast(gazeCallback, self._rawDataCallbackType)
        self._worldCallbackFunc = self._rawDataCallbackType(worldCallback) if worldCallback is not None else cast(worldCallback, self._rawDataCallbackType)
        self.lib.CStart(
            self.url.encode('utf-8'),
            self._logCallbackFunc,
            self._gazeCallbackFunc,
            self._worldCallbackFunc
        )
        return
        
    def stop(self):
        self.lib.CStop()
        return

if __name__=="__main__":
    import cv2
    from av.codec import CodecContext
    import struct
    
    codec = CodecContext.create("h264", "r")
    
    matcher = MatchingConsumer()
    def logCallback(message):
        print(message)
        return
        
    def gazeCallback(timestampMs, dataSize, data):
        data = bytes(cast(data, POINTER(c_uint8 * dataSize)).contents)
        print(f"Received gaze data at: {timestampMs}, length: {len(data)}=={dataSize}")
        matcher.gaze_queue.append((timestampMs, data))
        return
        
    def worldCallback(timestampMs, dataSize, data):
        data = bytes(cast(data, POINTER(c_uint8 * dataSize)).contents)
        print(f"Received world camera data at: {timestampMs}, length: {len(data)}=={dataSize}")
        matcher.frame_queue.append((timestampMs, data))
        return
    nc = NeonClient("rtsp://192.168.1.27:8086")
    nc.start(logCallback, gazeCallback, worldCallback)
    
    try:
        while(True):
            frameData, gazeData = matcher.next_match()
            if frameData is not None:
                packets = codec.parse(frameData[1])
                frame = None
                for packet in packets:
                    frames = codec.decode(packet)
                    if frames:
                        frame = frames[-1]
                if frame is not None and gazeData is not None:
                    point = tuple(int(x) for x in struct.unpack("!ff", gazeData[1][:8]))
                    print(point)
                    frame = frame.to_ndarray(format="bgr24")
                    print(frame.shape)
                    cv2.circle(frame, point, 20, (0, 0, 255), 2)
                    cv2.imshow("frame",  cv2.resize(frame, (0, 0), fx=0.5, fy=0.5))
            cv2.waitKey(10)
    except KeyboardInterrupt:
        pass
    
    nc.stop()
```

## C++ test program - frame and gaze (no sync)
```cpp
#include <opencv2/opencv.hpp>
#include <RTSPService.hh>
#include <concurrent_queue.h>

extern "C"
{
#include<libswscale/swscale.h>
#include<libavcodec/avcodec.h>
}

static float gazePoint[2];
static std::mutex gazePointMutex;
static Concurrency::concurrent_queue<std::vector<u_int8_t>> videoQueue;

void logCallback(const char* message) {
	std::cout << message;
}

void gazeCallback(int64_t timestampMs, unsigned int dataSize, const u_int8_t* data) {
	gazePointMutex.lock();
	CBytesToGazePoint(data, gazePoint);
	gazePointMutex.unlock();
	std::cout << "RECEIVED GAZE DATA AT: " << timestampMs << std::endl;
}

void videoCallback(int64_t timestampMs, unsigned int dataSize, const u_int8_t* data) {
	std::vector<u_int8_t> v(data, data + dataSize);
	videoQueue.push(v);
	std::cout << "RECEIVED VIDEO DATA AT: " << timestampMs << std::endl;
}

cv::Mat avframeToCvmat(const AVFrame* frame) {
	int width = frame->width;
	int height = frame->height;
	cv::Mat image(height, width, CV_8UC3);
	int cvLinesizes[1];
	cvLinesizes[0] = image.step1();
	SwsContext* conversion = sws_getContext(
		width, height, (AVPixelFormat)frame->format, width, height,
		AVPixelFormat::AV_PIX_FMT_BGR24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
	sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data,
		cvLinesizes);
	sws_freeContext(conversion);
	return image;
}

int main() {

	int gpRadius = 20;
	cv::Scalar gpColor(0, 0, 255);
	int gpThickness = 2;

	CStart("rtsp://192.168.1.27:8086", logCallback, gazeCallback, videoCallback);

	const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec) {
		return -1;
	}
	AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		return -1;
	}
	if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
		avcodec_free_context(&codec_ctx);
		return -1;
	}
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	while (true) {

		std::vector<u_int8_t> unit;
		if (videoQueue.try_pop(unit) == false) {
			continue;
		}
		packet->data = unit.data();
		packet->size = unit.size();

		int ret = avcodec_send_packet(codec_ctx, packet);
		if (ret < 0) {
			continue;
		}

		ret = avcodec_receive_frame(codec_ctx, frame);
		if (ret == 0) {
			cv::Mat cvFrame = avframeToCvmat(frame);
			gazePointMutex.lock();
			cv::Point gp(static_cast<int>(gazePoint[0]), static_cast<int>(gazePoint[1]));
			gazePointMutex.unlock();
			cv::circle(cvFrame, gp, gpRadius, gpColor, gpThickness);
			cv::imshow("World and gaze", cvFrame);
		}

		if (cv::waitKey(10) >= 0)
			break;
	}

	av_packet_free(&packet);
	av_frame_free(&frame);
	avcodec_free_context(&codec_ctx);
	cv::destroyAllWindows();

	CStop();

	return 0;
}
```
