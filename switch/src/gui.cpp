// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "gui.h"
#include <array>
#include <chiaki/log.h>
#include "views/enter_pin_view.h"
#include "views/ps_remote_play.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>



#define SCREEN_W 1280
#define SCREEN_H 720

using namespace brls::literals; // for _i18n

static std::string menuTabTitle(const std::string &host_name)
{
	static constexpr size_t MAX_TAB_TITLE_LEN = 12;
	if(host_name.size() <= MAX_TAB_TITLE_LEN)
		return host_name;
	return host_name.substr(0, MAX_TAB_TITLE_LEN - 3) + "...";
}

#define DIALOG(dialog, r)                                                       \
	brls::Dialog *d_##dialog = new brls::Dialog(r);                             \
	brls::VoidEvent::Callback cb_##dialog = [d_##dialog]() {                    \
		d_##dialog->close();                                                    \
	};                                                                          \
	d_##dialog->addButton("Ok", cb_##dialog);                                   \
	d_##dialog->setCancelable(false);                                           \
	d_##dialog->open();                                                         \
	brls::Logger::info("Dialog: {0}", r);

HostInterface::HostInterface(Host *host, DiscoveryManager *dm)
	: host(host), discoverymanager(dm)
{
	brls::Logger::info("HostInterface: ctor start ({})", host ? host->GetHostName() : "null");
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
	this->io = IO::GetInstance();

	this->connectButton = new brls::ListItem("Connect");
	this->connectButton->getClickEvent()->subscribe(std::bind(&HostInterface::Connect, this, std::placeholders::_1));
	this->addView(this->connectButton);

	brls::ListItem *wakeup = new brls::ListItem("Wakeup");
	wakeup->getClickEvent()->subscribe(std::bind(&HostInterface::Wakeup, this, std::placeholders::_1));
	this->addView(wakeup);

	// message delimiter
	brls::Label *info = new brls::Label();
	info->setText("Host configuration");
	this->addView(info);

	// push opengl chiaki stream
	// when the host is connected
	this->host->SetEventConnectedCallback(std::bind(&HostInterface::Stream, this));
	this->host->SetEventQuitCallback(std::bind(&HostInterface::CloseStream, this, std::placeholders::_1));
	// push login pin view onto the stack in callback fn
	this->host->SetEventLoginPinRequestCallback(std::bind(&HostInterface::EnterPin, this, std::placeholders::_1));
	// allow host to update controller state
	this->host->SetEventRumbleCallback(std::bind(&IO::SetRumble, this->io, std::placeholders::_1, std::placeholders::_2));
	this->host->SetReadControllerCallback(std::bind(&IO::UpdateControllerState, this->io, std::placeholders::_1, std::placeholders::_2));

	UpdateConnectButton();
	ScheduleStatusPoll();

	if(this->discoverymanager && this->host)
	{
		this->discoveryCbKey = "hi_" + this->host->GetHostName();
		auto cb_flag = this->alive;
		this->discoverymanager->RegisterHostStateCallback(
			this->discoveryCbKey,
			[this, cb_flag](const std::string &name) {
				if(!*cb_flag)
					return;
				if(name == this->host->GetHostName())
					UpdateConnectButton();
			});
	}

	brls::Logger::info("HostInterface: ctor done ({})", host ? host->GetHostName() : "null");
}

HostInterface::~HostInterface()
{
	*this->alive = false;
	if(this->statusPollActive)
	{
		this->statusPollActive = false;
		brls::cancelDelay(this->statusPollId);
	}
	if(this->probeSock >= 0)
	{
		close(this->probeSock);
		this->probeSock = -1;
	}
	if(this->discoverymanager && !this->discoveryCbKey.empty())
		this->discoverymanager->UnregisterHostStateCallback(this->discoveryCbKey);
	Disconnect();
}

void HostInterface::UpdateConnectButton()
{
	if(!this->connectButton || !this->host)
		return;

	bool can_connect = this->host->IsReady() || this->hostReachable;
	std::string status = can_connect ? "Online" : "Offline";

	this->connectButton->setFocusable(can_connect);
	this->connectButton->setAlpha(can_connect ? 1.0f : 0.4f);
	this->connectButton->setValue(status);
}

void HostInterface::ScheduleStatusPoll()
{
	this->statusPollActive = true;
	auto flag = this->alive;
	this->statusPollId = brls::delay(1000, [this, flag]() {
		if(!*flag || !this->statusPollActive)
			return;
		AdvanceProbe();
		UpdateConnectButton();
		ScheduleStatusPoll();
	});
}

