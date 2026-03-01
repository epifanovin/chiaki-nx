// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <fstream>
#include <cstdlib>
#include <tuple>

#include <chiaki/base64.h>
#include "settings.h"

Settings::Settings()
{
#if defined(__SWITCH__)
	chiaki_log_init(&this->log, CHIAKI_LOG_ALL & ~(CHIAKI_LOG_VERBOSE | CHIAKI_LOG_DEBUG), chiaki_log_cb_print, NULL);
#else
	chiaki_log_init(&this->log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, chiaki_log_cb_print, NULL);
#endif
}

Settings::ConfigurationItem Settings::ParseLine(std::string *line, std::string *value)
{
	Settings::ConfigurationItem ci;
	std::smatch m;
	for(auto it = re_map.begin(); it != re_map.end(); it++)
	{
		if(regex_search(*line, m, it->second))
		{
			ci = it->first;
			*value = m[1];
			return ci;
		}
	}
	return UNKNOWN;
}

size_t Settings::GetB64encodeSize(size_t in)
{
	// calculate base64 buffer size after encode
	return ((4 * in / 3) + 3) & ~3;
}

Settings *Settings::instance = nullptr;

Settings *Settings::GetInstance()
{
	if(instance == nullptr)
	{
		instance = new Settings;
		instance->ParseFile();
	}
	return instance;
}

ChiakiLog *Settings::GetLogger()
{
	return &this->log;
}

std::map<std::string, Host> *Settings::GetHostsMap()
{
	return &this->hosts;
}

Host *Settings::GetOrCreateHost(std::string *host_name)
{
	bool created = false;
	if(this->hosts.find(*host_name) == hosts.end())
	{
		this->hosts.emplace(std::piecewise_construct,
			std::forward_as_tuple(*host_name),
			std::forward_as_tuple(*host_name));
		created = true;
	}

	Host *host = &(this->hosts.at(*host_name));
	if(created)
	{
		// copy default settings
		// to the newly created host
		this->SetPSNOnlineID(host, this->global_psn_online_id);
			this->SetPSNAccountID(host, this->global_psn_account_id);
			this->SetVideoResolution(host, this->global_video_resolution);
			this->SetVideoFPS(host, this->global_video_fps);
			this->SetPacketLossMax(host, this->global_packet_loss_max);
			this->SetEnableIDROnFECFailure(host, this->global_enable_idr_on_fec_failure);
			this->SetDecodeQueueSize(host, this->global_decode_queue_size);
			this->SetHaptic(host, this->global_haptic);
			this->SetBitrate(host, this->global_bitrate);
			this->SetCodec(host, this->global_codec);
			this->SetAudioVolume(host, this->global_audio_volume);
			this->SetStickDeadzone(host, this->global_stick_deadzone);
			this->SetVsync(host, this->global_vsync);
			this->SetAudioBackend(host, this->global_audio_backend);
		}
	return host;
}

