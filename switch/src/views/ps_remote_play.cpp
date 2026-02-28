#include "views/ps_remote_play.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace
{
constexpr uint32_t STREAM_OVERLAY_TOUCH_EDGE_PX = 80;
constexpr uint32_t STATS_TOUCH_ZONE_SIZE = 120;
}

PSRemotePlay::PSRemotePlay(Host *host)
	: host(host)
{
	this->io = IO::GetInstance();
	this->last_fps_time = std::chrono::steady_clock::now();
	this->setFocusable(true);
	this->registerAction("Stream menu", brls::BUTTON_BACK, [this](brls::View *view) {
		(void)view;
		this->OpenOverlay();
		return true;
	}, false, false, brls::SOUND_CLICK);
}

void PSRemotePlay::draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext *ctx)
{
	(void)height;
	(void)style;
	(void)ctx;

	this->HandleOverlayTouchTrigger();
	this->HandleStatsToggle();
	this->io->MainLoop();
	this->host->SendFeedbackState();

	if(this->stats_visible)
		this->DrawStatsHUD(vg, x, y, width);
}

void PSRemotePlay::DrawStatsHUD(NVGcontext *vg, float x, float y, float width)
{
	auto now = std::chrono::steady_clock::now();
	uint64_t current_rendered = this->io->GetFramesDecoded();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_fps_time).count();
	if(elapsed >= 1000)
	{
		uint64_t delta = current_rendered - this->frame_count_at_last_fps;
		this->measured_fps = delta * 1000.0f / (float)elapsed;
		this->frame_count_at_last_fps = current_rendered;
		this->last_fps_time = now;
	}

	float decode_ms = this->io->GetLastDecodeTimeUs() / 1000.0f;
	int audio_kb = this->io->GetAudioQueueBytes() / 1024;

	char stats_buf[256];
	snprintf(stats_buf, sizeof(stats_buf),
		"FPS: %.0f | Decode: %.1fms | Queue: %d\nAudio: %dKB",
		this->measured_fps, decode_ms,
		this->io->GetDecodeQueueSize(), audio_kb);

	float text_x = x + 10.0f;
	float text_y = y + 24.0f;

	nvgFontSize(vg, 16.0f);
	nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

	nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
	float bounds[4];
	nvgTextBoxBounds(vg, text_x, text_y, width - 20.0f, stats_buf, nullptr, bounds);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, bounds[0] - 4, bounds[1] - 2, bounds[2] - bounds[0] + 8, bounds[3] - bounds[1] + 4, 4.0f);
	nvgFill(vg);

	nvgFillColor(vg, nvgRGBA(0, 255, 0, 230));
	nvgTextBox(vg, text_x, text_y, width - 20.0f, stats_buf, nullptr);
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

void PSRemotePlay::HandleStatsToggle()
{
#ifdef __SWITCH__
	HidTouchScreenState sw_state = {0};
	hidGetTouchScreenStates(&sw_state, 1);

	if(sw_state.count == 0)
	{
		this->stats_touch_armed = true;
		return;
	}

	if(!this->stats_touch_armed || this->overlay_open)
		return;

	for(int i = 0; i < sw_state.count; i++)
	{
		if(sw_state.touches[i].x < STATS_TOUCH_ZONE_SIZE &&
		   sw_state.touches[i].y < STATS_TOUCH_ZONE_SIZE)
		{
			this->stats_touch_armed = false;
			this->stats_visible = !this->stats_visible;
			return;
		}
	}
#endif
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
