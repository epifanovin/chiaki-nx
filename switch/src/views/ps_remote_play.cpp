#include "views/ps_remote_play.h"
#include "settings.h"

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
	this->io->ResetStats();
	this->last_fps_time = std::chrono::steady_clock::now();
	this->stats_visible = Settings::GetInstance()->GetShowStats() != 0;
	this->setFocusable(true);
	this->setHideHighlight(true);
	this->setHideClickAnimation(true);
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
	this->io->MainLoop();
	this->host->SendFeedbackState();

	if(this->stats_visible)
		this->DrawStatsHUD(vg, x, y, width);
}

void PSRemotePlay::DrawStatsHUD(NVGcontext *vg, float x, float y, float width)
{
	(void)width;
	auto now = std::chrono::steady_clock::now();
	uint64_t current_decoded = this->io->GetFramesDecoded();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_fps_time).count();
	if(elapsed >= 1000)
	{
		uint64_t delta = current_decoded - this->frame_count_at_last_fps;
		this->measured_fps = delta * 1000.0f / (float)elapsed;
		this->frame_count_at_last_fps = current_decoded;
		this->last_fps_time = now;
	}

	float decode_ms = this->io->GetLastDecodeTimeUs() / 1000.0f;
	double bitrate_mbps = this->host->GetMeasuredBitrate();
	uint64_t total_recv = this->io->GetTotalFramesReceived();
	uint64_t total_lost = this->io->GetTotalFramesLost();
	float drop_pct = 0.0f;
	if(total_recv + total_lost > 0)
		drop_pct = (float)total_lost / (float)(total_recv + total_lost) * 100.0f;

	char line1[128], line2[128];
	snprintf(line1, sizeof(line1), "FPS: %.0f  Decode: %.1fms  %.1f Mbps", this->measured_fps, decode_ms, bitrate_mbps);
	snprintf(line2, sizeof(line2), "Dropped: %.1f%%  (%llu/%llu)", drop_pct,
		(unsigned long long)total_lost, (unsigned long long)(total_recv + total_lost));

	nvgFontSize(vg, 14.0f);
	nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

	constexpr float pad = 6.0f;
	constexpr float line_h = 18.0f;
	float tx = x + 8.0f + pad;
	float ty = y + 8.0f + pad;

	float b1[4], b2[4];
	nvgTextBounds(vg, tx, ty, line1, nullptr, b1);
	nvgTextBounds(vg, tx, ty + line_h, line2, nullptr, b2);
	float text_w = (b1[2] - b1[0] > b2[2] - b2[0]) ? b1[2] - b1[0] : b2[2] - b2[0];

	nvgBeginPath(vg);
	nvgRoundedRect(vg, x + 8.0f, y + 8.0f, text_w + pad * 2, line_h * 2 + pad * 2, 4.0f);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
	nvgFill(vg);

	nvgFillColor(vg, nvgRGBA(0, 255, 0, 230));
	nvgText(vg, tx, ty, line1, nullptr);

	NVGcolor drop_color = drop_pct < 1.0f ? nvgRGBA(0, 255, 0, 230)
	                     : drop_pct < 5.0f ? nvgRGBA(255, 255, 0, 230)
	                                       : nvgRGBA(255, 60, 60, 230);
	nvgFillColor(vg, drop_color);
	nvgText(vg, tx, ty + line_h, line2, nullptr);
}

void PSRemotePlay::OpenOverlay()
{
	if(this->overlay_open)
		return;

	this->overlay_open = true;
	this->io->overlay_open.store(true, std::memory_order_release);
	brls::Dialog *dialog = new brls::Dialog("Streaming options");
	dialog->setCancelable(false);

	dialog->addButton("Resume", [this]() {
		this->overlay_open = false;
		this->io->overlay_open.store(false, std::memory_order_release);
	});

	dialog->addButton("Stop Streaming", [this]() {
		this->overlay_open = false;
		this->io->overlay_open.store(false, std::memory_order_release);
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