void Settings::ParseFile()
{
	CHIAKI_LOGI(&this->log, "Parse config file %s", this->filename);
	std::fstream config_file;
	config_file.open(this->filename, std::fstream::in);
	std::string line;
	std::string value;
	bool rp_key_b = false, rp_regist_key_b = false, rp_key_type_b = false;
	Host *current_host = nullptr;
	if(config_file.is_open())
	{
		CHIAKI_LOGV(&this->log, "Config file opened");
		Settings::ConfigurationItem ci;
		while(getline(config_file, line))
		{
			CHIAKI_LOGV(&this->log, "Parse config line `%s`", line.c_str());
			// for each line loop over config regex
			ci = this->ParseLine(&line, &value);
			switch(ci)
			{
				// got to next line
				case UNKNOWN:
					CHIAKI_LOGV(&this->log, "UNKNOWN config");
					break;
				case HOST_NAME:
					CHIAKI_LOGV(&this->log, "HOST_NAME %s", value.c_str());
					// current host is in context
					current_host = this->GetOrCreateHost(&value);
					// all following case will edit the current_host config

					rp_key_b = false;
					rp_regist_key_b = false;
					rp_key_type_b = false;
					break;
				case HOST_ADDR:
					CHIAKI_LOGV(&this->log, "HOST_ADDR %s", value.c_str());
					if(current_host != nullptr)
						current_host->host_addr = value;
					break;
				case PSN_ONLINE_ID:
					CHIAKI_LOGV(&this->log, "PSN_ONLINE_ID %s", value.c_str());
					// current_host == nullptr
					// means we are in global ini section
					// update default setting
					this->SetPSNOnlineID(current_host, value);
					break;
				case PSN_ACCOUNT_ID:
					CHIAKI_LOGV(&this->log, "PSN_ACCOUNT_ID %s", value.c_str());
					this->SetPSNAccountID(current_host, value);
					break;
				case RP_KEY:
					CHIAKI_LOGV(&this->log, "RP_KEY %s", value.c_str());
					if(current_host != nullptr)
						rp_key_b = this->SetHostRPKey(current_host, value);
					break;
				case RP_KEY_TYPE:
					CHIAKI_LOGV(&this->log, "RP_KEY_TYPE %s", value.c_str());
					if(current_host != nullptr)
						// TODO Check possible rp_type values
						rp_key_type_b = this->SetHostRPKeyType(current_host, value);
					break;
				case RP_REGIST_KEY:
					CHIAKI_LOGV(&this->log, "RP_REGIST_KEY %s", value.c_str());
					if(current_host != nullptr)
						rp_regist_key_b = this->SetHostRPRegistKey(current_host, value);
					break;
				case VIDEO_RESOLUTION:
					this->SetVideoResolution(current_host, value);
					break;
					case VIDEO_FPS:
						this->SetVideoFPS(current_host, value);
						break;
					case PACKET_LOSS_MAX:
						this->SetPacketLossMax(current_host, value);
						break;
					case ENABLE_IDR_ON_FEC_FAILURE:
						this->SetEnableIDROnFECFailure(current_host, value);
						break;
					case DECODE_QUEUE_SIZE:
						this->SetDecodeQueueSize(current_host, value);
						break;
				case HAPTIC:
					this->SetHaptic(current_host, value);
					break;
				case BITRATE:
					this->SetBitrate(current_host, value);
					break;
				case CODEC:
					this->SetCodec(current_host, value);
					break;
				case AUDIO_VOLUME:
					this->SetAudioVolume(current_host, value);
					break;
				case STICK_DEADZONE:
					this->SetStickDeadzone(current_host, value);
					break;
				case VSYNC:
					this->SetVsync(current_host, value);
					break;
				case AUTO_CONNECT:
					this->SetAutoConnect(value);
					break;
				case LAST_HOST:
					this->SetLastHost(value);
					break;
				case AUDIO_BACKEND_KEY:
					this->SetAudioBackend(current_host, value);
					break;
				case SHOW_STATS:
					this->SetShowStats(atoi(value.c_str()));
					break;
				case DITHERING:
					this->SetDithering(atoi(value.c_str()));
					break;
			case TARGET:
					CHIAKI_LOGV(&this->log, "TARGET %s", value.c_str());
					if(current_host != nullptr)
						this->SetChiakiTarget(current_host, value);
					break;
			} // ci switch
			if(rp_key_b && rp_regist_key_b && rp_key_type_b)
				// the current host contains rp key data
				current_host->rp_key_data = true;
		} // is_open
		config_file.close();
	}
	return;
}

