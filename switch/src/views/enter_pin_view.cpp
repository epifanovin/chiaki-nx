
#include "views/enter_pin_view.h"

#define PIN_MAX_LEN 4

EnterPinView::EnterPinView(Host *host, bool isError)
	: host(host)
{
	this->isError = isError;
	this->settings = Settings::GetInstance();
	this->log = this->settings->GetLogger();
}

EnterPinView::~EnterPinView()
{
}

void EnterPinView::ClosePinView()
{
	brls::Application::popActivity();
	if(this->input_blocked)
	{
		brls::Application::unblockInputs();
		this->input_blocked = false;
	}
}

void EnterPinView::draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext *ctx)
{
	if(this->ime_requested)
		return;
	this->ime_requested = true;

	// the host is not logged in yet
	// use callback to ensure that the message is showed on screen
	// before the Swkbd
	const std::string title = !this->isError ? "Please enter your login pin" : "Login PIN incorrect: please try again!";
	auto login_pin_input_cb = [this](long pin) {
		// prevent users form messing with the gui
		brls::Application::blockInputs();
		this->input_blocked = true;
		std::string pin_str = std::to_string(pin);
		// left pad. ie "0001"
		pin_str.insert(0, PIN_MAX_LEN - pin_str.length(), '0'); 
		this->login_pin = pin_str;
	};
	if(!brls::Application::getImeManager()->openForNumber(login_pin_input_cb,
		title, "4 digits without spaces", PIN_MAX_LEN, "", "", ""))
	{
		brls::Application::notify("Unable to open keyboard");
		this->ClosePinView();
		return;
	}
	if(this->login_pin.empty())
	{
		this->ClosePinView();
		return;
	}
	CHIAKI_LOGI(this->log, "Sending pin to libchiaki");
	ChiakiErrorCode ret = chiaki_session_set_login_pin(&(this->host->session), reinterpret_cast<const uint8_t*>(this->login_pin.c_str()), this->login_pin.length());
	if(ret != CHIAKI_ERR_SUCCESS)	
	{
		brls::Application::notify(chiaki_error_string(ret));
	}
	this->ClosePinView();
}
