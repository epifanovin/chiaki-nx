#ifndef CHIAKI_PS_REMOTE_PLAY_H
#define CHIAKI_PS_REMOTE_PLAY_H

#include <borealis.hpp>
#include <chrono>
#include "host.h"
#include "io.h"

class PSRemotePlay : public brls::View
{
	private:
		IO *io;
		Host *host;
		bool overlay_touch_armed = true;
		bool overlay_open = false;
		bool stats_visible = false;
		bool stats_touch_armed = true;

		uint64_t frame_count_at_last_fps = 0;
		std::chrono::steady_clock::time_point last_fps_time;
		float measured_fps = 0.0f;

		void OpenOverlay();
		void StopStreaming();
		void HandleOverlayTouchTrigger();
		void HandleStatsToggle();
		void DrawStatsHUD(NVGcontext *vg, float x, float y, float width);
	public:
		PSRemotePlay(Host *host);
		~PSRemotePlay();

		void draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext *ctx) override;
};


#endif // CHIAKI_PS_REMOTE_PLAY_H