void HostInterface::AdvanceProbe()
{
	if(!this->host)
		return;

	if(this->probeSock < 0)
	{
		// Start a new probe: non-blocking TCP connect to port 9295
		std::string addr = this->host->GetHostAddr();
		if(addr.empty())
			return;

		struct sockaddr_in sa = {};
		sa.sin_family = AF_INET;
		sa.sin_port = htons(9295);

		if(inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1)
		{
			// Hostname, not IP -- try DNS resolve (instant for cached)
			struct addrinfo hints = {}, *res = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			if(getaddrinfo(addr.c_str(), "9295", &hints, &res) != 0 || !res)
				return;
			memcpy(&sa, res->ai_addr, sizeof(sa));
			freeaddrinfo(res);
		}

		this->probeSock = socket(AF_INET, SOCK_STREAM, 0);
		if(this->probeSock < 0)
			return;

		int flags = fcntl(this->probeSock, F_GETFL, 0);
		fcntl(this->probeSock, F_SETFL, flags | O_NONBLOCK);

		int ret = connect(this->probeSock, (struct sockaddr *)&sa, sizeof(sa));
		if(ret == 0)
		{
			this->hostReachable = true;
			close(this->probeSock);
			this->probeSock = -1;
			CHIAKI_LOGI(this->log, "Probe: %s reachable (instant)", addr.c_str());
		}
		else if(errno != EINPROGRESS)
		{
			this->hostReachable = false;
			close(this->probeSock);
			this->probeSock = -1;
		}
		// else: EINPROGRESS, will check on next tick
	}
	else
	{
		// Check if pending connect completed (non-blocking)
		struct pollfd pfd = {this->probeSock, POLLOUT, 0};
		int ret = poll(&pfd, 1, 0);

		if(ret > 0)
		{
			int err = 0;
			socklen_t len = sizeof(err);
			getsockopt(this->probeSock, SOL_SOCKET, SO_ERROR, &err, &len);
			this->hostReachable = (err == 0);
			CHIAKI_LOGI(this->log, "Probe: %s %s (err=%d)",
				this->host->GetHostAddr().c_str(),
				this->hostReachable ? "reachable" : "unreachable", err);
			close(this->probeSock);
			this->probeSock = -1;
		}
		else if(ret == 0)
		{
			// Still connecting -- will check next tick.
			// If too many ticks pass, give up (handled by next start overwriting).
		}
		else
		{
			this->hostReachable = false;
			close(this->probeSock);
			this->probeSock = -1;
		}
	}
}

void HostInterface::Register(Host *host, std::function<void()> success_cb)
{
	Settings *settings = Settings::GetInstance();
	IO *io = IO::GetInstance();

	// user must provide psn id for registration
	std::string account_id = settings->GetPSNAccountID(host);
	std::string online_id = settings->GetPSNOnlineID(host);
	ChiakiTarget target = host->GetChiakiTarget();

	if(target >= CHIAKI_TARGET_PS4_9 && account_id.length() <= 0)
	{
		// PS4 firmware > 7.0
		DIALOG(upaid, "Undefined PSN Account ID (Please configure a valid psn_account_id)");
		return;
	}
	else if(target < CHIAKI_TARGET_PS4_9 && online_id.length() <= 0)
	{
		// use oline ID for ps4 < 7.0
		DIALOG(upoid, "Undefined PSN Online ID (Please configure a valid psn_online_id)");
		return;
	}

	// add HostConnected function to regist_event_type_finished_success
	auto event_type_finished_success_cb = [settings, success_cb]() {
		// save RP keys
		settings->WriteFile();
		if(success_cb != nullptr)
		{
			// FIXME: may raise a connection refused
			// when the connection is triggered
			// just after the register success
			sleep(2);
			success_cb();
		}
		// decrement block input token number
		brls::Application::unblockInputs();
	};
	host->SetRegistEventTypeFinishedSuccess(event_type_finished_success_cb);

	auto event_type_finished_failed_cb = []() {
		// unlock user inputs
		brls::Application::unblockInputs();
		brls::Application::notify("Registration failed");
	};
	host->SetRegistEventTypeFinishedFailed(event_type_finished_failed_cb);

	// the host is not registered yet
	// use callback to ensure that the message is showed on screen
	// before the Swkbd
	auto pin_input_cb = [host](long pin) {
		// prevent users form messing with the gui
		brls::Application::blockInputs();
		int ret = host->Register((int)pin);
		if(ret != HOST_REGISTER_OK)
		{
			switch(ret)
			{
				// account not configured
				case HOST_REGISTER_ERROR_SETTING_PSNACCOUNTID:
					brls::Application::notify("No PSN Account ID provided");
					brls::Application::unblockInputs();
					break;
				case HOST_REGISTER_ERROR_SETTING_PSNONLINEID:
					brls::Application::notify("No PSN Online ID provided");
					brls::Application::unblockInputs();
					break;
			}
		}
	};
	// the pin is 8 digit
	bool success = brls::Application::getImeManager()->openForNumber(pin_input_cb,
		"Please enter your PlayStation registration PIN code", "8 digits without spaces", 8, "", "", "");
	if(!success)
		brls::Application::notify("Unable to open keyboard");
}

void HostInterface::Register()
{
	// use Connect just after the registration to save user inputs
	HostInterface::Register(this->host, std::bind(&HostInterface::ConnectSession, this));
}

