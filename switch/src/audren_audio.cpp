// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifdef __SWITCH__

#include "audren_audio.h"
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <algorithm>
#include <climits>

#include <borealis.hpp>

static const uint8_t sink_channels[] = {0, 1};

static const AudioRendererConfig ar_config = {
	.output_rate = AudioRendererOutputRate_48kHz,
	.num_voices = 24,
	.num_effects = 0,
	.num_sinks = 1,
	.num_mix_objs = 1,
	.num_mix_buffers = 2,
};

AudrenAudio::AudrenAudio() = default;

AudrenAudio::~AudrenAudio()
{
	Cleanup();
}

bool AudrenAudio::Init(unsigned int channels, unsigned int sample_rate_, int extra_latency_ms)
{
	if(initialized)
		Cleanup();

	channel_count = (int)channels;
	sample_rate = (int)sample_rate_;

	int extra_samples = (sample_rate * extra_latency_ms) / 1000;
	int extra_frames = (extra_samples + AUDREN_SAMPLES_PER_FRAME_48KHZ - 1) / AUDREN_SAMPLES_PER_FRAME_48KHZ;
	const int latency_frames = 5 + extra_frames;
	samples_per_buf = latency_frames * AUDREN_SAMPLES_PER_FRAME_48KHZ;
	buffer_size = samples_per_buf * channel_count * (int)sizeof(int16_t);
	current_size = 0;

	mutexInit(&update_lock);

	int mempool_size =
		(buffer_size * AUDREN_BUFFER_COUNT + (AUDREN_MEMPOOL_ALIGNMENT - 1)) &
		~(AUDREN_MEMPOOL_ALIGNMENT - 1);
	mempool_ptr = memalign(AUDREN_MEMPOOL_ALIGNMENT, mempool_size);
	if(!mempool_ptr)
	{
		brls::Logger::error("Audren: mempool alloc failed");
		return false;
	}

	Result rc = audrenInitialize(&ar_config);
	if(R_FAILED(rc))
	{
		brls::Logger::error("Audren: audrenInitialize: {:x}", rc);
		free(mempool_ptr);
		mempool_ptr = nullptr;
		return false;
	}

	rc = audrvCreate(&driver, &ar_config, channel_count);
	if(R_FAILED(rc))
	{
		brls::Logger::error("Audren: audrvCreate: {:x}", rc);
		audrenExit();
		free(mempool_ptr);
		mempool_ptr = nullptr;
		return false;
	}

	memset(wavebufs, 0, sizeof(wavebufs));
	for(int i = 0; i < AUDREN_BUFFER_COUNT; i++)
	{
		wavebufs[i].data_raw = mempool_ptr;
		wavebufs[i].size = mempool_size;
		wavebufs[i].start_sample_offset = i * samples_per_buf;
		wavebufs[i].end_sample_offset = wavebufs[i].start_sample_offset + samples_per_buf;
	}

	current_wavebuf = nullptr;

	int mpid = audrvMemPoolAdd(&driver, mempool_ptr, mempool_size);
	audrvMemPoolAttach(&driver, mpid);

	audrvDeviceSinkAdd(&driver, AUDREN_DEFAULT_DEVICE_NAME, channel_count, sink_channels);

	rc = audrenStartAudioRenderer();
	if(R_FAILED(rc))
		brls::Logger::error("Audren: audrenStartAudioRenderer: {:x}", rc);

	audrvVoiceInit(&driver, 0, channel_count, PcmFormat_Int16, sample_rate);
	audrvVoiceSetDestinationMix(&driver, 0, AUDREN_FINAL_MIX_ID);

	for(int i = 0; i < channel_count; i++)
	{
		for(int j = 0; j < channel_count; j++)
			audrvVoiceSetMixFactor(&driver, 0, i == j ? 1.0f : 0.0f, i, j);
	}

	audrvVoiceStart(&driver, 0);
	initialized = true;

	brls::Logger::info("Audren: Init done (ch={}, rate={}, latency_frames={}, buf={}ms)",
		channel_count, sample_rate, latency_frames,
		(int)(samples_per_buf * 1000 / sample_rate));
	return true;
}