int Settings::WriteFile()
{
	std::fstream config_file;
	CHIAKI_LOGI(&this->log, "Write config file %s", this->filename);
	// flush file (trunc)
	// the config file is completely overwritten
	config_file.open(this->filename, std::fstream::out | std::ofstream::trunc);
	std::string line;
	std::string value;

	if(config_file.is_open())
	{
		// save global settings
		CHIAKI_LOGD(&this->log, "Write Global config file %s", this->filename);

		if(this->global_video_resolution)
			config_file << "video_resolution = \""
						<< this->ResolutionPresetToString(this->GetVideoResolution(nullptr))
						<< "\"\n";

			if(this->global_video_fps)
				config_file << "video_fps = "
							<< this->FPSPresetToString(this->GetVideoFPS(nullptr))
							<< "\n";
			config_file << "packet_loss_max = " << this->GetPacketLossMax(nullptr) << "\n";
			config_file << "enable_idr_on_fec_failure = " << (this->GetEnableIDROnFECFailure(nullptr) ? 1 : 0) << "\n";
			config_file << "decode_queue_size = " << this->GetDecodeQueueSize(nullptr) << "\n";

			if(this->global_haptic)
				config_file << "haptic = "
						<< std::to_string(this->GetHaptic(nullptr))
						<< "\n";
		if(this->global_bitrate > 0)
			config_file << "bitrate = " << this->global_bitrate << "\n";
		if(this->global_codec != CODEC_PRESET_AUTO)
			config_file << "codec = " << (this->global_codec == CODEC_PRESET_H264 ? "h264" : "h265") << "\n";
		config_file << "audio_volume = " << this->global_audio_volume << "\n";
		if(this->global_stick_deadzone > 0)
			config_file << "stick_deadzone = " << this->global_stick_deadzone << "\n";
		config_file << "vsync = " << this->global_vsync << "\n";
		config_file << "auto_connect = " << this->global_auto_connect << "\n";
		if(this->global_last_host.length())
			config_file << "last_host = \"" << this->global_last_host << "\"\n";
		if(this->global_audio_backend != AUDIO_BACKEND_SDL)
			config_file << "audio_backend = audren\n";
		config_file << "show_stats = " << this->global_show_stats << "\n";
		config_file << "dithering = " << this->global_dithering << "\n";

		if(this->global_psn_online_id.length())
			config_file << "psn_online_id = \"" << this->global_psn_online_id << "\"\n";

		if(this->global_psn_account_id.length())
			config_file << "psn_account_id = \"" << this->global_psn_account_id << "\"\n";

		// write host config in file
		// loop over all configured
		for(auto it = this->hosts.begin(); it != this->hosts.end(); it++)
		{
			// first is std::string
			// second is Host
			CHIAKI_LOGD(&this->log, "Write Host config file %s", it->first.c_str());

			config_file << "[" << it->first << "]\n"
						<< "host_addr = \"" << it->second.GetHostAddr() << "\"\n"
						<< "target = \"" << it->second.GetChiakiTarget() << "\"\n";

			if(it->second.video_resolution)
				config_file << "video_resolution = \""
							<< this->ResolutionPresetToString(this->GetVideoResolution(&it->second))
							<< "\"\n";

				if(it->second.video_fps)
					config_file << "video_fps = "
								<< this->FPSPresetToString(this->GetVideoFPS(&it->second))
								<< "\n";
				config_file << "packet_loss_max = " << this->GetPacketLossMax(&it->second) << "\n";
				config_file << "enable_idr_on_fec_failure = " << (this->GetEnableIDROnFECFailure(&it->second) ? 1 : 0) << "\n";
				config_file << "decode_queue_size = " << this->GetDecodeQueueSize(&it->second) << "\n";

				if(it->second.psn_online_id.length())
					config_file << "psn_online_id = \"" << it->second.psn_online_id << "\"\n";

			if(it->second.psn_account_id.length())
				config_file << "psn_account_id = \"" << it->second.psn_account_id << "\"\n";

			if(it->second.rp_key_data || it->second.registered)
			{
				char rp_key_type[33] = { 0 };
				snprintf(rp_key_type, sizeof(rp_key_type), "%d", it->second.rp_key_type);
				// save registered rp key for auto login
				config_file << "rp_key = \"" << this->GetHostRPKey(&it->second) << "\"\n"
							<< "rp_regist_key = \"" << this->GetHostRPRegistKey(&it->second) << "\"\n"
							<< "rp_key_type = " << rp_key_type << "\n";
			}
			config_file << "haptic = "
						<< std::to_string(this->GetHaptic(&it->second))
						<< "\n";
			if(it->second.bitrate > 0)
				config_file << "bitrate = " << it->second.bitrate << "\n";
			if(it->second.codec != CODEC_PRESET_AUTO)
				config_file << "codec = " << (it->second.codec == CODEC_PRESET_H264 ? "h264" : "h265") << "\n";
			config_file << "audio_volume = " << it->second.audio_volume << "\n";
			if(it->second.stick_deadzone > 0)
				config_file << "stick_deadzone = " << it->second.stick_deadzone << "\n";
			config_file << "vsync = " << it->second.vsync << "\n";
			if(it->second.audio_backend != AUDIO_BACKEND_SDL)
				config_file << "audio_backend = audren\n";

			config_file << "\n";
		} // for host
	}	  // is_open
	config_file.close();
	return 0;
}

