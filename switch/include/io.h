// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_IO_H
#define CHIAKI_IO_H

#include <SDL2/SDL.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <glad.h> // glad library (OpenGL loader)

#include <chiaki/session.h>

/*
https://github.com/devkitPro/switch-glad/blob/master/include/glad/glad.h
https://glad.dav1d.de/#profile=core&language=c&specification=gl&api=gl%3D4.3&extensions=GL_EXT_texture_compression_s3tc&extensions=GL_EXT_texture_filter_anisotropic

Language/Generator: C/C++
Specification: gl
APIs: gl=4.3
Profile: core
Extensions:
GL_EXT_texture_compression_s3tc,
GL_EXT_texture_filter_anisotropic
Loader: False
Local files: False
Omit khrplatform: False
Reproducible: False
*/

#ifdef __SWITCH__
#include <switch.h>
#else
#include <iostream>
#endif

#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chiaki/controller.h>
#include <chiaki/log.h>

#include "exception.h"
#include "audren_audio.h"
#include "deko_video_renderer.h"

#define PLANES_COUNT 3
#define SDL_JOYSTICK_COUNT 2

class IO
{
	protected:
		IO();
		static IO * instance;
	private:
		ChiakiLog *log;
		int video_width;
		int video_height;
		bool quit = false;
		static const int MAX_FRAME_COUNT_MAX = 8;
		static const int MAX_NV12_PLANE_COUNT = 2;
		static const int DEFAULT_FRAME_QUEUE_SIZE = 4;
		GLint m_texture_uniform[MAX_NV12_PLANE_COUNT];
		// opengl reader writer
		std::mutex mtx;
		// default nintendo switch res
		AVBufferRef *hw_device_ctx = nullptr;
		int screen_width = 1280;
		int screen_height = 720;
		const AVCodec *codec;
		AVCodecContext *codec_context;
		AVFrame **frames;
		uintptr_t origin_ptr[MAX_FRAME_COUNT_MAX][MAX_NV12_PLANE_COUNT] = {};
			int frame_queue_size = DEFAULT_FRAME_QUEUE_SIZE;
		int audio_volume = 180;
		int stick_deadzone = 0;
		int audio_buffer_max = 16000;
		int audio_backend_mode = 0;
#ifdef __SWITCH__
		AudrenAudio audren;
#endif

		std::atomic<uint64_t> frames_decoded{0};
		std::atomic<uint64_t> frames_rendered{0};
		std::atomic<uint32_t> last_decode_time_us{0};
		std::atomic<int> audio_queue_bytes{0};
		std::atomic<uint64_t> total_frames_lost{0};
		std::atomic<uint64_t> total_frames_received{0};
		std::atomic<int> current_frame_index{0};
		std::atomic<bool> has_decoded_frame{false};
		int next_frame_index = 0;
		bool frames_have_sw_buffers = false;
	bool use_deko_renderer = false;
	int pending_dither_strength = 0;
	bool frame_pacing_enabled = false;
	std::mutex frame_signal_mutex;
	std::condition_variable frame_signal_cv;
	std::atomic<bool> new_frame_available{false};
	std::queue<int> frame_fifo;
	int last_displayed_index = 0;
	AVFrame *tmp_frame;
	std::unique_ptr<DekoVideoRenderer> deko_video_renderer;
		SDL_AudioDeviceID sdl_audio_device_id = 0;
		SDL_Event sdl_event;
		SDL_Joystick *sdl_joystick_ptr[SDL_JOYSTICK_COUNT] = {0};
		SDL_Haptic *sdl_haptic_ptr[2];
#ifdef __SWITCH__
		PadState pad;
		HidSixAxisSensorHandle sixaxis_handles[4];
		HidVibrationDeviceHandle vibration_handles[2][2];
#endif
		GLuint vao;
		GLuint vbo;
		GLuint tex[PLANES_COUNT];
		GLuint pbo[PLANES_COUNT];
		GLuint vert;
		GLuint frag;
		GLuint prog;
		bool InitOpenGl();
		bool InitOpenGlTextures();
		bool InitOpenGlTX1Textures();
		bool InitOpenGlShader();
		void OpenGlDraw();
#ifdef DEBUG_OPENGL
		void CheckGLError(const char *func, const char *file, int line);
		void DumpShaderError(GLuint prog, const char *func, const char *file, int line);
		void DumpProgramError(GLuint prog, const char *func, const char *file, int line);
#endif
		GLuint CreateAndCompileShader(GLenum type, const char *source);
		void SetOpenGlYUVPixels(AVFrame *frame);
		void SetOpenGlNV12Pixels(AVFrame *frame);
		bool ReadGameKeys(SDL_Event *event, ChiakiControllerState *state);
		bool ReadGameTouchScreen(ChiakiControllerState *state, std::map<uint32_t, int8_t> *finger_id_touch_id);
		bool ReadGameSixAxis(ChiakiControllerState *state);
	public:
		// singleton configuration
		IO(const IO&) = delete;
		void operator=(const IO&) = delete;
		static IO * GetInstance();
		int HapticBase = 400;
		std::atomic<bool> overlay_open{false};

		~IO();
		bool isFirst = true;
		void SetMesaConfig();
		bool VideoCB(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
		void SetDecodeQueueSize(int value);
		void SetAudioVolume(int value);
		void SetStickDeadzone(int value);
		void SetDithering(int strength);
		void SetFramePacing(bool enable);
		void SetAudioBufferMax(int value);
		void SetAudioBackend(int value);

		uint64_t GetFramesDecoded() { return frames_decoded.load(std::memory_order_relaxed); }
		uint64_t GetFramesRendered() { return frames_rendered.load(std::memory_order_relaxed); }
		uint32_t GetLastDecodeTimeUs() { return last_decode_time_us.load(std::memory_order_relaxed); }
		int GetAudioQueueBytes() { return audio_queue_bytes.load(std::memory_order_relaxed); }
		int GetDecodeQueueSize() { return frame_queue_size; }
		uint64_t GetTotalFramesLost() { return total_frames_lost.load(std::memory_order_relaxed); }
		uint64_t GetTotalFramesReceived() { return total_frames_received.load(std::memory_order_relaxed); }
		void ResetStats();
		bool HasDecodedFrame() { return has_decoded_frame.load(std::memory_order_relaxed); }
		void InitAudioCB(unsigned int channels, unsigned int rate);
		void AudioCB(int16_t *buf, size_t samples_count);
		bool InitVideo(int video_width, int video_height, int screen_width, int screen_height);
		bool InitAVCodec(bool is_PS5);
		bool FreeVideo();
		bool InitController();
		bool FreeController();
		bool MainLoop();
		void UpdateControllerState(ChiakiControllerState *state, std::map<uint32_t, int8_t> *finger_id_touch_id);
		void SetRumble(uint8_t left, uint8_t right);
		void SetHapticRumble(uint8_t left, uint8_t right);
		void HapticCB(uint8_t *buf, size_t buf_size);
		void CleanUpHaptic();
};

#endif //CHIAKI_IO_H