void AudrenAudio::Cleanup()
{
	if(!initialized)
		return;

	initialized = false;
	audrvVoiceStop(&driver, 0);
	audrvClose(&driver);
	audrenExit();

	if(mempool_ptr)
	{
		free(mempool_ptr);
		mempool_ptr = nullptr;
	}

	current_wavebuf = nullptr;
	current_pool_ptr = nullptr;
	total_queued_samples = 0;
	current_size = 0;

	brls::Logger::info("Audren: Cleanup done");
}

ssize_t AudrenAudio::FreeWaveBufIndex()
{
	for(int i = 0; i < AUDREN_BUFFER_COUNT; i++)
	{
		if(wavebufs[i].state == AudioDriverWaveBufState_Free ||
		   wavebufs[i].state == AudioDriverWaveBufState_Done)
			return i;
	}
	return -1;
}

size_t AudrenAudio::AppendAudio(const void *buf, size_t size)
{
	if(!current_wavebuf)
	{
		ssize_t index = FreeWaveBufIndex();
		if(index == -1)
			return 0;

		current_wavebuf = &wavebufs[index];
		current_pool_ptr = (uint8_t *)mempool_ptr + (index * buffer_size);
		current_size = 0;
	}

	if((ssize_t)size > buffer_size - current_size)
		size = buffer_size - current_size;

	void *dstbuf = (uint8_t *)current_pool_ptr + current_size;
	memcpy(dstbuf, buf, size);
	armDCacheFlush(dstbuf, size);

	current_size += size;
	total_queued_samples += size / channel_count / sizeof(int16_t);

	if(current_size == buffer_size)
	{
		audrvVoiceAddWaveBuf(&driver, 0, current_wavebuf);

		mutexLock(&update_lock);
		audrvUpdate(&driver);
		mutexUnlock(&update_lock);

		if(!audrvVoiceIsPlaying(&driver, 0))
			audrvVoiceStart(&driver, 0);

		current_wavebuf = nullptr;
	}

	return size;
}

void AudrenAudio::QueueAudio(const int16_t *buf, size_t samples_count, float volume)
{
	if(!initialized)
		return;

	audrvUpdate(&driver);

	size_t queued_samples = total_queued_samples -
		audrvVoiceGetPlayedSampleCount(&driver, 0);

	if(queued_samples > (size_t)sample_rate / 2)
		return;

	size_t byte_count = samples_count * channel_count * sizeof(int16_t);

	int16_t scaled_buf[4096];
	size_t total_samples = samples_count * channel_count;
	for(size_t offset = 0; offset < total_samples; )
	{
		size_t chunk = std::min(total_samples - offset, (size_t)4096);
		for(size_t i = 0; i < chunk; i++)
		{
			int s = (int)(buf[offset + i] * volume);
			scaled_buf[i] = (int16_t)std::max(SHRT_MIN, std::min(SHRT_MAX, s));
		}

		size_t chunk_bytes = chunk * sizeof(int16_t);
		size_t written = 0;
		while(written < chunk_bytes)
		{
			written += AppendAudio((const uint8_t *)scaled_buf + written, chunk_bytes - written);
			if(written != chunk_bytes)
			{
				mutexLock(&update_lock);
				audrvUpdate(&driver);
				mutexUnlock(&update_lock);
				audrenWaitFrame();
			}
		}
		offset += chunk;
	}
}

int AudrenAudio::GetQueuedBytes()
{
	if(!initialized)
		return 0;
	size_t queued = total_queued_samples - audrvVoiceGetPlayedSampleCount(&driver, 0);
	return (int)(queued * channel_count * sizeof(int16_t));
}

#endif // __SWITCH__
