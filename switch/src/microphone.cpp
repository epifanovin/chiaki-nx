// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifdef __SWITCH__

#include "microphone.h"
#include <cstring>
#include <cstdlib>

SwitchMicrophone::SwitchMicrophone()
{
	chiaki_opus_encoder_init(&opus_encoder, nullptr);
}

SwitchMicrophone::~SwitchMicrophone()
{
	Stop();
	chiaki_opus_encoder_fini(&opus_encoder);
}

bool SwitchMicrophone::Start(ChiakiSession *sess, ChiakiLog *lg)
{
	if(running.load())
		return true;

	this->log = lg;
	this->session = sess;
	opus_encoder.log = lg;

	Result rc = audinInitialize();
	if(R_FAILED(rc))
	{
		CHIAKI_LOGE(log, "audinInitialize failed: 0x%x", rc);
		return false;
	}
	audin_initialized = true;

	rc = audinStartAudioIn();
	if(R_FAILED(rc))
	{
		CHIAKI_LOGE(log, "audinStartAudioIn failed: 0x%x", rc);
		audinExit();
		audin_initialized = false;
		return false;
	}

	ChiakiAudioHeader audio_header;
	chiaki_audio_header_set(&audio_header, MIC_CHANNELS, 16,
		MIC_FRAME_SAMPLES * 100, MIC_FRAME_SAMPLES);
	chiaki_opus_encoder_header(&audio_header, &opus_encoder, session);

	ChiakiErrorCode err = chiaki_session_connect_microphone(session);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGW(log, "chiaki_session_connect_microphone failed: %s", chiaki_error_string(err));

	chiaki_session_toggle_microphone(session, false);

	should_stop.store(false);
	running.store(true);

	rc = threadCreate(&capture_thread, CaptureThreadFunc, this,
		nullptr, 0x8000, 0x2C, -2);
	if(R_FAILED(rc))
	{
		CHIAKI_LOGE(log, "threadCreate for mic failed: 0x%x", rc);
		running.store(false);
		audinStopAudioIn();
		audinExit();
		audin_initialized = false;
		return false;
	}

	threadStart(&capture_thread);
	CHIAKI_LOGI(log, "Microphone capture started");
	return true;
}

void SwitchMicrophone::Stop()
{
	if(!running.load())
		return;

	should_stop.store(true);
	threadWaitForExit(&capture_thread);
	threadClose(&capture_thread);
	running.store(false);

	if(audin_initialized)
	{
		audinStopAudioIn();
		audinExit();
		audin_initialized = false;
	}

	if(session)
		chiaki_session_toggle_microphone(session, true);

	CHIAKI_LOGI(log, "Microphone capture stopped");
}

void SwitchMicrophone::CaptureThreadFunc(void *arg)
{
	auto *mic = static_cast<SwitchMicrophone *>(arg);
	mic->CaptureLoop();
}

void SwitchMicrophone::CaptureLoop()
{
	const size_t buf_size = MIC_FRAME_SAMPLES * MIC_CHANNELS * sizeof(int16_t);
	AudioInBuffer audin_buf;
	memset(&audin_buf, 0, sizeof(audin_buf));

	audin_buf.buffer = aligned_alloc(0x1000, buf_size);
	if(!audin_buf.buffer)
	{
		CHIAKI_LOGE(log, "Mic: failed to allocate capture buffer");
		return;
	}
	audin_buf.buffer_size = buf_size;

	AudioInBuffer *released_buf = nullptr;
	u32 released_count = 0;

	while(!should_stop.load())
	{
		memset(audin_buf.buffer, 0, buf_size);
		audinAppendAudioInBuffer(&audin_buf);

		Result rc = audinWaitCaptureFinish(&released_buf, &released_count, UINT64_MAX);
		if(R_FAILED(rc) || released_count == 0)
		{
			if(should_stop.load())
				break;
			continue;
		}

		chiaki_opus_encoder_frame(
			(int16_t *)released_buf->buffer, &opus_encoder);
	}

	free(audin_buf.buffer);
}

#endif // __SWITCH__
