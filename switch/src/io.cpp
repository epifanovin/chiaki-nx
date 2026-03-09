// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "io.h"
#include "settings.h"

#include <chrono>
#include <thread>

// https://github.com/torvalds/linux/blob/41ba50b0572e90ed3d24fe4def54567e9050bc47/drivers/hid/hid-sony.c#L2742
#define DS4_TRACKPAD_MAX_X 1920
#define DS4_TRACKPAD_MAX_Y 942
#define SWITCH_TOUCHSCREEN_MAX_X 1280
#define SWITCH_TOUCHSCREEN_MAX_Y 720

// source:
// gui/src/avopenglwidget.cpp
//
// examples :
// https://www.roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
// https://gist.github.com/roxlu/9329339

// use OpenGl to decode YUV
// the aim is to spare CPU load on nintendo switch

static const char *shader_vert_glsl = R"glsl(
#version 150 core
in vec2 pos_attr;
out vec2 uv_var;
void main()
{
	uv_var = pos_attr;
	gl_Position = vec4(pos_attr * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)glsl";

static const char *yuv420p_shader_frag_glsl = R"glsl(
#version 150 core
uniform sampler2D plane1; // Y
uniform sampler2D plane2; // U
uniform sampler2D plane3; // V
in vec2 uv_var;
out vec4 out_color;
void main()
{
	vec3 yuv = vec3(
		(texture(plane1, uv_var).r - (16.0 / 255.0)) / ((235.0 - 16.0) / 255.0),
		(texture(plane2, uv_var).r - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5,
		(texture(plane3, uv_var).r - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5);
	vec3 rgb = mat3(
		1.0,		1.0,		1.0,
 		0.0,		-0.18733,	1.85563,
 		1.57480,	-0.46812, 	0.0) * yuv;
	out_color = vec4(rgb, 1.0);
}
)glsl";

static const char *nv12_shader_frag_glsl = R"glsl(
#version 150 core

uniform sampler2D plane1; // Y
uniform sampler2D plane2; // interlaced UV

in vec2 uv_var;

out vec4 out_color;

void main()
{
	vec3 yuv = vec3(
		(texture(plane1, uv_var).r - (16.0 / 255.0)) / ((235.0 - 16.0) / 255.0),
		(texture(plane2, uv_var).r - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5,
		(texture(plane2, uv_var).g - (16.0 / 255.0)) / ((240.0 - 16.0) / 255.0) - 0.5
	);
	vec3 rgb = mat3(
		1.0,		1.0,		1.0,
 		0.0,		-0.18733,	1.85563,
 		1.57480,	-0.46812, 	0.0) * yuv;
	out_color = vec4(rgb, 1.0);
}
)glsl";

bool haptic_lock = false;
int haptic_val = 0;
std::chrono::system_clock::time_point haptic_lock_time;

static const float vert_pos[] = {
	0.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	1.0f, 1.0f};

IO *IO::instance = nullptr;
bool enableHWAccl = true;

IO *IO::GetInstance()
{
	if(instance == nullptr)
	{
		instance = new IO;
	}
	return instance;
}

IO::IO()
{
	Settings *settings = Settings::GetInstance();
	this->log = settings->GetLogger();
#if defined(CHIAKI_SWITCH_RENDERER_DEKO3D) && defined(BOREALIS_USE_DEKO3D)
	this->use_deko_renderer = true;
#else
	this->use_deko_renderer = false;
#endif
	if(!enableHWAccl)
		this->use_deko_renderer = false;
}

IO::~IO()
{
	//FreeJoystick();
	if(this->sdl_audio_device_id > 0)
	{
		SDL_CloseAudioDevice(this->sdl_audio_device_id);
	}
	FreeVideo();
}

void IO::SetDecodeQueueSize(int value)
{
	SetFrameQueueSize(value);
}

void IO::SetAudioVolume(int value)
{
	if(value < 0)
		value = 0;
	if(value > 200)
		value = 200;
	this->audio_volume = value;
}

void IO::SetStickDeadzone(int value)
{
	if(value < 0)
		value = 0;
	if(value > 30)
		value = 30;
	this->stick_deadzone = value;
}

void IO::SetDithering(int strength)
{
	this->pending_dither_strength = strength;
	if(this->deko_video_renderer)
		this->deko_video_renderer->SetDithering(strength);
}

void IO::SetFrameQueueSize(int value)
{
	if(value < 4)
		value = 4;
	if(value > MAX_FRAME_COUNT_MAX)
		value = MAX_FRAME_COUNT_MAX;
	this->frame_queue_size = value;
}

void IO::SetFifoDrainThreshold(int value)
{
	if(value < 2)
		value = 2;
	if(value > this->frame_queue_size - 2)
		value = this->frame_queue_size - 2;
	this->fifo_drain_threshold = value;
}

void IO::SetAudioBufferMax(int value)
{
	if(value < 4000)
		value = 4000;
	if(value > 32000)
		value = 32000;
	this->audio_buffer_max = value;
}

void IO::SetAudioBackend(int value)
{
	this->audio_backend_mode = value;
}

void IO::SetMesaConfig()
{
	//TRACE("%s", "Mesaconfig");
	//setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 1);
	//setenv("MESA_GLSL_VERSION_OVERRIDE", "330", 1);
	// Uncomment below to disable error checking and save CPU time (useful for production):
	//setenv("MESA_NO_ERROR", "1", 1);
#ifdef DEBUG_OPENGL
	// Uncomment below to enable Mesa logging:
	setenv("EGL_LOG_LEVEL", "debug", 1);
	setenv("MESA_VERBOSE", "all", 1);
	setenv("NOUVEAU_MESA_DEBUG", "1", 1);

	// Uncomment below to enable shader debugging in Nouveau:
	//setenv("NV50_PROG_OPTIMIZE", "0", 1);
	setenv("NV50_PROG_DEBUG", "1", 1);
	//setenv("NV50_PROG_CHIPSET", "0x120", 1);
#endif
}

#ifdef DEBUG_OPENGL
#define D(x)                                        \
	{                                               \
		(x);                                        \
		CheckGLError(__func__, __FILE__, __LINE__); \
	}
void IO::CheckGLError(const char *func, const char *file, int line)
{
	GLenum err;
	while((err = glGetError()) != GL_NO_ERROR)
	{
		CHIAKI_LOGE(this->log, "glGetError: %x function: %s from %s line %d", err, func, file, line);
		//GL_INVALID_VALUE, 0x0501
		// Given when a value parameter is not a legal value for that function. T
		// his is only given for local problems;
		// if the spec allows the value in certain circumstances,
		// where other parameters or state dictate those circumstances,
		// then GL_INVALID_OPERATION is the result instead.
	}
}

#define DS(x)                                             \
	{                                                     \
		DumpShaderError(x, __func__, __FILE__, __LINE__); \
	}
void IO::DumpShaderError(GLuint shader, const char *func, const char *file, int line)
{
	GLchar str[512 + 1];
	GLsizei len = 0;
	glGetShaderInfoLog(shader, 512, &len, str);
	if(len > 512)
		len = 512;
	str[len] = '\0';
	CHIAKI_LOGE(this->log, "glGetShaderInfoLog: %s function: %s from %s line %d", str, func, file, line);
}

#define DP(x)                                              \
	{                                                      \
		DumpProgramError(x, __func__, __FILE__, __LINE__); \
	}
void IO::DumpProgramError(GLuint prog, const char *func, const char *file, int line)
{
	GLchar str[512 + 1];
	GLsizei len = 0;
	glGetProgramInfoLog(prog, 512, &len, str);
	if(len > 512)
		len = 512;
	str[len] = '\0';
	CHIAKI_LOGE(this->log, "glGetProgramInfoLog: %s function: %s from %s line %d", str, func, file, line);
}

#else
// do nothing
#define D(x) \
	{        \
		(x); \
	}
#define DS(x) \
	{         \
	}
#define DP(x) \
	{         \
	}
#endif

bool IO::VideoCB(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	(void)frame_recovered;
	(void)user;

	this->total_frames_received.fetch_add(1, std::memory_order_relaxed);
	if(frames_lost > 0)
		this->total_frames_lost.fetch_add(frames_lost, std::memory_order_relaxed);

	auto decode_start = std::chrono::high_resolution_clock::now();

	AVPacket packet = {0};
	packet.data = buf;
	packet.size = (int)buf_size;

	int r = 0;
	int send_retry = 0;
	while((r = avcodec_send_packet(this->codec_context, &packet)) == AVERROR(EAGAIN) && send_retry < 2)
	{
		// Drain one queued frame to unblock decoder input without spinning.
		AVFrame *drain_frame = this->tmp_frame ? this->tmp_frame : this->frames[this->next_frame_index];
		int drain = avcodec_receive_frame(this->codec_context, drain_frame);
		if(drain == 0)
			av_frame_unref(drain_frame);
		send_retry++;
	}

	if(r < 0)
	{
		if(r == AVERROR(EAGAIN))
			return true;
		char errbuf[128];
		av_make_error_string(errbuf, sizeof(errbuf), r);
		CHIAKI_LOGE(this->log, "Failed to push frame: %s", errbuf);
		return false;
	}

	AVFrame *target_frame = this->frames[this->next_frame_index];
	av_frame_unref(target_frame);

	if(enableHWAccl && this->use_deko_renderer)
	{
		r = avcodec_receive_frame(this->codec_context, target_frame);
		if(r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return true;
		if(r < 0)
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(this->log, "Failed to pull hardware frame: %s", errbuf);
			return false;
		}
	}
	else if(enableHWAccl)
	{
		r = avcodec_receive_frame(this->codec_context, this->tmp_frame);
		if(r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return true;
		if(r < 0)
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(this->log, "Failed to pull frame: %s", errbuf);
			return false;
		}
		if(av_hwframe_transfer_data(target_frame, this->tmp_frame, 0) < 0)
		{
			// Some decoder paths already output software NV12.
			if(this->tmp_frame->format == AV_PIX_FMT_NV12 || this->tmp_frame->format == AV_PIX_FMT_P010LE)
			{
				av_frame_ref(target_frame, this->tmp_frame);
			}
			else
			{
				CHIAKI_LOGE(this->log, "Failed to transfer hardware frame to system memory");
				av_frame_unref(this->tmp_frame);
				return false;
			}
		}
		av_frame_copy_props(target_frame, this->tmp_frame);
		av_frame_unref(this->tmp_frame);
	}
	else
	{
		r = avcodec_receive_frame(this->codec_context, target_frame);
		if(r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return true;
		if(r < 0)
		{
			char errbuf[128];
			av_make_error_string(errbuf, sizeof(errbuf), r);
			CHIAKI_LOGE(this->log, "Failed to pull frame: %s", errbuf);
			return false;
		}
	}

	auto decode_end = std::chrono::high_resolution_clock::now();
	auto decode_us = std::chrono::duration_cast<std::chrono::microseconds>(decode_end - decode_start).count();
	this->last_decode_time_us.store((uint32_t)decode_us, std::memory_order_relaxed);
	this->frames_decoded.fetch_add(1, std::memory_order_relaxed);

	int decoded_index = this->next_frame_index;

	this->current_frame_index.store(decoded_index, std::memory_order_release);
	this->has_decoded_frame.store(true, std::memory_order_release);
	this->next_frame_index = (decoded_index + 1) % this->frame_queue_size;

	size_t fifo_sz;
	{
		std::lock_guard<std::mutex> lock(this->frame_signal_mutex);
		this->new_frame_available.store(true, std::memory_order_release);
		this->frame_fifo.push(decoded_index);
		fifo_sz = this->frame_fifo.size();
	}

	static auto last_decode_tp = std::chrono::high_resolution_clock::now();
	static int decode_log_counter = 0;
	auto now_d = std::chrono::high_resolution_clock::now();
	float decode_dt = std::chrono::duration<float, std::milli>(now_d - last_decode_tp).count();
	last_decode_tp = now_d;

	if(decode_log_counter++ % 60 == 0)
		CHIAKI_LOGI(this->log, "DECODE dt=%.1fms dec=%.1fms idx=%d fifo=%zu lost=%d",
			decode_dt, decode_us / 1000.0f, decoded_index, fifo_sz, (int)frames_lost);

	return true;
}

void IO::InitAudioCB(unsigned int channels, unsigned int rate)
{
#ifdef __SWITCH__
	if(this->audio_backend_mode == 1)
	{
		if(this->audren.Init(channels, rate))
		{
			CHIAKI_LOGI(this->log, "Audren audio backend initialized");
			return;
		}
		CHIAKI_LOGW(this->log, "Audren init failed, falling back to SDL");
	}
#endif

	SDL_AudioSpec want, have, test;
	SDL_memset(&want, 0, sizeof(want));

	want.freq = rate;
	want.format = AUDIO_S16SYS;
	want.channels = channels;
	want.samples = 1024;
	want.callback = NULL;

	if(this->sdl_audio_device_id <= 0)
	{
		// the chiaki session might be called many times
		// open the audio device only once
		this->sdl_audio_device_id = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	}

	if(this->sdl_audio_device_id <= 0)
	{
		CHIAKI_LOGE(this->log, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
	}
	else
	{
		SDL_PauseAudioDevice(this->sdl_audio_device_id, 0);
	}
}

void IO::AudioCB(int16_t *buf, size_t samples_count)
{
	float vol = this->audio_volume / 100.0f;

#ifdef __SWITCH__
	if(this->audio_backend_mode == 1)
	{
		this->audren.QueueAudio(buf, samples_count, vol);
		this->audio_queue_bytes.store(this->audren.GetQueuedBytes(), std::memory_order_relaxed);
		if(haptic_lock)
			CleanUpHaptic();
		return;
	}
#endif

	for(size_t x = 0; x < samples_count * 2; x++)
	{
		int sample = (int)(buf[x] * vol);
		if(sample > INT16_MAX)
			buf[x] = INT16_MAX;
		else if(sample < INT16_MIN)
			buf[x] = INT16_MIN;
		else
			buf[x] = (int16_t)sample;
	}

	int audio_queued_size = SDL_GetQueuedAudioSize(this->sdl_audio_device_id);
	this->audio_queue_bytes.store(audio_queued_size, std::memory_order_relaxed);
	if(audio_queued_size > this->audio_buffer_max)
	{
		CHIAKI_LOGW(this->log, "Triggering SDL_ClearQueuedAudio with queue size = %d (max %d)", audio_queued_size, this->audio_buffer_max);
		SDL_ClearQueuedAudio(this->sdl_audio_device_id);
	}

	int success = SDL_QueueAudio(this->sdl_audio_device_id, buf, sizeof(int16_t) * samples_count * 2);
	if(success != 0)
		CHIAKI_LOGE(this->log, "SDL_QueueAudio failed: %s\n", SDL_GetError());

	// check haptic
	if (haptic_lock) {
		CleanUpHaptic();
	}
}

bool IO::InitVideo(int video_width, int video_height, int screen_width, int screen_height)
{
	CHIAKI_LOGI(this->log, "load InitVideo");
	this->video_width = video_width;
	this->video_height = video_height;

	this->screen_width = screen_width;
	this->screen_height = screen_height;
	this->frames = (AVFrame **)malloc(this->frame_queue_size * sizeof(AVFrame *));
	if(!this->frames)
		return false;

	this->frames_have_sw_buffers = !this->use_deko_renderer;
	this->current_frame_index.store(0, std::memory_order_release);
	this->has_decoded_frame.store(false, std::memory_order_release);
	this->fifo_primed.store(false, std::memory_order_release);
	this->next_frame_index = 0;
	{
		std::lock_guard<std::mutex> lock(this->frame_signal_mutex);
		this->new_frame_available.store(false, std::memory_order_release);
		while(!this->frame_fifo.empty())
			this->frame_fifo.pop();
		this->last_displayed_index = 0;
	}

	for(int i = 0; i < this->frame_queue_size; i++)
	{
		this->frames[i] = av_frame_alloc();
		if(this->frames[i] == NULL)
		{
			CHIAKI_LOGE(this->log, "FFmpeg: Couldn't allocate frame");
			return false;
		}
		this->frames[i]->format = this->use_deko_renderer ? AV_PIX_FMT_NVTEGRA : AV_PIX_FMT_NV12;
		this->frames[i]->width = video_width;
		this->frames[i]->height = video_height;

		if(this->frames_have_sw_buffers)
		{
			int err = av_frame_get_buffer(this->frames[i], 256);
			if(err < 0)
			{
				CHIAKI_LOGE(this->log, "FFmpeg: Couldn't allocate frame buffer");
				return false;
			}
			for(int j = 0; j < MAX_NV12_PLANE_COUNT; j++)
			{
				uintptr_t ptr = (uintptr_t)this->frames[i]->data[j];
				this->origin_ptr[i][j] = ptr;
				uintptr_t dst = (((ptr)+(256)-1)&~((256)-1));
				uintptr_t gap = dst - ptr;
				this->frames[i]->data[j] += gap;
			}
		}
	}
	this->tmp_frame = av_frame_alloc();

	if(this->use_deko_renderer)
	{
		this->deko_video_renderer.reset(new DekoVideoRenderer());
		this->deko_video_renderer->SetDithering(this->pending_dither_strength);
	}
	else if(!InitOpenGl())
	{
		throw Exception("Failed to initiate OpenGl");
	}
	return true;
}

bool IO::FreeVideo()
{
	bool ret = true;

	if (this->hw_device_ctx) {
			av_buffer_unref(&this->hw_device_ctx);
	}

	if(this->deko_video_renderer)
	{
		this->deko_video_renderer->Reset();
		this->deko_video_renderer.reset();
	}

	if(this->frames != NULL) {
		for(int i = 0; i < this->frame_queue_size; i++) {
			if(this->frames[i]) {
				if(this->frames_have_sw_buffers)
				{
					for(int j = 0; j < MAX_NV12_PLANE_COUNT; j++)
					{
						this->frames[i]->data[j] = (uint8_t *)this->origin_ptr[i][j];
					}
				}
				av_frame_free(&this->frames[i]);
			}
		}
			free(this->frames);
			this->frames = nullptr;
	}
	this->has_decoded_frame.store(false, std::memory_order_release);

	if(this->tmp_frame)
		av_frame_free(&this->tmp_frame);

	// avcodec_alloc_context3(codec);
	if(this->codec_context)
	{
		avcodec_free_context(&this->codec_context);
	}

	return ret;
}

bool IO::ReadGameTouchScreen(ChiakiControllerState *chiaki_state, std::map<uint32_t, int8_t> *finger_id_touch_id)
{
#ifdef __SWITCH__
	HidTouchScreenState sw_state = {0};

	bool ret = false;
	hidGetTouchScreenStates(&sw_state, 1);
	// scale switch screen to the PS trackpad
	chiaki_state->buttons &= ~CHIAKI_CONTROLLER_BUTTON_TOUCHPAD; // touchscreen release

	// un-touch all old touches
	for(auto it = finger_id_touch_id->begin(); it != finger_id_touch_id->end();)
	{
		auto cur = it;
		it++;
		for(int i = 0; i < sw_state.count; i++)
		{
			if(sw_state.touches[i].finger_id == cur->first)
				goto cont;
		}
		if(cur->second >= 0)
			chiaki_controller_state_stop_touch(chiaki_state, (uint8_t)cur->second);
		finger_id_touch_id->erase(cur);
cont:
		continue;
	}


	// touch or update all current touches
	for(int i = 0; i < sw_state.count; i++)
	{
		uint16_t x = sw_state.touches[i].x * ((float)DS4_TRACKPAD_MAX_X / (float)SWITCH_TOUCHSCREEN_MAX_X);
		uint16_t y = sw_state.touches[i].y * ((float)DS4_TRACKPAD_MAX_Y / (float)SWITCH_TOUCHSCREEN_MAX_Y);
		// use nintendo switch border's 5% to trigger the touchpad button
		if(x <= (DS4_TRACKPAD_MAX_X * 0.05) || x >= (DS4_TRACKPAD_MAX_X * 0.95) || y <= (DS4_TRACKPAD_MAX_Y * 0.05) || y >= (DS4_TRACKPAD_MAX_Y * 0.95))
			chiaki_state->buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD; // touchscreen

		auto it = finger_id_touch_id->find(sw_state.touches[i].finger_id);
		if(it == finger_id_touch_id->end())
		{
			// new touch
			(*finger_id_touch_id)[sw_state.touches[i].finger_id] =
				chiaki_controller_state_start_touch(chiaki_state, x, y);
		}
		else if(it->second >= 0)
			chiaki_controller_state_set_touch_pos(chiaki_state, (uint8_t)it->second, x, y);
		// it->second < 0 ==> touch ignored because there were already too many multi-touches
		ret = true;
	}
	return ret;
#else
	return false;
#endif
}

void IO::SetRumble(uint8_t left, uint8_t right)
{
#ifdef __SWITCH__
	Result rc = 0;
	HidVibrationValue vibration_values[] = {
		{
			.amp_low = 0.0f,
			.freq_low = 160.0f,
			.amp_high = 0.0f,
			.freq_high = 200.0f,
		},
		{
			.amp_low = 0.0f,
			.freq_low = 160.0f,
			.amp_high = 0.0f,
			.freq_high = 200.0f,
		}};

	int target_device = padIsHandheld(&pad) ? 0 : 1;
	if(left > 160) left = 160;
	if(left > 0)
	{
		// SDL_HapticRumblePlay(this->sdl_haptic_ptr[0], left / 100, 5000);
		float l = (float)left / 255.0;
		vibration_values[0].amp_low = l;
		vibration_values[0].freq_low *= l;
		vibration_values[0].amp_high = l;
		vibration_values[0].freq_high *= l;
	}

	if(right > 160) right = 160;
	if(right > 0)
	{
		// SDL_HapticRumblePlay(this->sdl_haptic_ptr[1], right / 100, 5000);
		float r = (float)right / 255.0;
		vibration_values[1].amp_low = r;
		vibration_values[1].freq_low *= r;
		vibration_values[1].amp_high = r;
		vibration_values[1].freq_high *= r;
	}

	rc = hidSendVibrationValues(this->vibration_handles[target_device], vibration_values, 2);
	if(R_FAILED(rc))
		CHIAKI_LOGE(this->log, "hidSendVibrationValues() returned: 0x%x", rc);

#endif
}

void IO::HapticCB(uint8_t *buf, size_t buf_size) {
		int16_t amplitudel = 0, amplituder = 0;
		int32_t suml = 0, sumr = 0;
		const size_t sample_size = 2 * sizeof(int16_t); // stereo samples

		size_t buf_count = buf_size / sample_size;
		for (size_t i = 0; i < buf_count; i++){
			size_t cur = i * sample_size;

			memcpy(&amplitudel, buf + cur, sizeof(int16_t));
			memcpy(&amplituder, buf + cur + sizeof(int16_t), sizeof(int16_t));
			suml += amplitudel;
			sumr += amplituder;
		}
		uint16_t left = 0, right = 0;
		left = suml / buf_count;
		right = sumr / buf_count;
		SetHapticRumble(left, right);
		if ((left != 0 || right != 0) && !haptic_lock) {
			haptic_lock = true;
		}
}

void IO::SetHapticRumble(uint8_t left, uint8_t right)
{
	uint8_t val = left > right ? left : right;
	haptic_val = val;
	haptic_lock_time = std::chrono::high_resolution_clock::now(); 
	
#ifdef __SWITCH__
	Result rc = 0;
	HidVibrationValue vibration_values[] = {
		{
			.amp_low = 0.0f,
			.freq_low = 160.0f,
			.amp_high = 0.0f,
			.freq_high = 200.0f,
		},
		{
			.amp_low = 0.0f,
			.freq_low = 160.0f,
			.amp_high = 0.0f,
			.freq_high = 200.0f,
		}};

	int target_device = padIsHandheld(&pad) ? 0 : 1;
	for (int i = 0; i < 2; i++) {
		float index = (float)val / (float)HapticBase;
		vibration_values[i].amp_low = index;
		vibration_values[i].amp_high = index;
		if (val != 0) {
			vibration_values[i].freq_low *= index;
			vibration_values[i].freq_high *= index;
		}
	}
	// CHIAKI_LOGW(this->log, "haptic rumble param: %f %f %f %f",
	// 	vibration_values[0].amp_low, vibration_values[0].amp_high,
	// 	vibration_values[0].freq_low, vibration_values[0].freq_high);
	
	rc = hidSendVibrationValues(this->vibration_handles[target_device], vibration_values, 2);
#endif
}

void IO::CleanUpHaptic() {
	std::chrono::system_clock::time_point now = std::chrono::high_resolution_clock::now();
	auto dur = now - haptic_lock_time;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
	if (haptic_val == 0) {
		haptic_lock = false;
	} else if (ms > 30) {
		SetHapticRumble(0, 0);
		haptic_lock = false;
	}
}

bool IO::ReadGameSixAxis(ChiakiControllerState *state)
{
#ifdef __SWITCH__
	// Read from the correct sixaxis handle depending on the current input style
	HidSixAxisSensorState sixaxis = {0};
	uint64_t style_set = padGetStyleSet(&pad);
	if(style_set & HidNpadStyleTag_NpadHandheld)
		hidGetSixAxisSensorStates(this->sixaxis_handles[0], &sixaxis, 1);
	else if(style_set & HidNpadStyleTag_NpadFullKey)
		hidGetSixAxisSensorStates(this->sixaxis_handles[1], &sixaxis, 1);
	else if(style_set & HidNpadStyleTag_NpadJoyDual)
	{
		// For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
		u64 attrib = padGetAttributes(&pad);
		if(attrib & HidNpadAttribute_IsLeftConnected)
			hidGetSixAxisSensorStates(this->sixaxis_handles[2], &sixaxis, 1);
		else if(attrib & HidNpadAttribute_IsRightConnected)
			hidGetSixAxisSensorStates(this->sixaxis_handles[3], &sixaxis, 1);
	}

	state->gyro_x = sixaxis.angular_velocity.x * 2.0f * M_PI;
	state->gyro_y = sixaxis.angular_velocity.z * 2.0f * M_PI;
	state->gyro_z = -sixaxis.angular_velocity.y * 2.0f * M_PI;
	state->accel_x = -sixaxis.acceleration.x;
	state->accel_y = -sixaxis.acceleration.z;
	state->accel_z = sixaxis.acceleration.y;

	// https://d3cw3dd2w32x2b.cloudfront.net/wp-content/uploads/2015/01/matrix-to-quat.pdf
	float (*dm)[3] = sixaxis.direction.direction;
	float m[3][3] = {
		{ dm[0][0], dm[2][0], dm[1][0] },
		{ dm[0][2], dm[2][2], dm[1][2] },
		{ dm[0][1], dm[2][1], dm[1][1] }
	};
	std::array<float, 4> q;
	float t;
	if(m[2][2] < 0)
	{
		if (m[0][0] > m[1][1])
		{
			t = 1 + m[0][0] - m[1][1] - m[2][2];
			q = { t, m[0][1] + m[1][0], m[2][0] + m[0][2], m[1][2] - m[2][1] };
		}
		else
		{
			t = 1 - m[0][0] + m[1][1] -m[2][2];
			q = { m[0][1] + m[1][0], t, m[1][2] + m[2][1], m[2][0] - m[0][2] };
		}
	}
	else
	{
		if(m[0][0] < -m[1][1])
		{
			t = 1 - m[0][0] - m[1][1] + m[2][2];
			q = { m[2][0] + m[0][2], m[1][2] + m[2][1], t, m[0][1] - m[1][0] };
		}
		else
		{
			t = 1 + m[0][0] + m[1][1] + m[2][2];
			q = { m[1][2] - m[2][1], m[2][0] - m[0][2], m[0][1] - m[1][0], t };
		}
	}
	float fac = 0.5f / sqrt(t);
	state->orient_x = q[0] * fac;
	state->orient_y = q[1] * fac;
	state->orient_z = -q[2] * fac;
	state->orient_w = q[3] * fac;
	return true;
#else
	return false;
#endif
}

static int16_t applyDeadzone(int16_t value, int deadzone_percent)
{
	if(deadzone_percent <= 0)
		return value;
	int threshold = (32767 * deadzone_percent) / 100;
	if(abs(value) < threshold)
		return 0;
	float sign = value > 0 ? 1.0f : -1.0f;
	return (int16_t)(sign * ((abs(value) - threshold) * 32767.0f / (32767 - threshold)));
}

bool IO::ReadGameKeys(SDL_Event *event, ChiakiControllerState *state)
{
	bool ret = true;
	int dz = this->stick_deadzone;
	switch(event->type)
	{
		case SDL_JOYAXISMOTION:
			if(event->jaxis.which == 0)
			{
				if(event->jaxis.axis == 0)
					state->left_x = applyDeadzone(event->jaxis.value, dz);
				else if(event->jaxis.axis == 1)
					state->left_y = applyDeadzone(event->jaxis.value, dz);
				else if(event->jaxis.axis == 2)
					state->right_x = applyDeadzone(event->jaxis.value, dz);
				else if(event->jaxis.axis == 3)
					state->right_y = applyDeadzone(event->jaxis.value, dz);
				else
					ret = false;
			}
			else if(event->jaxis.which == 1)
			{
				if(event->jaxis.axis == 0)
					state->right_x = applyDeadzone(event->jaxis.value, dz);
				else if(event->jaxis.axis == 1)
					state->right_y = applyDeadzone(event->jaxis.value, dz);
				else
					ret = false;
			}
			else
				ret = false;
			break;
		case SDL_JOYBUTTONDOWN:
			// printf("Joystick %d button %d DOWN\n",
			// 	event->jbutton.which, event->jbutton.button);
			switch(event->jbutton.button)
			{
				case 0:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
					break; // KEY_A
				case 1:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
					break; // KEY_B
				case 2:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
					break; // KEY_X
				case 3:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
					break; // KEY_Y
				case 12:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
					break; // KEY_DLEFT
				case 14:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
					break; // KEY_DRIGHT
				case 13:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
					break; // KEY_DUP
				case 15:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
					break; // KEY_DDOWN
				case 6:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
					break; // KEY_L
				case 7:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
					break; // KEY_R
				case 8:
					state->l2_state = 0xff;
					break; // KEY_ZL
				case 9:
					state->r2_state = 0xff;
					break; // KEY_ZR
				case 4:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
					break; // KEY_LSTICK
				case 5:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_R3;
					break; // KEY_RSTICK
				case 10:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
					break; // KEY_PLUS
				// FIXME
				// case 11: state->buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE; break; // KEY_MINUS
				case 11:
					state->buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
					break; // KEY_MINUS
				default:
					ret = false;
			}
			break;
		case SDL_JOYBUTTONUP:
			// printf("Joystick %d button %d UP\n",
			// 	event->jbutton.which, event->jbutton.button);
			switch(event->jbutton.button)
			{
				case 0:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_MOON;
					break; // KEY_A
				case 1:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_CROSS;
					break; // KEY_B
				case 2:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
					break; // KEY_X
				case 3:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_BOX;
					break; // KEY_Y
				case 12:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
					break; // KEY_DLEFT
				case 14:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
					break; // KEY_DRIGHT
				case 13:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
					break; // KEY_DUP
				case 15:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
					break; // KEY_DDOWN
				case 6:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_L1;
					break; // KEY_L
				case 7:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_R1;
					break; // KEY_R
				case 8:
					state->l2_state = 0x00;
					break; // KEY_ZL
				case 9:
					state->r2_state = 0x00;
					break; // KEY_ZR
				case 4:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_L3;
					break; // KEY_LSTICK
				case 5:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_R3;
					break; // KEY_RSTICK
				case 10:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
					break; // KEY_PLUS
						   //case 11: state->buttons ^= CHIAKI_CONTROLLER_BUTTON_SHARE; break; // KEY_MINUS
				case 11:
					state->buttons ^= CHIAKI_CONTROLLER_BUTTON_PS;
					break; // KEY_MINUS
				default:
					ret = false;
			}
			break;
		default:
			ret = false;
	}
	return ret;
}

bool IO::InitAVCodec(bool is_PS5)
{
	CHIAKI_LOGI(this->log, "loading AVCodec");
	if (is_PS5) {
		this->codec = avcodec_find_decoder_by_name("hevc");
	} else {
		this->codec = avcodec_find_decoder_by_name("h264");
	}
	if(!this->codec)
		throw Exception("H265 Codec not available");
	CHIAKI_LOGI(this->log, "get codec %s", this->codec->name);

	this->codec_context = avcodec_alloc_context3(codec);
	if(!this->codec_context)
		throw Exception("Failed to alloc codec context");

	if (enableHWAccl) {
		this->codec_context->skip_loop_filter = AVDISCARD_ALL;
		this->codec_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
		this->codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
	} else {
		// use rock88's mooxlight-nx optimization
		// https://github.com/rock88/moonlight-nx/blob/698d138b9fdd4e483c998254484ccfb4ec829e95/src/streaming/ffmpeg/FFmpegVideoDecoder.cpp#L63
		// this->codec_context->skip_loop_filter = AVDISCARD_ALL;
		this->codec_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
		this->codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
		// this->codec_context->flags2 |= AV_CODEC_FLAG2_CHUNKS;
		this->codec_context->thread_type = FF_THREAD_SLICE;
		this->codec_context->thread_count = 4;
	}

	if (enableHWAccl) {
		if(av_hwdevice_ctx_create(&this->hw_device_ctx, AV_HWDEVICE_TYPE_NVTEGRA, NULL, NULL, 0) < 0)
			throw Exception("Failed to enable hardware encoding");
		this->codec_context->hw_device_ctx = av_buffer_ref(this->hw_device_ctx);
		this->codec_context->pix_fmt = this->use_deko_renderer ? AV_PIX_FMT_NVTEGRA : AV_PIX_FMT_NV12;
	}

	if(avcodec_open2(this->codec_context, this->codec, nullptr) < 0)
	{
		avcodec_free_context(&this->codec_context);
		throw Exception("Failed to open codec context");
	}
	return true;
}

bool IO::InitOpenGl()
{
	CHIAKI_LOGI(this->log, "loading OpenGL");
	isFirst = true;

	if(!InitOpenGlShader())
		return false;
	
	if (enableHWAccl) {
		if(!InitOpenGlTX1Textures()) {
			return false;
		}
	} else {
		if(!InitOpenGlTextures()) {
			return false;
		}
	}


	return true;
}

bool IO::InitOpenGlTextures()
{
	CHIAKI_LOGV(this->log, "loading OpenGL textrures");

	D(glGenTextures(PLANES_COUNT, this->tex));
	D(glGenBuffers(PLANES_COUNT, this->pbo));
	uint8_t uv_default[] = {0x7f, 0x7f};
	for(int i = 0; i < PLANES_COUNT; i++)
	{
		D(glBindTexture(GL_TEXTURE_2D, this->tex[i]));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		D(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, i > 0 ? uv_default : nullptr));
	}

	D(glUseProgram(this->prog));
	// bind only as many planes as we need
	const char *plane_names[] = {"plane1", "plane2", "plane3"};
	for(int i = 0; i < PLANES_COUNT; i++)
		D(glUniform1i(glGetUniformLocation(this->prog, plane_names[i]), i));

	D(glGenVertexArrays(1, &this->vao));
	D(glBindVertexArray(this->vao));

	D(glGenBuffers(1, &this->vbo));
	D(glBindBuffer(GL_ARRAY_BUFFER, this->vbo));
	D(glBufferData(GL_ARRAY_BUFFER, sizeof(vert_pos), vert_pos, GL_STATIC_DRAW));

	D(glBindBuffer(GL_ARRAY_BUFFER, this->vbo));
	D(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr));
	D(glEnableVertexAttribArray(0));

	D(glCullFace(GL_BACK));
	D(glEnable(GL_CULL_FACE));
	D(glClearColor(0.5, 0.5, 0.5, 1.0));
	return true;
}

bool IO::InitOpenGlTX1Textures()
{
	CHIAKI_LOGV(this->log, "loading OpenGL TX1 textrures");

	int planes[][5] = {
		// { width_divide, height_divider, data_per_pixel }
			{ 1, 1, 1, GL_R8, GL_RED },
			{ 2, 2, 2, GL_RG8, GL_RG }
	};

	D(glGenTextures(2, this->tex));
	D(glGenBuffers(2, this->pbo));
	uint8_t uv_default[] = {0x7f, 0x7f};
	for(int i = 0; i < MAX_NV12_PLANE_COUNT; i++)
	{
		D(glBindTexture(GL_TEXTURE_2D, this->tex[i]));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		D(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		D(glTexImage2D(GL_TEXTURE_2D, 0, planes[i][3], 1, 1, 0, planes[i][4], GL_UNSIGNED_BYTE, i > 0 ? uv_default : nullptr));
	}

	D(glUseProgram(this->prog));
	// bind only as many planes as we need
	const char *plane_names[] = {"plane1", "plane2", "plane3"};
	for(int i = 0; i < MAX_NV12_PLANE_COUNT; i++) {
		m_texture_uniform[i] = glGetUniformLocation(this->prog, plane_names[i]);
		D(glUniform1i(m_texture_uniform[i], i));
	}

	D(glGenVertexArrays(1, &this->vao));
	D(glBindVertexArray(this->vao));

	D(glGenBuffers(1, &this->vbo));
	D(glBindBuffer(GL_ARRAY_BUFFER, this->vbo));
	D(glBufferData(GL_ARRAY_BUFFER, sizeof(vert_pos), vert_pos, GL_STATIC_DRAW));

	D(glBindBuffer(GL_ARRAY_BUFFER, this->vbo));
	D(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr));
	D(glEnableVertexAttribArray(0));

	D(glCullFace(GL_BACK));
	D(glEnable(GL_CULL_FACE));
	return true;
}

GLuint IO::CreateAndCompileShader(GLenum type, const char *source)
{
	GLint success;
	GLchar msg[512];

	GLuint handle;
	D(handle = glCreateShader(type));
	if(!handle)
	{
		CHIAKI_LOGE(this->log, "%u: cannot create shader", type);
		DP(this->prog);
	}

	D(glShaderSource(handle, 1, &source, nullptr));
	D(glCompileShader(handle));
	D(glGetShaderiv(handle, GL_COMPILE_STATUS, &success));

	if(!success)
	{
		D(glGetShaderInfoLog(handle, sizeof(msg), nullptr, msg));
		CHIAKI_LOGE(this->log, "%u: %s\n", type, msg);
		D(glDeleteShader(handle));
	}

	return handle;
}

bool IO::InitOpenGlShader()
{
	CHIAKI_LOGV(this->log, "loading OpenGl Shaders");

	D(this->vert = CreateAndCompileShader(GL_VERTEX_SHADER, shader_vert_glsl));
	if (enableHWAccl) {
		D(this->frag = CreateAndCompileShader(GL_FRAGMENT_SHADER, nv12_shader_frag_glsl));
	} else {
		D(this->frag = CreateAndCompileShader(GL_FRAGMENT_SHADER, yuv420p_shader_frag_glsl));
	}

	D(this->prog = glCreateProgram());

	D(glAttachShader(this->prog, this->vert));
	D(glAttachShader(this->prog, this->frag));
	D(glBindAttribLocation(this->prog, 0, "pos_attr"));
	D(glLinkProgram(this->prog));

	GLint success;
	D(glGetProgramiv(this->prog, GL_LINK_STATUS, &success));
	if(!success)
	{
		char buf[512];
		glGetProgramInfoLog(this->prog, sizeof(buf), nullptr, buf);
		CHIAKI_LOGE(this->log, "OpenGL link error: %s", buf);
		return false;
	}

	D(glDeleteShader(this->vert));
	D(glDeleteShader(this->frag));

	return true;
}

inline void IO::SetOpenGlYUVPixels(AVFrame *frame)
{
	D(glUseProgram(this->prog));

	int planes[][3] = {
		// { width_divide, height_divider, data_per_pixel }
		{1, 1, 1}, // Y
		{2, 2, 1}, // U
		{2, 2, 1}  // V
	};

	for(int i = 0; i < PLANES_COUNT; i++)
	{
		int width = frame->width / planes[i][0];
		int height = frame->height / planes[i][1];
		int size = width * height * planes[i][2];
		uint8_t *buf;

		D(glBindBuffer(GL_PIXEL_UNPACK_BUFFER, this->pbo[i]));
		D(glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW));
		D(buf = reinterpret_cast<uint8_t *>(glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT)));
		if(!buf)
		{
			GLint data;
			D(glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER, GL_BUFFER_SIZE, &data));
			CHIAKI_LOGE(this->log, "AVOpenGLFrame failed to map PBO");
			CHIAKI_LOGE(this->log, "Info buf == %p. size %d frame %d * %d, divs %d, %d, pbo %d GL_BUFFER_SIZE %x",
				buf, size, frame->width, frame->height, planes[i][0], planes[i][1], pbo[i], data);
			continue;
		}

		if(frame->linesize[i] == width)
		{
			// Y
			memcpy(buf, frame->data[i], size);
		}
		else
		{
			// UV
			for(int l = 0; l < height; l++)
				memcpy(buf + width * l * planes[i][2],
					frame->data[i] + frame->linesize[i] * l,
					width * planes[i][2]);
		}
		D(glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER));
		D(glBindTexture(GL_TEXTURE_2D, tex[i]));
		D(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr));
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
}
inline void IO::SetOpenGlNV12Pixels(AVFrame *frame)
{
	D(glUseProgram(this->prog));

	int planes[][5] = {
		// { width_divide, height_divider, data_per_pixel }
			{ 1, 1, 1, GL_R8, GL_RED },
			{ 2, 2, 2, GL_RG8, GL_RG }
	};

	for (int i = 0; i < MAX_NV12_PLANE_COUNT; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		int real_width = frame->linesize[i] / planes[i][0];
		int width = frame->width / planes[i][0];
		int height = frame->height / planes[i][1];
		D(glBindTexture(GL_TEXTURE_2D, tex[i]));
		glPixelStorei(GL_UNPACK_ROW_LENGTH, real_width);
		if (isFirst) {
			D(glTexImage2D(GL_TEXTURE_2D, 0, planes[i][3], width, height, 0, planes[i][4], GL_UNSIGNED_BYTE, frame->data[i]));
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width,
											height, planes[i][4], GL_UNSIGNED_BYTE, frame->data[i]);
		}
		glUniform1i(m_texture_uniform[i], i);
		D(glBindTexture(GL_TEXTURE_2D, 0));
	}

	isFirst = false;
}