std::string Settings::ResolutionPresetToString(ChiakiVideoResolutionPreset resolution)
{
	switch(resolution)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			return "360p";
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			return "540p";
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			return "720p";
		case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
			return "1080p";
	}
	return "UNKNOWN";
}

int Settings::ResolutionPresetToInt(ChiakiVideoResolutionPreset resolution)
{
	switch(resolution)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			return 360;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			return 540;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			return 720;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
			return 1080;
	}
	return 0;
}

ChiakiVideoResolutionPreset Settings::StringToResolutionPreset(std::string value)
{
	if(value.compare("1080p") == 0)
		return CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
	else if(value.compare("720p") == 0)
		return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	else if(value.compare("540p") == 0)
		return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
	else if(value.compare("360p") == 0)
		return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;

	// default
	CHIAKI_LOGE(&this->log, "Unable to parse String resolution: %s",
		value.c_str());

	return CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
}

std::string Settings::FPSPresetToString(ChiakiVideoFPSPreset fps)
{
	switch(fps)
	{
		case CHIAKI_VIDEO_FPS_PRESET_30:
			return "30";
		case CHIAKI_VIDEO_FPS_PRESET_60:
			return "60";
		case CHIAKI_VIDEO_FPS_PRESET_120:
			return "120";
	}
	return "UNKNOWN";
}

int Settings::FPSPresetToInt(ChiakiVideoFPSPreset fps)
{
	switch(fps)
	{
		case CHIAKI_VIDEO_FPS_PRESET_30:
			return 30;
		case CHIAKI_VIDEO_FPS_PRESET_60:
			return 60;
		case CHIAKI_VIDEO_FPS_PRESET_120:
			return 120;
	}
	return 0;
}

ChiakiVideoFPSPreset Settings::StringToFPSPreset(std::string value)
{
	if(value.compare("120") == 0)
		return CHIAKI_VIDEO_FPS_PRESET_120;
	else if(value.compare("60") == 0)
		return CHIAKI_VIDEO_FPS_PRESET_60;
	else if(value.compare("30") == 0)
		return CHIAKI_VIDEO_FPS_PRESET_30;

	// default
	CHIAKI_LOGE(&this->log, "Unable to parse String fps: %s",
		value.c_str());

	return CHIAKI_VIDEO_FPS_PRESET_30;
}

std::string Settings::GetHostName(Host *host)
{
	if(host != nullptr)
		return host->GetHostName();
	else
		CHIAKI_LOGE(&this->log, "Cannot GetHostName from nullptr host");
	return "";
}

std::string Settings::GetHostAddr(Host *host)
{
	if(host != nullptr)
		return host->GetHostAddr();
	else
		CHIAKI_LOGE(&this->log, "Cannot GetHostAddr from nullptr host");
	return "";
}

void Settings::SetHostAddr(Host * host, std::string host_addr)
{
    if (host == nullptr)
    {
        CHIAKI_LOGE(&this->log, "Cannot SetHostAddr on nullptr host");
        return;
    }
	
    host->host_addr = host_addr;
}

std::string Settings::GetPSNOnlineID(Host *host)
{
	if(host == nullptr || host->psn_online_id.length() == 0)
		return this->global_psn_online_id;
	else
		return host->psn_online_id;
}

void Settings::SetPSNOnlineID(Host *host, std::string psn_online_id)
{
	if(host == nullptr)
		this->global_psn_online_id = psn_online_id;
	else
		host->psn_online_id = psn_online_id;
}

std::string Settings::GetPSNAccountID(Host *host)
{
	if(host == nullptr || host->psn_account_id.length() == 0)
		return this->global_psn_account_id;
	else
		return host->psn_account_id;
}

void Settings::SetPSNAccountID(Host *host, std::string psn_account_id)
{
	if(host == nullptr)
		this->global_psn_account_id = psn_account_id;
	else
		host->psn_account_id = psn_account_id;
}

ChiakiVideoResolutionPreset Settings::GetVideoResolution(Host *host)
{
	if(host == nullptr)
		return this->global_video_resolution;
	else
		return host->video_resolution;
}

void Settings::SetVideoResolution(Host *host, ChiakiVideoResolutionPreset value)
{
	if(host == nullptr)
		this->global_video_resolution = value;
	else
		host->video_resolution = value;
}