void HostInterface::Wakeup(brls::View *view)
{
	if(!this->host->HasRPkey())
	{
		// the host is not registered yet
		this->Register();
	}
	else
	{
		int r = host->Wakeup();
		if(r == 0)
		{
			brls::Application::notify("Wakeup packet sent to PlayStation");
		}
		else
		{
			brls::Application::notify("Wakeup packet sent, no response from PlayStation");
		}
	}
}

void HostInterface::Connect(brls::View *view)
{
	// check that all requirements are met
	if(!this->host->IsDiscovered() && !this->host->HasRPkey())
	{
		// at this point the host must be discovered or registered manually
		// to validate the system_version accuracy
		brls::Application::crash("Undefined PlayStation remote version");
	}

	if(!this->host->IsReady() && !this->hostReachable)
	{
		DIALOG(ptoyp, "Your PlayStation is off, please turn it on");
		return;
	}

	if(!this->host->HasRPkey())
	{
		this->Register();
	}
	else
	{
		// the host is already registered
		// start session directly
		ConnectSession();
	}
}

void HostInterface::ConnectSession()
{
	brls::Application::blockInputs();

	if(this->statusPollActive)
	{
		this->statusPollActive = false;
		brls::cancelDelay(this->statusPollId);
	}
	if(this->probeSock >= 0)
	{
		close(this->probeSock);
		this->probeSock = -1;
	}

	if(this->discoverymanager)
		this->discoverymanager->SetService(false);

	this->host->InitSession(this->io);
	CHIAKI_LOGI(this->log, "Session initiated");
	this->host->StartSession();

	this->settings->SetLastHost(this->host->GetHostName());
	this->settings->WriteFile();
}

void HostInterface::Disconnect()
{
	if(this->connected)
	{
		brls::Application::popActivity();
		this->host->StopSession();
		this->connected = false;
	}

	this->host->FiniSession();

	if(this->discoverymanager)
		this->discoverymanager->SetService(true);

	if(*this->alive)
		ScheduleStatusPoll();
}

void HostInterface::Stream()
{
	this->connected = true;
	// https://github.com/natinusala/borealis/issues/59
	// disable 60 fps limit
	brls::Application::setLimitedFPS(0);

	// show FPS counter
	// brls::Application::setDisplayFramerate(true);

	// push raw opengl stream over borealis
	brls::Application::pushActivity(
		new brls::Activity(new PSRemotePlay(this->host)),
		brls::TransitionAnimation::NONE);
}

void HostInterface::CloseStream(ChiakiQuitEvent *quit)
{
	std::string reason = chiaki_quit_reason_string(quit->reason);
	brls::Threading::sync([this, reason]() {
		brls::Application::unblockInputs();
		brls::Application::setLimitedFPS(60);
		brls::Application::notify(reason);
		Disconnect();
	});
}

void HostInterface::EnterPin(bool isError) 
{
	// enter pin callback,
	// inputs were blocked in ConnectSession
	brls::Application::unblockInputs();
	// if this is triggered as a result 
	if(isError){
		brls::Application::notify("Wrong PIN");
	}
	
	brls::Application::pushActivity(new brls::Activity(new EnterPinView(this->host, isError)));
}

MainApplication::MainApplication(DiscoveryManager *discoverymanager)
	: discoverymanager(discoverymanager)
{
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
	this->io = IO::GetInstance();
}

MainApplication::~MainApplication()
{
	this->discoverymanager->SetService(false);
	this->io->FreeController();
	// During app shutdown Borealis already tears down the video platform first.
	// FreeVideo() can touch stale Deko3D state here and crash on exit, so skip it.
}