inline void IO::OpenGlDraw()
{
	glClear(GL_COLOR_BUFFER_BIT);
	if(!this->has_decoded_frame.load(std::memory_order_acquire))
		return;

	int frame_index = this->current_frame_index.load(std::memory_order_acquire);
	AVFrame *frame = this->frames ? this->frames[frame_index] : nullptr;
	if(!frame)
		return;

	if (enableHWAccl) {
		SetOpenGlNV12Pixels(frame);
	} else {
		// send to OpenGl
		SetOpenGlYUVPixels(frame);
	}

	//avcodec_flush_buffers(this->codec_context);
	D(glBindVertexArray(this->vao));

	for(int i = 0; i < PLANES_COUNT; i++)
	{
		D(glActiveTexture(GL_TEXTURE0 + i));
		D(glBindTexture(GL_TEXTURE_2D, this->tex[i]));
	}

	D(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
	D(glBindVertexArray(0));
}

bool IO::InitController()
{
	// https://github.com/switchbrew/switch-examples/blob/master/graphics/sdl2/sdl2-simple/source/main.cpp#L57
	// open CONTROLLER_PLAYER_1 and CONTROLLER_PLAYER_2
	// when railed, both joycons are mapped to joystick #0,
	// else joycons are individually mapped to joystick #0, joystick #1, ...
	for(int i = 0; i < SDL_JOYSTICK_COUNT; i++)
	{
		this->sdl_joystick_ptr[i] = SDL_JoystickOpen(i);
		if(sdl_joystick_ptr[i] == nullptr)
		{
			CHIAKI_LOGE(this->log, "SDL_JoystickOpen: %s\n", SDL_GetError());
			return false;
		}
		// this->sdl_haptic_ptr[i] = SDL_HapticOpenFromJoystick(sdl_joystick_ptr[i]);
		// if(sdl_haptic_ptr[i] == nullptr)
		// {
		// 	CHIAKI_LOGE(this->log, "SDL_HapticRumbleInit: %s\n", SDL_GetError());
		// } else {
		// 	SDL_HapticRumbleInit(this->sdl_haptic_ptr[i]);
		// }
	}
#ifdef __SWITCH__
Result rc = 0;
	// Configure our supported input layout: a single player with standard controller styles
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);

	// Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
	padInitializeDefault(&this->pad);
	// touchpad
	hidInitializeTouchScreen();
	// It's necessary to initialize these separately as they all have different handle values
	hidGetSixAxisSensorHandles(&this->sixaxis_handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
	hidGetSixAxisSensorHandles(&this->sixaxis_handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
	hidGetSixAxisSensorHandles(&this->sixaxis_handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
	hidStartSixAxisSensor(this->sixaxis_handles[0]);
	hidStartSixAxisSensor(this->sixaxis_handles[1]);
	hidStartSixAxisSensor(this->sixaxis_handles[2]);
	hidStartSixAxisSensor(this->sixaxis_handles[3]);

    rc = hidInitializeVibrationDevices(this->vibration_handles[0], 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
	if(R_FAILED(rc))
		CHIAKI_LOGE(this->log, "hidInitializeVibrationDevices() HidNpadIdType_Handheld returned: 0x%x", rc);

    rc = hidInitializeVibrationDevices(this->vibration_handles[1], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
	if(R_FAILED(rc))
		CHIAKI_LOGE(this->log, "hidInitializeVibrationDevices() HidNpadIdType_No1 returned: 0x%x", rc);

#endif
	return true;
}

bool IO::FreeController()
{
	for(int i = 0; i < SDL_JOYSTICK_COUNT; i++)
	{
		SDL_JoystickClose(this->sdl_joystick_ptr[i]);
		// SDL_HapticClose(this->sdl_haptic_ptr[i]);
	}
#ifdef __SWITCH__
	hidStopSixAxisSensor(this->sixaxis_handles[0]);
	hidStopSixAxisSensor(this->sixaxis_handles[1]);
	hidStopSixAxisSensor(this->sixaxis_handles[2]);
	hidStopSixAxisSensor(this->sixaxis_handles[3]);
#endif
	return true;
}

void IO::UpdateControllerState(ChiakiControllerState *state, std::map<uint32_t, int8_t> *finger_id_touch_id)
{
#ifdef __SWITCH__
	padUpdate(&this->pad);
#endif
	// handle SDL events
	while(SDL_PollEvent(&this->sdl_event))
	{
		this->ReadGameKeys(&this->sdl_event, state);
		switch(this->sdl_event.type)
		{
			case SDL_QUIT:
				this->quit = true;
		}
	}

	ReadGameTouchScreen(state, finger_id_touch_id);
	ReadGameSixAxis(state);
}

bool IO::MainLoop()
{
	if(this->use_deko_renderer && this->deko_video_renderer)
	{
		if(!this->has_decoded_frame.load(std::memory_order_acquire))
			return !this->quit;

		if(!this->fifo_primed.load(std::memory_order_acquire))
		{
			std::lock_guard<std::mutex> lock(this->frame_signal_mutex);
			if((int)this->frame_fifo.size() >= this->fifo_drain_threshold)
				this->fifo_primed.store(true, std::memory_order_release);
			else
				return !this->quit;
		}

		int frame_index;
		bool underflow = false;
		int drained = 0;
		size_t depth_before = 0;

		if(!this->overlay_open)
		{
			std::lock_guard<std::mutex> lock(this->frame_signal_mutex);
			depth_before = this->frame_fifo.size();

			while((int)this->frame_fifo.size() > this->fifo_drain_threshold)
			{
				this->frame_fifo.pop();
				drained++;
			}

			if(!this->frame_fifo.empty())
			{
				frame_index = this->frame_fifo.front();
				this->frame_fifo.pop();
				this->last_displayed_index = frame_index;
			}
			else
			{
				frame_index = this->last_displayed_index;
				underflow = true;
			}
		}
		else
		{
			frame_index = this->current_frame_index.load(std::memory_order_acquire);
		}

		AVFrame *frame = this->frames ? this->frames[frame_index] : nullptr;
		if(frame)
			this->deko_video_renderer->Draw(frame, this->screen_width, this->screen_height);

		static auto last_render_tp = std::chrono::high_resolution_clock::now();
		static int render_log_counter = 0;
		static int total_normal = 0, total_uflow = 0, total_drained = 0;
		static float dt_sum = 0;
		static int depth_sum = 0;

		auto now_r = std::chrono::high_resolution_clock::now();
		float render_dt = std::chrono::duration<float, std::milli>(now_r - last_render_tp).count();
		last_render_tp = now_r;

		dt_sum += render_dt;
		depth_sum += (int)depth_before;
		if(underflow)
			total_uflow++;
		else if(drained > 0)
			total_drained++;
		else
			total_normal++;

		if(++render_log_counter >= 120)
		{
			float avg_dt = dt_sum / render_log_counter;
			float avg_depth = (float)depth_sum / render_log_counter;
			CHIAKI_LOGI(this->log, "RENDER 2s: ok=%d uflow=%d drain=%d avgdt=%.1fms depth=%.1f idx=%d",
				total_normal, total_uflow, total_drained,
				avg_dt, avg_depth, frame_index);
			render_log_counter = 0;
			total_normal = total_uflow = total_drained = 0;
			dt_sum = 0;
			depth_sum = 0;
		}

		if(underflow || drained > 0)
			CHIAKI_LOGI(this->log, "RENDER dt=%.1fms fifo=%zu idx=%d %s%s",
				render_dt, depth_before, frame_index,
				underflow ? "UNDERFLOW" : "",
				drained > 0 ? " DRAINED" : "");

		return !this->quit;
	}

	D(glUseProgram(this->prog));

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	OpenGlDraw();

	return !this->quit;
}

void IO::ResetStats()
{
	this->frames_decoded.store(0, std::memory_order_relaxed);
	this->frames_rendered.store(0, std::memory_order_relaxed);
	this->last_decode_time_us.store(0, std::memory_order_relaxed);
	this->total_frames_lost.store(0, std::memory_order_relaxed);
	this->total_frames_received.store(0, std::memory_order_relaxed);
}

void IO::StartInputThread(ChiakiSession *session)
{
#ifdef __SWITCH__
	this->input_session.store(session, std::memory_order_release);
	this->input_thread_running.store(true, std::memory_order_release);

	Result rc = threadCreate(&this->input_thread, InputThreadFunc, this,
		nullptr, 0x4000, 0x2C, 1);
	if(R_SUCCEEDED(rc))
	{
		this->input_thread_created = true;
		rc = threadStart(&this->input_thread);
		if(R_FAILED(rc))
		{
			CHIAKI_LOGE(this->log, "Failed to start input thread: 0x%x", rc);
			this->input_thread_running.store(false, std::memory_order_release);
			threadClose(&this->input_thread);
			this->input_thread_created = false;
		}
		else
		{
			CHIAKI_LOGI(this->log, "Input polling thread started at 120Hz on core 1");
		}
	}
	else
	{
		CHIAKI_LOGE(this->log, "Failed to create input thread: 0x%x", rc);
		this->input_thread_running.store(false, std::memory_order_release);
	}
#endif
}

void IO::StopInputThread()
{
#ifdef __SWITCH__
	this->input_thread_running.store(false, std::memory_order_release);
	if(this->input_thread_created)
	{
		threadWaitForExit(&this->input_thread);
		threadClose(&this->input_thread);
		this->input_thread_created = false;
	}
	this->input_session.store(nullptr, std::memory_order_release);
#endif
}

void IO::InputThreadFunc(void *arg)
{
#ifdef __SWITCH__
	IO *io = (IO *)arg;
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	std::map<uint32_t, int8_t> finger_id_touch_id;

	while(io->input_thread_running.load(std::memory_order_acquire))
	{
		ChiakiSession *session = io->input_session.load(std::memory_order_acquire);
		if(!session)
		{
			svcSleepThread(8000000LL);
			continue;
		}

		padUpdate(&io->pad);

		u64 buttons = padGetButtons(&io->pad);
		int dz = io->stick_deadzone;

		state.buttons = 0;
		state.l2_state = 0;
		state.r2_state = 0;

		if(buttons & HidNpadButton_A)      state.buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
		if(buttons & HidNpadButton_B)      state.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
		if(buttons & HidNpadButton_X)      state.buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
		if(buttons & HidNpadButton_Y)      state.buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
		if(buttons & HidNpadButton_StickL) state.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
		if(buttons & HidNpadButton_StickR) state.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;
		if(buttons & HidNpadButton_L)      state.buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
		if(buttons & HidNpadButton_R)      state.buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
		if(buttons & HidNpadButton_ZL)     state.l2_state = 0xff;
		if(buttons & HidNpadButton_ZR)     state.r2_state = 0xff;
		if(buttons & HidNpadButton_Plus)   state.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
		if(buttons & HidNpadButton_Minus)  state.buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
		if(buttons & HidNpadButton_Left)   state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
		if(buttons & HidNpadButton_Up)     state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
		if(buttons & HidNpadButton_Right)  state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
		if(buttons & HidNpadButton_Down)   state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;

		HidAnalogStickState left_stick = padGetStickPos(&io->pad, 0);
		HidAnalogStickState right_stick = padGetStickPos(&io->pad, 1);
		state.left_x = applyDeadzone((int16_t)left_stick.x, dz);
		state.left_y = applyDeadzone((int16_t)(-left_stick.y), dz);
		state.right_x = applyDeadzone((int16_t)right_stick.x, dz);
		state.right_y = applyDeadzone((int16_t)(-right_stick.y), dz);

		io->ReadGameTouchScreen(&state, &finger_id_touch_id);
		io->ReadGameSixAxis(&state);

		chiaki_session_set_controller_state(session, &state);

		svcSleepThread(8000000LL);
	}
#endif
}
