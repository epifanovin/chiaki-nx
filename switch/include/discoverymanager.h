// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DISCOVERYMANAGER_H
#define CHIAKI_DISCOVERYMANAGER_H

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <functional>
#include <curl/curl.h>
#include <json-c/json.h>

#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>

#include "host.h"
#include "settings.h"

static void Discovery(ChiakiDiscoveryHost *, void *);

struct IfAddrs
{
	uint32_t local;
	uint32_t broadcast;
};

class DiscoveryManager
{
	private:
		Settings * settings = nullptr;
		ChiakiLog * log = nullptr;
		ChiakiDiscoveryService service;
		ChiakiDiscovery discovery;
		struct sockaddr * host_addr = nullptr;
		size_t host_addr_len = 0;
		IfAddrs GetIPv4BroadcastAddr();
		bool service_enable = false;

		std::mutex state_cb_mutex;
		std::map<std::string, std::function<void(const std::string &host_name)>> host_state_callbacks;

	public:
		typedef enum hoststate
		{
			UNKNOWN,
			READY,
			STANDBY,
			SHUTDOWN,
		} HostState;

		DiscoveryManager();
		~DiscoveryManager();
		void SetService(bool);
		int Send();
		int Send(struct sockaddr * host_addr, size_t host_addr_len);
		int Send(const char *);
		void DiscoveryCB(ChiakiDiscoveryHost *);
		void makeRequest(const std::string& username, std::function<void(const std::string&)> successCallback, 
                std::function<void(const std::string&)> errorCallback);

		void MarkOfflineHosts(const std::set<std::string> &alive_addrs);
		void RegisterHostStateCallback(const std::string &key, std::function<void(const std::string &host_name)> cb);
		void UnregisterHostStateCallback(const std::string &key);

	private:
		void NotifyStateCallbacks(const std::string &host_name);
};

#endif //CHIAKI_DISCOVERYMANAGER_H
