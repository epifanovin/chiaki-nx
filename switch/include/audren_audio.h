// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AUDREN_AUDIO_H
#define CHIAKI_AUDREN_AUDIO_H

#ifdef __SWITCH__

#include <switch.h>
#include <cstdint>
#include <cstddef>

#define AUDREN_BUFFER_COUNT 8

class AudrenAudio
{
public:
	AudrenAudio();
	~AudrenAudio();

	bool Init(unsigned int channels, unsigned int sample_rate, int extra_latency_ms = 0);
	void Cleanup();
	void QueueAudio(const int16_t *buf, size_t samples_count, float volume);
	int GetQueuedBytes();

private:
	ssize_t FreeWaveBufIndex();
	size_t AppendAudio(const void *buf, size_t size);

	AudioDriver driver;
	AudioDriverWaveBuf wavebufs[AUDREN_BUFFER_COUNT];
	AudioDriverWaveBuf *current_wavebuf = nullptr;
	Mutex update_lock;

	void *mempool_ptr = nullptr;
	void *current_pool_ptr = nullptr;

	bool initialized = false;
	int channel_count = 0;
	int sample_rate = 0;
	int buffer_size = 0;
	int samples_per_buf = 0;
	size_t total_queued_samples = 0;
	ssize_t current_size = 0;
};

#endif // __SWITCH__
#endif // CHIAKI_AUDREN_AUDIO_H
