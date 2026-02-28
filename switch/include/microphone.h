// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SWITCH_MICROPHONE_H
#define CHIAKI_SWITCH_MICROPHONE_H

#ifdef __SWITCH__

#include <switch.h>
#include <chiaki/session.h>
#include <chiaki/opusencoder.h>
#include <chiaki/log.h>
#include <atomic>

#define MIC_SAMPLE_RATE 48000
#define MIC_CHANNELS 2
#define MIC_FRAME_SAMPLES 480

class SwitchMicrophone
{
public:
	SwitchMicrophone();
	~SwitchMicrophone();

	bool Start(ChiakiSession *session, ChiakiLog *log);
	void Stop();
	bool IsRunning() { return running.load(); }

private:
	static void CaptureThreadFunc(void *arg);
	void CaptureLoop();

	ChiakiLog *log = nullptr;
	ChiakiSession *session = nullptr;
	ChiakiOpusEncoder opus_encoder;

	Thread capture_thread;
	std::atomic<bool> running{false};
	std::atomic<bool> should_stop{false};

	bool audin_initialized = false;
};

#endif // __SWITCH__
#endif // CHIAKI_SWITCH_MICROPHONE_H