void Settings::SetVideoResolution(Host *host, std::string value)
{
	ChiakiVideoResolutionPreset p = StringToResolutionPreset(value);
	this->SetVideoResolution(host, p);
}

ChiakiVideoFPSPreset Settings::GetVideoFPS(Host *host)
{
	if(host == nullptr)
		return this->global_video_fps;
	else
		return host->video_fps;
}

void Settings::SetVideoFPS(Host *host, ChiakiVideoFPSPreset value)
{
	if(host == nullptr)
		this->global_video_fps = value;
	else
		host->video_fps = value;
}

double Settings::GetPacketLossMax(Host *host)
{
	if(host == nullptr)
		return this->global_packet_loss_max;
	return host->packet_loss_max;
}

void Settings::SetPacketLossMax(Host *host, double value)
{
	if(value < 0.0)
		value = 0.0;
	if(value > 0.25)
		value = 0.25;
	if(host == nullptr)
		this->global_packet_loss_max = value;
	else
		host->packet_loss_max = value;
}

void Settings::SetPacketLossMax(Host *host, std::string value)
{
	char *end = nullptr;
	double parsed = strtod(value.c_str(), &end);
	if(end == value.c_str())
		parsed = 0.03;
	this->SetPacketLossMax(host, parsed);
}

bool Settings::GetEnableIDROnFECFailure(Host *host)
{
	if(host == nullptr)
		return this->global_enable_idr_on_fec_failure;
	return host->enable_idr_on_fec_failure;
}

void Settings::SetEnableIDROnFECFailure(Host *host, bool value)
{
	if(host == nullptr)
		this->global_enable_idr_on_fec_failure = value;
	else
		host->enable_idr_on_fec_failure = value;
}

void Settings::SetEnableIDROnFECFailure(Host *host, std::string value)
{
	bool enabled = value == "1" || value == "true" || value == "TRUE";
	this->SetEnableIDROnFECFailure(host, enabled);
}

int Settings::GetDecodeQueueSize(Host *host)
{
	if(host == nullptr)
		return this->global_decode_queue_size;
	return host->decode_queue_size;
}

void Settings::SetDecodeQueueSize(Host *host, int value)
{
	if(value < 2)
		value = 2;
	if(value > 8)
		value = 8;
	if(host == nullptr)
		this->global_decode_queue_size = value;
	else
		host->decode_queue_size = value;
}

void Settings::SetDecodeQueueSize(Host *host, std::string value)
{
	this->SetDecodeQueueSize(host, atoi(value.c_str()));
}

int Settings::GetHaptic(Host *host)
{
	if(host == nullptr) return this->global_haptic;
	return host->haptic;
}

void Settings::SetHaptic(Host *host, int value)
{
	if(value < 0) value = 0;
	if(value > 100) value = 100;
	if(host == nullptr)
		this->global_haptic = value;
	else
		host->haptic = value;
}
void Settings::SetHaptic(Host *host, std::string value)
{
	int v = atoi(value.c_str());
	if(v >= 0 && v <= 4)
		v = haptic_preset_to_percent(v);
	SetHaptic(host, v);
}

void Settings::SetVideoFPS(Host *host, std::string value)
{
	ChiakiVideoFPSPreset p = StringToFPSPreset(value);
	this->SetVideoFPS(host, p);
}

int Settings::GetBitrate(Host *host)
{
	if(host == nullptr)
		return this->global_bitrate;
	return host->bitrate;
}

void Settings::SetBitrate(Host *host, int value)
{
	if(value < 0)
		value = 0;
	if(value > 50000)
		value = 50000;
	if(host == nullptr)
		this->global_bitrate = value;
	else
		host->bitrate = value;
}

void Settings::SetBitrate(Host *host, std::string value)
{
	this->SetBitrate(host, atoi(value.c_str()));
}

CodecPreset Settings::GetCodec(Host *host)
{
	if(host == nullptr)
		return this->global_codec;
	return static_cast<CodecPreset>(host->codec);
}

void Settings::SetCodec(Host *host, CodecPreset value)
{
	if(host == nullptr)
		this->global_codec = value;
	else
		host->codec = value;
}

void Settings::SetCodec(Host *host, std::string value)
{
	CodecPreset result = CODEC_PRESET_AUTO;
	if(value == "h264")
		result = CODEC_PRESET_H264;
	else if(value == "h265")
		result = CODEC_PRESET_H265;
	this->SetCodec(host, result);
}