bool MainApplication::Load()
{
	brls::Logger::info("Load[1]: start");
	this->discoverymanager->SetService(true);
	// Init the app
	brls::Logger::info("Load[2]: set logger level");
	brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

	brls::Logger::info("Load[3]: application init");
	if(!brls::Application::init())
	{
		brls::Logger::error("Unable to init Borealis application");
		return false;
	}
	// Use Borealis native global quit handling for +/START across all activities.
	brls::Application::setGlobalQuit(true);
	brls::Logger::info("Load[4]: create window");
	brls::Application::createWindow("Chiaki Remote play");
	brls::Logger::info("Load[5]: load translations");
	brls::loadTranslations();

	// init chiaki gl after borealis
	// let borealis manage the main screen/window
	// if(!io->InitVideo(0, 0, SCREEN_W, SCREEN_H))
	// {
	// 	brls::Logger::error("Failed to initiate Video");
	// }

	brls::Logger::info("Load[6]: init controller");
	brls::Logger::info("Load SDL/HiD controller");
	if(!io->InitController())
	{
		brls::Logger::error("Failed to initiate Controller");
	}

	// Create a view
	brls::Logger::info("Load[7]: create root frame");
	this->rootFrame = new brls::TabFrame();

	std::map<std::string, Host> *hosts = this->settings->GetHostsMap();
	brls::Logger::info("Load[8]: build host tabs ({})", hosts->size());
	for (auto it = hosts->begin(); it != hosts->end(); ++it)
	{
		if(it->second.HasRPkey() || it->second.IsDiscovered())
		{
			Host *host = &it->second;
			const std::string tab_title = menuTabTitle(it->second.GetHostName());
			this->rootFrame->addTab(tab_title, [this, host]() -> brls::View * {
				brls::Logger::info("Tab creator: host tab start ({})", host ? host->GetHostName() : "null");
				HostInterface *view = new HostInterface(host, this->discoverymanager);
				this->BuildConfigurationMenu(view, host);
				brls::Logger::info("Tab creator: host tab done ({})", host ? host->GetHostName() : "null");
				return view;
			});
		}
	}

	// Static tabs under the registered targets
	brls::Logger::info("Load[9]: build static tabs");
	this->rootFrame->addSeparator();
	this->rootFrame->addTab("Configuration", [this]() -> brls::View * {
		brls::Logger::info("Tab creator: configuration start");
		brls::List *config = new brls::List();
		this->BuildConfigurationMenu(config);
		brls::Logger::info("Tab creator: configuration done");
		return config;
	});
	this->rootFrame->addTab("Add Manual Host", [this]() -> brls::View * {
		brls::Logger::info("Tab creator: add host start");
		brls::List *add_host = new brls::List();
		this->BuildAddHostConfigurationMenu(add_host);
		brls::Logger::info("Tab creator: add host done");
		return add_host;
	});

	brls::Logger::info("Load[10]: push activity");
	brls::Box *main_menu_root = new brls::Box(brls::Axis::COLUMN);
	main_menu_root->setGrow(1.0f);
	main_menu_root->setDimensions(brls::View::AUTO, brls::View::AUTO);
	this->rootFrame->setGrow(1.0f);
	main_menu_root->addView(this->rootFrame);
	main_menu_root->addView(new brls::BottomBar());
	brls::Application::pushActivity(new brls::Activity(main_menu_root));

	brls::Logger::info("Load[11]: enter main loop");
	int auto_connect_countdown = -1;
	if(this->settings->GetAutoConnect())
	{
		std::string last_host_name = this->settings->GetLastHost();
		if(last_host_name.length() > 0)
		{
			auto *host_map = this->settings->GetHostsMap();
			auto it = host_map->find(last_host_name);
			if(it != host_map->end() && (it->second.HasRPkey() || it->second.IsDiscovered()))
				auto_connect_countdown = 120;
		}
	}

	while(brls::Application::mainLoop())
	{
		if(auto_connect_countdown > 0)
		{
			auto_connect_countdown--;
		}
		else if(auto_connect_countdown == 0)
		{
			auto_connect_countdown = -1;
			std::string last_host_name = this->settings->GetLastHost();
			auto *host_map = this->settings->GetHostsMap();
			auto it = host_map->find(last_host_name);
			if(it != host_map->end() && it->second.HasRPkey())
			{
				brls::Logger::info("Auto-connecting to last host: {}", last_host_name);
				Host *auto_host = &it->second;
				HostInterface *hi = new HostInterface(auto_host, this->discoverymanager);
				try
				{
					hi->ConnectSession();
					hi->Stream();
				}
				catch(...)
				{
					brls::Logger::error("Auto-connect failed");
					delete hi;
				}
			}
		}
	}
	brls::Logger::info("Load[12]: main loop ended");
	
	return true;
}

