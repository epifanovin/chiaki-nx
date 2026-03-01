// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "deko_video_renderer.h"

#if defined(__SWITCH__) && defined(BOREALIS_USE_DEKO3D)

#include <array>
#include <cstddef>
#include <cstring>

extern "C" {
#include <libavutil/hwcontext_nvtegra.h>
}

namespace {
static constexpr unsigned STATIC_CMD_SIZE = 0x10000;
static constexpr unsigned UPDATE_CMD_SLICE_SIZE = 0x1000;

struct Vertex
{
	float position[3];
	float uv[2];
};

constexpr std::array<Vertex, 4> QUAD_VERTICES = {{
	{{-1.0f, +1.0f, 0.0f}, {0.0f, 0.0f}},
	{{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
	{{+1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
	{{+1.0f, +1.0f, 0.0f}, {1.0f, 0.0f}},
}};

constexpr std::array<DkVtxAttribState, 2> VERTEX_ATTRIB_STATE = {{
	DkVtxAttribState{0, 0, offsetof(Vertex, position), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
	DkVtxAttribState{0, 0, offsetof(Vertex, uv), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
}};

constexpr std::array<DkVtxBufferState, 1> VERTEX_BUFFER_STATE = {{
	DkVtxBufferState{sizeof(Vertex), 0},
}};
} // namespace

DekoVideoRenderer::DekoVideoRenderer() = default;

DekoVideoRenderer::~DekoVideoRenderer()
{
	Reset();
}

void DekoVideoRenderer::Reset()
{
	if(video_context)
		queue.waitIdle();

	frame_mappings.clear();
	vertex_buffer.destroy();
	update_cmdmem.destroy();
	draw_cmdmem.destroy();
	data_pool.reset();
	code_pool.reset();
	draw_cmdlist = 0;
	luma_texture_id = 0;
	chroma_texture_id = 0;
	current_mapping_index = -1;
	update_cmdmem_slice = 0;
	initialized = false;
	video_context = nullptr;
}

bool DekoVideoRenderer::EnsureInitialized(AVFrame *frame, int width, int height)
{
	if(initialized)
		return true;
	if(!frame)
		return false;

	frame_width = frame->width;
	frame_height = frame->height;
	screen_width = width;
	screen_height = height;

	video_context = (brls::SwitchVideoContext *)brls::Application::getPlatform()->getVideoContext();
	if(!video_context)
		return false;

	device = video_context->getDeko3dDevice();
	queue = video_context->getQueue();

	code_pool.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
	data_pool.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

	draw_cmdbuf = dk::CmdBufMaker{device}.create();
	update_cmdbuf = dk::CmdBufMaker{device}.create();
	draw_cmdmem = data_pool->allocate(STATIC_CMD_SIZE);
	update_cmdmem = data_pool->allocate(UPDATE_CMD_SLICE_SIZE * brls::FRAMEBUFFERS_COUNT, DK_CMDMEM_ALIGNMENT);

	draw_cmdbuf.addMemory(draw_cmdmem.getMemBlock(), draw_cmdmem.getOffset(), draw_cmdmem.getSize());
	image_descriptor_set = video_context->getImageDescriptor();
	if(!image_descriptor_set)
		return false;

	vertex_shader.load(*code_pool, "romfs:/shaders/basic_vsh.dksh");
	fragment_shader.load(*code_pool, "romfs:/shaders/texture_fsh.dksh");

	vertex_buffer = data_pool->allocate(sizeof(QUAD_VERTICES), alignof(Vertex));
	std::memcpy(vertex_buffer.getCpuAddr(), QUAD_VERTICES.data(), vertex_buffer.getSize());

	luma_texture_id = video_context->allocateImageIndex();
	chroma_texture_id = video_context->allocateImageIndex();

	dk::ImageLayoutMaker{device}
		.setType(DkImageType_2D)
		.setFormat(DkImageFormat_R8_Unorm)
		.setDimensions(frame_width, frame_height, 1)
		.setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
		.initialize(luma_layout);

	dk::ImageLayoutMaker{device}
		.setType(DkImageType_2D)
		.setFormat(DkImageFormat_RG8_Unorm)
		.setDimensions(frame_width / 2, frame_height / 2, 1)
		.setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine | DkImageFlags_UsageVideo)
		.initialize(chroma_layout);

	dk::RasterizerState rasterizer_state;
	dk::ColorState color_state;
	dk::ColorWriteState color_write_state;

	draw_cmdbuf.clear();
	draw_cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);
	draw_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, {vertex_shader, fragment_shader});
	draw_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(luma_texture_id, 0));
	draw_cmdbuf.bindTextures(DkStage_Fragment, 1, dkMakeTextureHandle(chroma_texture_id, 0));
	draw_cmdbuf.bindRasterizerState(rasterizer_state);
	draw_cmdbuf.bindColorState(color_state);
	draw_cmdbuf.bindColorWriteState(color_write_state);
	draw_cmdbuf.bindVtxBuffer(0, vertex_buffer.getGpuAddr(), vertex_buffer.getSize());
	draw_cmdbuf.bindVtxAttribState(VERTEX_ATTRIB_STATE);
	draw_cmdbuf.bindVtxBufferState(VERTEX_BUFFER_STATE);
	draw_cmdbuf.draw(DkPrimitive_Quads, QUAD_VERTICES.size(), 1, 0, 0);
	draw_cmdlist = draw_cmdbuf.finishList();

	initialized = true;
	return true;
}

bool DekoVideoRenderer::UpdateFrameMapping(AVFrame *frame)
{
	if(!frame || frame->format != AV_PIX_FMT_NVTEGRA)
		return false;
	if(!frame->buf[0] || !frame->buf[0]->data)
		return false;
	if(!frame->data[0] || !frame->data[1] || frame->data[1] < frame->data[0])
		return false;

	auto *nv_frame = reinterpret_cast<AVNVTegraFrame *>(frame->buf[0]->data);
	if(!nv_frame->map_ref || !nv_frame->map_ref->data)
		return false;

	AVNVTegraMap *map = reinterpret_cast<AVNVTegraMap *>(nv_frame->map_ref->data);
	if(!map)
		return false;

	uint32_t handle = av_nvtegra_map_get_handle(map);
	void *cpu_addr = av_nvtegra_map_get_addr(map);
	uint32_t size = av_nvtegra_map_get_size(map);
	uint32_t chroma_offset = (uint32_t)(frame->data[1] - frame->data[0]);
	if(!cpu_addr || !size || chroma_offset >= size)
		return false;

	int mapping_index = -1;
	for(size_t i = 0; i < frame_mappings.size(); i++)
	{
		const FrameMapping &mapping = frame_mappings[i];
		if(mapping.handle == handle
			&& mapping.cpu_addr == cpu_addr
			&& mapping.size == size
			&& mapping.chroma_offset == chroma_offset)
		{
			mapping_index = (int)i;
			break;
		}
	}

	if(mapping_index < 0)
	{
		FrameMapping mapping;
		mapping.handle = handle;
		mapping.cpu_addr = cpu_addr;
		mapping.size = size;
		mapping.chroma_offset = chroma_offset;
		mapping.memblock = dk::MemBlockMaker{device, size}
			.setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
			.setStorage(cpu_addr)
			.create();

		mapping.luma.initialize(luma_layout, mapping.memblock, 0);
		mapping.chroma.initialize(chroma_layout, mapping.memblock, chroma_offset);
		mapping.luma_desc.initialize(mapping.luma);
		mapping.chroma_desc.initialize(mapping.chroma);

		frame_mappings.emplace_back(std::move(mapping));
		mapping_index = (int)frame_mappings.size() - 1;
	}

	if(mapping_index == current_mapping_index)
		return true;

	update_cmdbuf.clear();
	update_cmdbuf.addMemory(
		update_cmdmem.getMemBlock(),
		update_cmdmem.getOffset() + update_cmdmem_slice * UPDATE_CMD_SLICE_SIZE,
		UPDATE_CMD_SLICE_SIZE);
	update_cmdmem_slice = (update_cmdmem_slice + 1) % brls::FRAMEBUFFERS_COUNT;

	FrameMapping &active = frame_mappings[(size_t)mapping_index];
	image_descriptor_set->update(update_cmdbuf, luma_texture_id, active.luma_desc);
	image_descriptor_set->update(update_cmdbuf, chroma_texture_id, active.chroma_desc);

	queue.submitCommands(update_cmdbuf.finishList());
	current_mapping_index = mapping_index;
	return true;
}

bool DekoVideoRenderer::Draw(AVFrame *frame, int width, int height)
{
	if(!frame || frame->format != AV_PIX_FMT_NVTEGRA)
		return false;

	if(initialized && (frame->width != frame_width || frame->height != frame_height || width != screen_width || height != screen_height))
		Reset();

	if(!EnsureInitialized(frame, width, height))
		return false;

	if(!UpdateFrameMapping(frame))
		return false;
	if(draw_cmdlist)
		queue.submitCommands(draw_cmdlist);

	return true;
}

#else

DekoVideoRenderer::DekoVideoRenderer() = default;
DekoVideoRenderer::~DekoVideoRenderer() = default;
bool DekoVideoRenderer::Draw(AVFrame *, int, int) { return false; }
void DekoVideoRenderer::Reset() {}

#endif