int Settings::GetAudioVolume(Host *host)
{
	if(host == nullptr)
		return this->global_audio_volume;
	return host->audio_volume;
}

void Settings::SetAudioVolume(Host *host, int value)
{
	if(value < 0)
		value = 0;
	if(value > 200)
		value = 200;
	if(host == nullptr)
		this->global_audio_volume = value;
	else
		host->audio_volume = value;
}

void Settings::SetAudioVolume(Host *host, std::string value)
{
	this->SetAudioVolume(host, atoi(value.c_str()));
}

int Settings::GetStickDeadzone(Host *host)
{
	if(host == nullptr)
		return this->global_stick_deadzone;
	return host->stick_deadzone;
}

void Settings::SetStickDeadzone(Host *host, int value)
{
	if(value < 0)
		value = 0;
	if(value > 30)
		value = 30;
	if(host == nullptr)
		this->global_stick_deadzone = value;
	else
		host->stick_deadzone = value;
}

void Settings::SetStickDeadzone(Host *host, std::string value)
{
	this->SetStickDeadzone(host, atoi(value.c_str()));
}

int Settings::GetVsync(Host *host)
{
	if(host == nullptr)
		return this->global_vsync;
	return host->vsync;
}

void Settings::SetVsync(Host *host, int value)
{
	value = value ? 1 : 0;
	if(host == nullptr)
		this->global_vsync = value;
	else
		host->vsync = value;
}

void Settings::SetVsync(Host *host, std::string value)
{
	this->SetVsync(host, atoi(value.c_str()));
}

int Settings::GetAutoConnect()
{
	return this->global_auto_connect;
}

void Settings::SetAutoConnect(int value)
{
	this->global_auto_connect = value ? 1 : 0;
}

void Settings::SetAutoConnect(std::string value)
{
	this->SetAutoConnect(atoi(value.c_str()));
}

std::string Settings::GetLastHost()
{
	return this->global_last_host;
}

void Settings::SetLastHost(std::string value)
{
	this->global_last_host = value;
}

AudioBackend Settings::GetAudioBackend(Host *host)
{
	if(host == nullptr)
		return this->global_audio_backend;
	return static_cast<AudioBackend>(host->audio_backend);
}

void Settings::SetAudioBackend(Host *host, AudioBackend value)
{
	if(host == nullptr)
		this->global_audio_backend = value;
	else
		host->audio_backend = value;
}

void Settings::SetAudioBackend(Host *host, std::string value)
{
	AudioBackend result = AUDIO_BACKEND_SDL;
	if(value == "audren")
		result = AUDIO_BACKEND_AUDREN;
	this->SetAudioBackend(host, result);
}

ChiakiTarget Settings::GetChiakiTarget(Host *host)
{
	return host->GetChiakiTarget();
}

bool Settings::SetChiakiTarget(Host *host, ChiakiTarget target)
{
	if(host != nullptr)
	{
		host->SetChiakiTarget(target);
		return true;
	}
	else
	{
		CHIAKI_LOGE(&this->log, "Cannot SetChiakiTarget from nullptr host");
		return false;
	}
}

bool Settings::SetChiakiTarget(Host *host, std::string value)
{
	// TODO Check possible target values
	return this->SetChiakiTarget(host, static_cast<ChiakiTarget>(std::atoi(value.c_str())));
}

std::string Settings::GetHostRPKey(Host *host)
{
	if(host != nullptr)
	{
		if(host->rp_key_data || host->registered)
		{
			size_t rp_key_b64_sz = this->GetB64encodeSize(0x10);
			char rp_key_b64[rp_key_b64_sz + 1] = { 0 };
			ChiakiErrorCode err;
			err = chiaki_base64_encode(
				host->rp_key, 0x10,
				rp_key_b64, sizeof(rp_key_b64));

			if(CHIAKI_ERR_SUCCESS == err)
				return rp_key_b64;
			else
				CHIAKI_LOGE(&this->log, "Failed to encode rp_key to base64");
		}
	}
	else
		CHIAKI_LOGE(&this->log, "Cannot GetHostRPKey from nullptr host");

	return "";
}

