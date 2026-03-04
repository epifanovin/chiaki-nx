// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SETTINGS_H
#define CHIAKI_SETTINGS_H

#include <regex>

#include <chiaki/log.h>
#include "host.h"

typedef enum {
	HAPTIC_PRESET_OFF = 0,
	HAPTIC_PRESET_25 = 1,
	HAPTIC_PRESET_50 = 2,
	HAPTIC_PRESET_75 = 3,
	HAPTIC_PRESET_100 = 4
} HapticPreset;

#define HAPTIC_PRESET_DIABLED HAPTIC_PRESET_OFF
#define HAPTIC_PRESET_WEAK HAPTIC_PRESET_50
#define HAPTIC_PRESET_STRONG HAPTIC_PRESET_100

static inline int haptic_preset_to_percent(int v)
{
	switch(v)
	{
		case 0: return 0;
		case 1: return 25;
		case 2: return 50;
		case 3: return 75;
		case 4: return 100;
		default: return v;
	}
}

typedef enum {
	CODEC_PRESET_AUTO = 0,
	CODEC_PRESET_H264 = 1,
	CODEC_PRESET_H265 = 2
} CodecPreset;

typedef enum {
	AUDIO_BACKEND_SDL = 0,
	AUDIO_BACKEND_AUDREN = 1
} AudioBackend;

// mutual host and settings
class Host;

class Settings
{
	protected:
		// keep constructor private (sigleton class)
		Settings();
		static Settings * instance;

	private:
		const char * filename = "chiaki.conf";
		ChiakiLog log;
		std::map<std::string, Host> hosts;

		// global_settings from psedo INI file
			ChiakiVideoResolutionPreset global_video_resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
			ChiakiVideoFPSPreset global_video_fps = CHIAKI_VIDEO_FPS_PRESET_60;
			double global_packet_loss_max = 0.03;
			bool global_enable_idr_on_fec_failure = true;
			int global_decode_queue_size = 4;
			std::string global_psn_online_id = "";
			std::string global_psn_account_id = "";
			int global_haptic = 0;
			int global_bitrate = 0;
			CodecPreset global_codec = CODEC_PRESET_AUTO;
			int global_audio_volume = 180;
			int global_stick_deadzone = 0;
			int global_vsync = 1;
			int global_auto_connect = 0;
			std::string global_last_host = "";
			AudioBackend global_audio_backend = AUDIO_BACKEND_SDL;
			int global_show_stats = 0;
			int global_dithering = 0;
			int global_frame_pacing = 1;

		typedef enum configurationitem
		{
			UNKNOWN,
			HOST_NAME,
			HOST_ADDR,
			PSN_ONLINE_ID,
			PSN_ACCOUNT_ID,
			RP_KEY,
			RP_KEY_TYPE,
			RP_REGIST_KEY,
				VIDEO_RESOLUTION,
				VIDEO_FPS,
				PACKET_LOSS_MAX,
				ENABLE_IDR_ON_FEC_FAILURE,
				DECODE_QUEUE_SIZE,
				TARGET,
				HAPTIC,
				BITRATE,
				CODEC,
				AUDIO_VOLUME,
				STICK_DEADZONE,
				VSYNC,
				AUTO_CONNECT,
				LAST_HOST,
				AUDIO_BACKEND_KEY,
				SHOW_STATS,
				DITHERING,
				FRAME_PACING,
			} ConfigurationItem;

		// dummy parser implementation
		// the aim is not to have bulletproof parser
		// the goal is to read/write inernal flat configuration file
		const std::map<Settings::ConfigurationItem, std::regex> re_map = {
			{HOST_NAME, std::regex("^\\[\\s*(.+)\\s*\\]")},
			{HOST_ADDR, std::regex("^\\s*host_(?:ip|addr)\\s*=\\s*\"?((\\d+\\.\\d+\\.\\d+\\.\\d+)|([A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+))\"?")},
			{PSN_ONLINE_ID, std::regex("^\\s*psn_online_id\\s*=\\s*\"?([\\w_-]+)\"?")},
			{PSN_ACCOUNT_ID, std::regex("^\\s*psn_account_id\\s*=\\s*\"?([\\w/=+]+)\"?")},
			{RP_KEY, std::regex("^\\s*rp_key\\s*=\\s*\"?([\\w/=+]+)\"?")},
			{RP_KEY_TYPE, std::regex("^\\s*rp_key_type\\s*=\\s*\"?(\\d)\"?")},
			{RP_REGIST_KEY, std::regex("^\\s*rp_regist_key\\s*=\\s*\"?([\\w/=+]+)\"?")},
				{VIDEO_RESOLUTION, std::regex("^\\s*video_resolution\\s*=\\s*\"?(1080p|720p|540p|360p)\"?")},
				{VIDEO_FPS, std::regex("^\\s*video_fps\\s*=\\s*\"?(120|60|30)\"?")},
				{PACKET_LOSS_MAX, std::regex("^\\s*packet_loss_max\\s*=\\s*\"?([0-9]+(?:\\.[0-9]+)?)\"?")},
				{ENABLE_IDR_ON_FEC_FAILURE, std::regex("^\\s*enable_idr_on_fec_failure\\s*=\\s*\"?(1|0|true|false)\"?")},
				{DECODE_QUEUE_SIZE, std::regex("^\\s*decode_queue_size\\s*=\\s*\"?(\\d+)\"?")},
				{TARGET, std::regex("^\\s*target\\s*=\\s*\"?(\\d+)\"?")},
				{HAPTIC, std::regex("^\\s*haptic\\s*=\\s*\"?(\\d+)\"?")},
				{BITRATE, std::regex("^\\s*bitrate\\s*=\\s*\"?(\\d+)\"?")},
				{CODEC, std::regex("^\\s*codec\\s*=\\s*\"?(auto|h264|h265)\"?")},
				{AUDIO_VOLUME, std::regex("^\\s*audio_volume\\s*=\\s*\"?(\\d+)\"?")},
				{STICK_DEADZONE, std::regex("^\\s*stick_deadzone\\s*=\\s*\"?(\\d+)\"?")},
				{VSYNC, std::regex("^\\s*vsync\\s*=\\s*\"?(0|1)\"?")},
				{AUTO_CONNECT, std::regex("^\\s*auto_connect\\s*=\\s*\"?(0|1)\"?")},
				{LAST_HOST, std::regex("^\\s*last_host\\s*=\\s*\"?([^\"]+)\"?")},
				{AUDIO_BACKEND_KEY, std::regex("^\\s*audio_backend\\s*=\\s*\"?(sdl|audren)\"?")},
				{SHOW_STATS, std::regex("^\\s*show_stats\\s*=\\s*\"?(0|1)\"?")},
				{DITHERING, std::regex("^\\s*dithering\\s*=\\s*\"?(\\d+)\"?")},
				{FRAME_PACING, std::regex("^\\s*frame_pacing\\s*=\\s*\"?(0|1)\"?")},
			};

