#include "views/ps_remote_play.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace
{
constexpr uint32_t STREAM_OVERLAY_TOUCH_EDGE_PX = 80;
}

PSRemotePlay::PSRemotePlay(Host *host)
	: host(host)
{
	this->io = IO::GetInstance();
	this->setFocusable(true);
	this->registerAction("Stream menu", brls::BUTTON_BACK, [this](brls::View *view) {
		(void)view;
		this->OpenOverlay();
		return true;
	}, false, false, brls::SOUND_CLICK);
}

void PSRemotePlay::draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext *ctx)
{
	(void)vg;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	(void)style;
	(void)ctx;

	this->HandleOverlayTouchTrigger();
	this->io->MainLoop();
	this->host->SendFeedbackState();
}

void PSRemotePlay::OpenOverlay()
{
	if(this->overlay_open)
		return;

	this->overlay_open = true;
	brls::Dialog *dialog = new brls::Dialog("Streaming options");
	dialog->setCancelable(false);

	dialog->addButton("Resume", [this]() {
		this->overlay_open = false;
	});

	dialog->addButton("Stop Streaming", [this]() {
		this->overlay_open = false;
		this->StopStreaming();
	});

	dialog->open();
}

void PSRemotePlay::StopStreaming()
{
	brls::Application::notify("Stopping stream");
	this->host->StopSession();
}

void PSRemotePlay::HandleOverlayTouchTrigger()
{
#ifdef __SWITCH__
	HidTouchScreenState sw_state = {0};
	hidGetTouchScreenStates(&sw_state, 1);

	if(sw_state.count == 0)
	{
		this->overlay_touch_armed = true;
		return;
	}

	if(!this->overlay_touch_armed || this->overlay_open)
		return;

	for(int i = 0; i < sw_state.count; i++)
	{
		if(sw_state.touches[i].x > STREAM_OVERLAY_TOUCH_EDGE_PX)
			continue;

		this->overlay_touch_armed = false;
		this->OpenOverlay();
		return;
	}
#endif
}

PSRemotePlay::~PSRemotePlay()
{
}