bool Settings::SetHostRPKey(Host *host, std::string rp_key_b64)
{
	if(host != nullptr)
	{
		size_t rp_key_sz = sizeof(host->rp_key);
		ChiakiErrorCode err = chiaki_base64_decode(
			rp_key_b64.c_str(), rp_key_b64.length(),
			host->rp_key, &rp_key_sz);
		if(CHIAKI_ERR_SUCCESS != err)
			CHIAKI_LOGE(&this->log, "Failed to parse RP_KEY %s (it must be a base64 encoded)", rp_key_b64.c_str());
		else
			return true;
	}
	else
		CHIAKI_LOGE(&this->log, "Cannot SetHostRPKey from nullptr host");

	return false;
}

std::string Settings::GetHostRPRegistKey(Host *host)
{
	if(host != nullptr)
	{
		if(host->rp_key_data || host->registered)
		{
			size_t rp_regist_key_b64_sz = this->GetB64encodeSize(CHIAKI_SESSION_AUTH_SIZE);
			char rp_regist_key_b64[rp_regist_key_b64_sz + 1] = { 0 };
			ChiakiErrorCode err;
			err = chiaki_base64_encode(
				(uint8_t *)host->rp_regist_key, CHIAKI_SESSION_AUTH_SIZE,
				rp_regist_key_b64, sizeof(rp_regist_key_b64));

			if(CHIAKI_ERR_SUCCESS == err)
				return rp_regist_key_b64;
			else
				CHIAKI_LOGE(&this->log, "Failed to encode rp_regist_key to base64");
		}
	}
	else
		CHIAKI_LOGE(&this->log, "Cannot GetHostRPRegistKey from nullptr host");

	return "";
}

bool Settings::SetHostRPRegistKey(Host *host, std::string rp_regist_key_b64)
{
	if(host != nullptr)
	{
		size_t rp_regist_key_sz = sizeof(host->rp_regist_key);
		ChiakiErrorCode err = chiaki_base64_decode(
			rp_regist_key_b64.c_str(), rp_regist_key_b64.length(),
			(uint8_t *)host->rp_regist_key, &rp_regist_key_sz);
		if(CHIAKI_ERR_SUCCESS != err)
			CHIAKI_LOGE(&this->log, "Failed to parse RP_REGIST_KEY %s (it must be a base64 encoded)", rp_regist_key_b64.c_str());
		else
			return true;
	}
	else
		CHIAKI_LOGE(&this->log, "Cannot SetHostRPKey from nullptr host");

	return false;
}

int Settings::GetHostRPKeyType(Host *host)
{
	if(host != nullptr)
		return host->rp_key_type;

	CHIAKI_LOGE(&this->log, "Cannot GetHostRPKeyType from nullptr host");
	return 0;
}

bool Settings::SetHostRPKeyType(Host *host, std::string value)
{
	if(host != nullptr)
	{
		// TODO Check possible rp_type values
		host->rp_key_type = std::atoi(value.c_str());
		return true;
	}
	return false;
}

#ifdef CHIAKI_ENABLE_SWITCH_OVERCLOCK
int Settings::GetCPUOverclock(Host *host)
{
	if(host == nullptr)
		return this->global_cpu_overclock;
	else
		return host->cpu_overclock;
}

void Settings::SetCPUOverclock(Host *host, int value)
{
	int oc = OC_1326;
	if(value > OC_1580)
		// max OC
		oc = OC_1785;
	else if(OC_1580 >= value && value > OC_1326)
		oc = OC_1580;
	else if(OC_1326 >= value && value > OC_1220)
		oc = OC_1326;
	else if(OC_1220 >= value && value > OC_1020)
		oc = OC_1220;
	else if(OC_1020 >= value)
		// no overclock
		// default nintendo switch value
		oc = OC_1020;
	if(host == nullptr)
		this->global_cpu_overclock = oc;
	else
		host->cpu_overclock = oc;
}

void Settings::SetCPUOverclock(Host *host, std::string value)
{
	int v = atoi(value.c_str());
	this->SetCPUOverclock(host, v);
}
#endif

int Settings::GetShowStats()
{
	return this->global_show_stats;
}

void Settings::SetShowStats(int value)
{
	this->global_show_stats = value ? 1 : 0;
}

int Settings::GetDithering()
{
	return this->global_dithering;
}

void Settings::SetDithering(int value)
{
	if(value < 0) value = 0;
	if(value > 128) value = 128;
	this->global_dithering = value;
}
