// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DEKO_VIDEO_RENDERER_H
#define CHIAKI_DEKO_VIDEO_RENDERER_H

#include <cstdint>
#include <vector>
#include <optional>

extern "C" {
#include <libavutil/frame.h>
}

#if defined(__SWITCH__) && defined(BOREALIS_USE_DEKO3D)
#include <deko3d.hpp>

#include <borealis.hpp>
#include <borealis/platforms/switch/switch_video.hpp>
#include <nanovg/framework/CDescriptorSet.h>
#include <nanovg/framework/CShader.h>
#endif

class DekoVideoRenderer
{
public:
	DekoVideoRenderer();
	~DekoVideoRenderer();

	bool Draw(AVFrame *frame, int width, int height);
	void Reset();

private:
#if defined(__SWITCH__) && defined(BOREALIS_USE_DEKO3D)
	struct FrameMapping
	{
		uint32_t handle = 0;
		void *cpu_addr = nullptr;
		uint32_t size = 0;
		uint32_t chroma_offset = 0;
		dk::UniqueMemBlock memblock;
		dk::Image luma;
		dk::Image chroma;
		dk::ImageDescriptor luma_desc;
		dk::ImageDescriptor chroma_desc;
		};

		bool EnsureInitialized(AVFrame *frame, int width, int height);
		bool UpdateFrameMapping(AVFrame *frame);

	bool initialized = false;
	int frame_width = 0;
	int frame_height = 0;
	int screen_width = 0;
	int screen_height = 0;
	int luma_texture_id = 0;
	int chroma_texture_id = 0;
	int current_mapping_index = -1;
	uint32_t update_cmdmem_slice = 0;

	brls::SwitchVideoContext *video_context = nullptr;
	dk::Device device;
	dk::Queue queue;

	std::optional<CMemPool> code_pool;
	std::optional<CMemPool> data_pool;
	dk::UniqueCmdBuf draw_cmdbuf;
	dk::UniqueCmdBuf update_cmdbuf;
	CMemPool::Handle draw_cmdmem;
	CMemPool::Handle update_cmdmem;
	DkCmdList draw_cmdlist = 0;
	CMemPool::Handle vertex_buffer;

	CDescriptorSet<4096U> *image_descriptor_set = nullptr;
	CShader vertex_shader;
	CShader fragment_shader;
	dk::ImageLayout luma_layout;
	dk::ImageLayout chroma_layout;
	std::vector<FrameMapping> frame_mappings;
#endif
};

#endif // CHIAKI_DEKO_VIDEO_RENDERER_H
