#include "views/ps_remote_play.h"
PSRemotePlay::PSRemotePlay(Host *host)
	: host(host)
{
	this->io = IO::GetInstance();
}

void PSRemotePlay::draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext *ctx)
{
	this->io->MainLoop();
	this->host->SendFeedbackState();
}

PSRemotePlay::~PSRemotePlay()
{
}