bool MainApplication::BuildConfigurationMenu(brls::List *ls, Host *host)
{
	brls::Logger::info("BuildConfigurationMenu: start ({})", host ? host->GetHostName() : "global");
	std::string psn_account_id_string = this->settings->GetPSNAccountID(host);
	brls::InputListItem *psn_account_id = new brls::InputListItem("PSN Account ID", psn_account_id_string,
		"Account ID in base64 format", "PS5 or PS4 System Software version 7.00 and greater", CHIAKI_PSN_ACCOUNT_ID_SIZE * 2,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	auto psn_account_id_cb = [this, host, psn_account_id](brls::View *view) {
		// retrieve, push and save setting
		this->settings->SetPSNAccountID(host, psn_account_id->getValue());
		// write on disk
		this->settings->WriteFile();
	};
	psn_account_id->getClickEvent()->subscribe(psn_account_id_cb);
	ls->addView(psn_account_id);

	
	brls::InputListItem *lookup_account_id = new brls::InputListItem("Lookup Account ID by username",
		"", "PSN Account name", "Only works if the PSN account's privacy settings are set to public!", 16,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	auto lookup_account_id_cb = [this, host, lookup_account_id, psn_account_id](brls::View *view) {
		// lookup psn_online
		std::string entered_value = lookup_account_id->getValue();
		this->discoverymanager->makeRequest(entered_value, 
			 [this, host, lookup_account_id, psn_account_id](const std::string& accountId) {
				// retrieve, push and save setting
				CHIAKI_LOGI(this->log, fmt::format("Found account id {}", accountId).c_str());
				this->settings->SetPSNAccountID(host, std::string(accountId));
				// write on disk
				this->settings->WriteFile();
				lookup_account_id->setValue("");
				// set the view of the other field,
				psn_account_id->setValue(std::string(accountId));
			},
			[this, lookup_account_id, entered_value](const std::string& error) {
				CHIAKI_LOGE(this->log, error.c_str());
				lookup_account_id->setValue("");
				DIALOG(upaid, fmt::format("Unable to fetch account id for {}", entered_value));
			}
		);
	};
	lookup_account_id->getClickEvent()->subscribe(lookup_account_id_cb);
	ls->addView(lookup_account_id);


	std::string psn_online_id_string = this->settings->GetPSNOnlineID(host);
	brls::InputListItem *psn_online_id = new brls::InputListItem("PSN Online ID",
		psn_online_id_string, "", "", 16,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	auto psn_online_id_cb = [this, host, psn_online_id](brls::View *view) {
		// retrieve, push and save setting
		this->settings->SetPSNOnlineID(host, psn_online_id->getValue());
		// write on disk
		this->settings->WriteFile();
	};
	psn_online_id->getClickEvent()->subscribe(psn_online_id_cb);
	ls->addView(psn_online_id);


	int value;
	ChiakiVideoResolutionPreset resolution_preset = this->settings->GetVideoResolution(host);
	switch(resolution_preset)
	{
		case CHIAKI_VIDEO_RESOLUTION_PRESET_1080p:
			value = 0;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_720p:
			value = 1;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_540p:
			value = 2;
			break;
		case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
			value = 3;
			break;
	}

	auto add_hint = [ls](const char *text) {
		brls::Label *hint = new brls::Label();
		hint->setText(text);
		hint->setFontSize(13.0f);
		hint->setTextColor(nvgRGB(140, 140, 140));
		hint->setMarginTop(-4.0f);
		ls->addView(hint);
	};

	brls::SelectListItem *resolution = new brls::SelectListItem(
		"Resolution", { "1080p (PS5 and PS4 Pro only)", "720p", "540p", "360p" }, value);

	auto resolution_cb = [this, host](int result) {
		ChiakiVideoResolutionPreset value = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
		switch(result)
		{
			case 0:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
				break;
			case 1:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
				break;
			case 2:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
				break;
			case 3:
				value = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
				break;
		}
		this->settings->SetVideoResolution(host, value);
		this->settings->WriteFile();
	};
	resolution->getValueSelectedEvent()->subscribe(resolution_cb);
	ls->addView(resolution);

	ChiakiVideoFPSPreset fps_preset = this->settings->GetVideoFPS(host);
	value = 1; // default: 60fps
	switch(fps_preset)
	{
		case CHIAKI_VIDEO_FPS_PRESET_120:
			value = 0;
			break;
		case CHIAKI_VIDEO_FPS_PRESET_60:
			value = 1;
			break;
		case CHIAKI_VIDEO_FPS_PRESET_30:
			value = 2;
			break;
	}

	brls::SelectListItem *fps = new brls::SelectListItem(
		"FPS", { "120fps (experimental)", "60fps", "30fps" }, value);

	auto fps_cb = [this, host](int result) {
		ChiakiVideoFPSPreset value = CHIAKI_VIDEO_FPS_PRESET_60;
		switch(result)
		{
			case 0:
				value = CHIAKI_VIDEO_FPS_PRESET_120;
				break;
			case 1:
				value = CHIAKI_VIDEO_FPS_PRESET_60;
				break;
			case 2:
				value = CHIAKI_VIDEO_FPS_PRESET_30;
				break;
		}
		this->settings->SetVideoFPS(host, value);
		this->settings->WriteFile();
	};

	fps->getValueSelectedEvent()->subscribe(fps_cb);
	ls->addView(fps);

	std::vector<std::string> bitrate_labels = {"Auto"};
	std::vector<int> br_values = {0};
	for(int i = 1; i <= 50; i++)
	{
		bitrate_labels.push_back(std::to_string(i) + " Mbps");
		br_values.push_back(i * 1000);
	}
	int bitrate_value = 0;
	int current_bitrate = this->settings->GetBitrate(host);
	for(size_t i = 0; i < br_values.size(); i++)
	{
		if(br_values[i] == current_bitrate)
		{
			bitrate_value = (int)i;
			break;
		}
	}
	brls::SelectListItem *bitrate = new brls::SelectListItem("Bitrate", bitrate_labels, bitrate_value);
	ls->addView(bitrate);
	add_hint("Auto lets the console adjust bitrate based on network conditions");

	CodecPreset codec_preset = this->settings->GetCodec(host);
	value = (int)codec_preset;
	brls::SelectListItem *codec = new brls::SelectListItem(
		"Video Codec", {"Auto (recommended)", "H.264", "H.265 / HEVC"}, value);
	auto codec_cb = [this, host](int result) {
		this->settings->SetCodec(host, static_cast<CodecPreset>(result));
		this->settings->WriteFile();
	};
	codec->getValueSelectedEvent()->subscribe(codec_cb);
	ls->addView(codec);
	add_hint("H.265 uses less bandwidth but needs more CPU to decode");

	const std::array<double, 4> packet_loss_values = {0.01, 0.02, 0.03, 0.05};
	int packet_loss_value = 2;
	double current_packet_loss = this->settings->GetPacketLossMax(host);
	for(size_t i = 0; i < packet_loss_values.size(); i++)
	{
		if(current_packet_loss <= packet_loss_values[i] + 0.0001)
		{
			packet_loss_value = (int)i;
			break;
		}
	}
	brls::SelectListItem *packet_loss_max = new brls::SelectListItem(
		"Packet Loss Max",
		{"1%", "2%", "3% (default)", "5%"},
		packet_loss_value);
	auto packet_loss_max_cb = [this, host, packet_loss_values](int result) {
		if(result < 0 || result >= (int)packet_loss_values.size())
			return;
		this->settings->SetPacketLossMax(host, packet_loss_values[(size_t)result]);
		this->settings->WriteFile();
	};
	packet_loss_max->getValueSelectedEvent()->subscribe(packet_loss_max_cb);

	brls::Label *packet_loss_hint = new brls::Label();
	packet_loss_hint->setText("Caps loss reported to console to prevent aggressive bitrate drops");
	packet_loss_hint->setFontSize(13.0f);
	packet_loss_hint->setTextColor(nvgRGB(140, 140, 140));
	packet_loss_hint->setMarginTop(-4.0f);

	bool bitrate_is_auto = (current_bitrate == 0);
	if(bitrate_is_auto)
	{
		ls->addView(packet_loss_max);
		ls->addView(packet_loss_hint);
	}

	auto bitrate_cb = [this, host, br_values, ls, packet_loss_max, packet_loss_hint](int result) {
		if(result < 0 || result >= (int)br_values.size())
			return;
		this->settings->SetBitrate(host, br_values[(size_t)result]);
		this->settings->WriteFile();
		bool is_auto = (result == 0);
		packet_loss_max->setVisibility(is_auto ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
		packet_loss_hint->setVisibility(is_auto ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
	};
	bitrate->getValueSelectedEvent()->subscribe(bitrate_cb);

	if(!bitrate_is_auto)
	{
		ls->addView(packet_loss_max);
		ls->addView(packet_loss_hint);
		packet_loss_max->setVisibility(brls::Visibility::GONE);
		packet_loss_hint->setVisibility(brls::Visibility::GONE);
	}

	value = this->settings->GetEnableIDROnFECFailure(host) ? 0 : 1;
	brls::SelectListItem *idr_on_fec_failure = new brls::SelectListItem(
		"IDR On FEC Failure", {"Enabled", "Disabled"}, value);
	auto idr_on_fec_failure_cb = [this, host](int result) {
		this->settings->SetEnableIDROnFECFailure(host, result == 0);
		this->settings->WriteFile();
	};
	idr_on_fec_failure->getValueSelectedEvent()->subscribe(idr_on_fec_failure_cb);
	ls->addView(idr_on_fec_failure);
	add_hint("Request a fresh keyframe when error correction fails, reduces glitches");

	const std::array<int, 5> decode_queue_values = {2, 3, 4, 6, 8};
	int decode_queue_value = 2;
	int current_decode_queue = this->settings->GetDecodeQueueSize(host);
	for(size_t i = 0; i < decode_queue_values.size(); i++)
	{
		if(current_decode_queue <= decode_queue_values[i])
		{
			decode_queue_value = (int)i;
			break;
		}
	}
	brls::SelectListItem *decode_queue_size = new brls::SelectListItem(
		"Decode Queue Size", {"2 (lowest latency)", "3", "4 (default)", "6", "8"}, decode_queue_value);
	auto decode_queue_size_cb = [this, host, decode_queue_values](int result) {
		if(result < 0 || result >= (int)decode_queue_values.size())
			return;
		this->settings->SetDecodeQueueSize(host, decode_queue_values[(size_t)result]);
		this->settings->WriteFile();
	};
	decode_queue_size->getValueSelectedEvent()->subscribe(decode_queue_size_cb);
	ls->addView(decode_queue_size);
	add_hint("Frames buffered before display. Lower = less latency, higher = smoother");

	std::vector<std::string> rumble_labels;
	for(int i = 0; i <= 100; i++)
		rumble_labels.push_back(i == 0 ? "Off" : std::to_string(i) + "%");
	int rumble_value = this->settings->GetHaptic(host);
	if(rumble_value < 0 || rumble_value > 100) rumble_value = 0;
	brls::SelectListItem *haptic = new brls::SelectListItem("Rumble Intensity", rumble_labels, rumble_value);
	auto haptic_cb = [this, host](int result) {
		if(result < 0 || result > 100)
			return;
		this->settings->SetHaptic(host, result);
		this->settings->WriteFile();
	};
	haptic->getValueSelectedEvent()->subscribe(haptic_cb);
	ls->addView(haptic);

	const std::array<int, 7> volume_values = {50, 75, 100, 125, 150, 180, 200};
	int volume_value = 5;
	int current_volume = this->settings->GetAudioVolume(host);
	for(size_t i = 0; i < volume_values.size(); i++)
	{
		if(current_volume <= volume_values[i])
		{
			volume_value = (int)i;
			break;
		}
	}
	brls::SelectListItem *audio_volume = new brls::SelectListItem(
		"Audio Volume", {"50%", "75%", "100%", "125%", "150%", "180% (default)", "200%"}, volume_value);
	auto audio_volume_cb = [this, host, volume_values](int result) {
		if(result < 0 || result >= (int)volume_values.size())
			return;
		this->settings->SetAudioVolume(host, volume_values[(size_t)result]);
		this->settings->WriteFile();
	};
	audio_volume->getValueSelectedEvent()->subscribe(audio_volume_cb);
	ls->addView(audio_volume);

	value = this->settings->GetVsync(host) ? 0 : 1;
	brls::SelectListItem *vsync = new brls::SelectListItem(
		"VSync", {"On (default)", "Off (lower latency, may tear)"}, value);
	auto vsync_cb = [this, host](int result) {
		this->settings->SetVsync(host, result == 0 ? 1 : 0);
		this->settings->WriteFile();
	};
	vsync->getValueSelectedEvent()->subscribe(vsync_cb);
	ls->addView(vsync);
	add_hint("Syncs frames to display refresh. Off reduces latency but may cause tearing");

	const int deband_values[] = {0, 16, 32, 64};
	std::vector<std::string> deband_options = {
		"Off", "Low (16)", "Medium (32) (Recommended)", "High (64)"};
	int cur_deband = this->settings->GetDithering();
	int deband_idx = 0;
	for(int i = 3; i >= 0; i--)
	{
		if(cur_deband >= deband_values[i]) { deband_idx = i; break; }
	}
	brls::SelectListItem *debanding = new brls::SelectListItem(
		"Debanding", deband_options, deband_idx);
	auto deband_cb = [this, deband_values](int result) {
		this->settings->SetDithering(deband_values[result]);
		this->settings->WriteFile();
	};
	debanding->getValueSelectedEvent()->subscribe(deband_cb);
	ls->addView(debanding);
	add_hint("Smooths compression banding in gradients. No measurable GPU cost");

	int stats_val = this->settings->GetShowStats() ? 0 : 1;
	brls::SelectListItem *show_stats = new brls::SelectListItem(
		"Stats HUD", {"Enabled", "Disabled"}, stats_val);
	auto stats_cb = [this](int result) {
		this->settings->SetShowStats(result == 0 ? 1 : 0);
		this->settings->WriteFile();
	};
	show_stats->getValueSelectedEvent()->subscribe(stats_cb);
	ls->addView(show_stats);
	add_hint("Shows FPS, decode time, bitrate, and frame drops during streaming");

	const std::array<int, 5> deadzone_values = {0, 5, 10, 15, 20};
	int deadzone_value = 0;
	int current_deadzone = this->settings->GetStickDeadzone(host);
	for(size_t i = 0; i < deadzone_values.size(); i++)
	{
		if(current_deadzone <= deadzone_values[i])
		{
			deadzone_value = (int)i;
			break;
		}
	}
	brls::SelectListItem *stick_deadzone = new brls::SelectListItem(
		"Stick Deadzone", {"None", "5%", "10%", "15%", "20%"}, deadzone_value);
	auto stick_deadzone_cb = [this, host, deadzone_values](int result) {
		if(result < 0 || result >= (int)deadzone_values.size())
			return;
		this->settings->SetStickDeadzone(host, deadzone_values[(size_t)result]);
		this->settings->WriteFile();
	};
	stick_deadzone->getValueSelectedEvent()->subscribe(stick_deadzone_cb);
	ls->addView(stick_deadzone);

	value = (int)this->settings->GetAudioBackend(host);
	brls::SelectListItem *audio_backend = new brls::SelectListItem(
		"Audio Backend", {"SDL (default)", "Audren (low latency)"}, value);
	auto audio_backend_cb = [this, host](int result) {
		this->settings->SetAudioBackend(host, static_cast<AudioBackend>(result));
		this->settings->WriteFile();
	};
	audio_backend->getValueSelectedEvent()->subscribe(audio_backend_cb);
	ls->addView(audio_backend);
	add_hint("Audren is the native Switch audio driver, lower latency than SDL");

	if(host == nullptr)
	{
		value = this->settings->GetAutoConnect() ? 0 : 1;
		brls::SelectListItem *auto_connect = new brls::SelectListItem(
			"Auto-Connect", {"Enabled", "Disabled"}, value);
		auto auto_connect_cb = [this](int result) {
			this->settings->SetAutoConnect(result == 0 ? 1 : 0);
			this->settings->WriteFile();
		};
		auto_connect->getValueSelectedEvent()->subscribe(auto_connect_cb);
		ls->addView(auto_connect);

		std::string last = this->settings->GetLastHost();
		if(last.length() > 0)
		{
			brls::ListItem *last_host_item = new brls::ListItem("Last Connected Host");
			last_host_item->setValue(last.c_str());
			ls->addView(last_host_item);
		}
	}

	if(host != nullptr)
	{	
	    brls::Label *info = new brls::Label();
		info->setText("Host information");
	    ls->addView(info);
	
	    std::string host_name_string = this->settings->GetHostName(host);
	    brls::ListItem *host_name = new brls::ListItem("PS Hostname");
	    host_name->setValue(host_name_string.c_str());
	    ls->addView(host_name);
	
	    std::string host_addr_string = settings->GetHostAddr(host);		
		brls::InputListItem *host_addr = new brls::InputListItem("Remote IP/name",
		host_addr_string, "IP address or fqdn", "", 255,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);		
		
	    auto host_addr_cb = [this, host, host_addr](brls::View *view)
		{
			std::string new_addr = host_addr->getValue();	
			this->settings->SetHostAddr(host, new_addr);	
			this->settings->WriteFile();		
			brls::Application::notify("PS Address updated successfully");
	    };
	
	    host_addr->getClickEvent()->subscribe(host_addr_cb);
	    ls->addView(host_addr);

		brls::ListItem *host_regist_state_item = new brls::ListItem("Register Status");
		host_regist_state_item->setValue(!settings->GetHostRPKey(host).empty() ? "registered" : "unregistered");
		ls->addView(host_regist_state_item);
	}

	brls::Logger::info("BuildConfigurationMenu: done ({})", host ? host->GetHostName() : "global");
	return true;
}

void MainApplication::BuildAddHostConfigurationMenu(brls::List *add_host)
{
	brls::Logger::info("BuildAddHostConfigurationMenu: start");
	// create host for wan connection
	// brls::Label* add_host_label = new brls::Label(brls::LabelStyle::REGULAR,
	// 	"Add Host configuration", true);

	brls::InputListItem *display_name = new brls::InputListItem("Display name",
		"default", "configuration name", "", 16,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	add_host->addView(display_name);

	brls::InputListItem *address = new brls::InputListItem("Remote IP/name",
		"", "IP address or fqdn", "", 255,
		brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH |
			brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH);

	add_host->addView(address);

	// TODO
	// brls::ListItem* port = new brls::ListItem("Remote session port",  "tcp/udp 9295");
	// brls::ListItem* port = new brls::ListItem("Remote stream port",  "udp 9296");
	// brls::ListItem* port = new brls::ListItem("Remote Senkusha port",  "udp 9297");
	brls::SelectListItem *ps_version = new brls::SelectListItem("Target PlayStation System",
		{ "PS5", "PS4 System Version > 8.00", "7.00 < PS4 System Version < 8.00", "PS4 System Version < 7.00" }); //This may look like a pointless change but it does actually make things a bit clearer for the user
	add_host->addView(ps_version);

	brls::ListItem *register_host = new brls::ListItem("Register");
	auto register_host_cb = [this, display_name, address, ps_version](brls::View *view) {
		bool err = false;
		std::string dn = display_name->getValue();
		std::string addr = address->getValue();
		ChiakiTarget version = CHIAKI_TARGET_PS4_UNKNOWN;

		switch(ps_version->getSelectedValue())
		{
			case 0:
				// ps5 v1
				version = CHIAKI_TARGET_PS5_1;
				break;
			case 1:
				// ps4 v8
				version = CHIAKI_TARGET_PS4_10;
				break;
			case 2:
				// ps4 v7
				version = CHIAKI_TARGET_PS4_9;
				break;
			case 3:
				// ps4 v6
				version = CHIAKI_TARGET_PS4_8;
				break;
		}

		if(dn.length() <= 0)
		{
			brls::Application::notify("No Display name defined");
			err = true;
		}

		if(addr.length() <= 0)
		{
			brls::Application::notify("No Remote address provided");
			err = true;
		}

		if(version <= CHIAKI_TARGET_PS4_UNKNOWN)
		{
			brls::Application::notify("No PlayStation Version provided");
			err = true;
		}

		if(err)
			return;

		Host *host = this->settings->GetOrCreateHost(&dn);
		host->SetHostAddr(addr);
		host->SetChiakiTarget(version);
		HostInterface::Register(host);
	};
	register_host->getClickEvent()->subscribe(register_host_cb);

	add_host->addView(register_host);
	brls::Logger::info("BuildAddHostConfigurationMenu: done");
}