		ConfigurationItem ParseLine(std::string * line, std::string * value);
		size_t GetB64encodeSize(size_t);

	public:
		// singleton configuration
		Settings(const Settings&) = delete;
		void operator=(const Settings&) = delete;
		static Settings * GetInstance();

		ChiakiLog * GetLogger();
		std::map<std::string, Host> * GetHostsMap();
		Host * GetOrCreateHost(std::string * host_name);

		void ParseFile();
		int WriteFile();

		std::string ResolutionPresetToString(ChiakiVideoResolutionPreset resolution);
		int ResolutionPresetToInt(ChiakiVideoResolutionPreset resolution);
		ChiakiVideoResolutionPreset StringToResolutionPreset(std::string value);

		std::string FPSPresetToString(ChiakiVideoFPSPreset fps);
		int FPSPresetToInt(ChiakiVideoFPSPreset fps);
		ChiakiVideoFPSPreset StringToFPSPreset(std::string value);

		std::string GetHostName(Host * host);
		std::string GetHostAddr(Host * host);
		void SetHostAddr(Host * host, std::string host_addr);

		std::string GetPSNOnlineID(Host * host);
		void SetPSNOnlineID(Host * host, std::string psn_online_id);

		std::string GetPSNAccountID(Host * host);
		void SetPSNAccountID(Host * host, std::string psn_account_id);

		ChiakiVideoResolutionPreset GetVideoResolution(Host * host);
		void SetVideoResolution(Host * host, ChiakiVideoResolutionPreset value);
		void SetVideoResolution(Host * host, std::string value);

			ChiakiVideoFPSPreset GetVideoFPS(Host * host);
			void SetVideoFPS(Host * host, ChiakiVideoFPSPreset value);
			void SetVideoFPS(Host * host, std::string value);
			double GetPacketLossMax(Host *host);
			void SetPacketLossMax(Host *host, double value);
			void SetPacketLossMax(Host *host, std::string value);
			bool GetEnableIDROnFECFailure(Host *host);
			void SetEnableIDROnFECFailure(Host *host, bool value);
			void SetEnableIDROnFECFailure(Host *host, std::string value);
			int GetDecodeQueueSize(Host *host);
			void SetDecodeQueueSize(Host *host, int value);
			void SetDecodeQueueSize(Host *host, std::string value);

		int GetHaptic(Host * host);
		void SetHaptic(Host * host, int value);
		void SetHaptic(Host * host, std::string value);

		int GetBitrate(Host *host);
		void SetBitrate(Host *host, int value);
		void SetBitrate(Host *host, std::string value);

		CodecPreset GetCodec(Host *host);
		void SetCodec(Host *host, CodecPreset value);
		void SetCodec(Host *host, std::string value);

		int GetAudioVolume(Host *host);
		void SetAudioVolume(Host *host, int value);
		void SetAudioVolume(Host *host, std::string value);

		int GetStickDeadzone(Host *host);
		void SetStickDeadzone(Host *host, int value);
		void SetStickDeadzone(Host *host, std::string value);

		int GetVsync(Host *host);
		void SetVsync(Host *host, int value);
		void SetVsync(Host *host, std::string value);

		int GetAutoConnect();
		void SetAutoConnect(int value);
		void SetAutoConnect(std::string value);

		std::string GetLastHost();
		void SetLastHost(std::string value);

		AudioBackend GetAudioBackend(Host *host);
		void SetAudioBackend(Host *host, AudioBackend value);
		void SetAudioBackend(Host *host, std::string value);

		int GetShowStats();
		void SetShowStats(int value);

		int GetDithering();
		void SetDithering(int value);

		int GetFramePacing();
		void SetFramePacing(int value);

		ChiakiTarget GetChiakiTarget(Host * host);
		bool SetChiakiTarget(Host * host, ChiakiTarget target);
		bool SetChiakiTarget(Host * host, std::string value);

		std::string GetHostRPKey(Host * host);
		bool SetHostRPKey(Host * host, std::string rp_key_b64);

		std::string GetHostRPRegistKey(Host * host);
		bool SetHostRPRegistKey(Host * host, std::string rp_regist_key_b64);

		int GetHostRPKeyType(Host * host);
		bool SetHostRPKeyType(Host * host, std::string value);
};

#endif // CHIAKI_SETTINGS_H
