// dear imgui: Renderer Backend for GameCube GX
// This needs to be used along with the GC Backend

#include "imgui.h"
#include "imgui_impl_gx.h"
#include "os/gx.h"
#include "util/math.h"
#include "util/draw/render.h"
#include <ogc/cache.h>
#include <ogc/gx.h>

struct ImGui_ImplGX_Data {
	GXTexObj font_texture;
	bool initialized = false;
};

static ImGui_ImplGX_Data *ImGui_ImplGX_GetBackendData()
{
	return ImGui::GetCurrentContext() != nullptr
		? (ImGui_ImplGX_Data*)ImGui::GetIO().BackendRendererUserData
		: NULL;
}

// Functions
bool ImGui_ImplGX_Init()
{
	auto &io = ImGui::GetIO();
	IM_ASSERT(io.BackendRendererUserData == nullptr &&
	          "Already initialized a renderer backend!");

	// Setup backend capabilities flags
	auto *bd = IM_NEW(ImGui_ImplGX_Data)();
	io.BackendRendererUserData = bd;
	io.BackendRendererName = "imgui_impl_gx";

	return true;
}

void ImGui_ImplGX_Shutdown()
{
	auto *bd = ImGui_ImplGX_GetBackendData();
	IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
	auto &io = ImGui::GetIO();

	io.BackendRendererName = NULL;
	io.BackendRendererUserData = NULL;
	IM_DELETE(bd);
}

void ImGui_ImplGX_NewFrame()
{
	auto *bd = ImGui_ImplGX_GetBackendData();
	IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplGX_Init()?");

	if (!bd->initialized) {
		ImGui_ImplGX_CreateDeviceObjects();
		bd->initialized = true;
	}
}

static void ImGui_ImplGX_SetupRenderState()
{
	// Set up tev/transforms/scissor/fog
	render_state::get().reset_2d();

	// Disable depth testing
	GX_SetZMode(GX_FALSE, GX_NEVER, GX_FALSE);

	// Set up vertex attributes
	GX_ClearVtxDesc();
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX16);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
}

// GX Render function.
void ImGui_ImplGX_RenderDrawData(ImDrawData* draw_data)
{
	// Setup desired GX state
	ImGui_ImplGX_SetupRenderState();

	// Will project scissor/clipping rectangles into framebuffer space
	const auto clip_off = draw_data->DisplayPos;
	const auto clip_scale = draw_data->FramebufferScale;

	auto &rs = render_state::get();

	// Render command lists
	for (auto n = 0; n < draw_data->CmdListsCount; n++) {
		auto *cmd_list = draw_data->CmdLists[n];
		auto *vtx_buffer = cmd_list->VtxBuffer.Data;
		auto *idx_buffer = cmd_list->IdxBuffer.Data;

		// Write vertex data to main memory to be read by GX unit
		DCStoreRange(vtx_buffer, cmd_list->VtxBuffer.size_in_bytes());

		GX_SetArray(GX_VA_POS,  &vtx_buffer->pos, sizeof(ImDrawVert));
		GX_SetArray(GX_VA_CLR0, &vtx_buffer->col, sizeof(ImDrawVert));
		GX_SetArray(GX_VA_TEX0, &vtx_buffer->uv,  sizeof(ImDrawVert));

		for (auto cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
			const auto *pcmd = &cmd_list->CmdBuffer[cmd_i];

			if (pcmd->UserCallback) {
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used
				// by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
					ImGui_ImplGX_SetupRenderState();
				else
					pcmd->UserCallback(cmd_list, pcmd);

				continue;
			}

			// Project scissor/clipping rectangles into framebuffer space
			const auto clip_min = vec2((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
			                           (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
			const auto clip_max = vec2((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
			                           (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

			if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
				continue;

			// Apply scissor/clipping rectangle
			rs.set_scissor((u32)clip_min.x,
			               (u32)clip_min.y,
			               (u32)(clip_max.x - clip_min.x),
			               (u32)(clip_max.y - clip_min.y));

			// Bind texture
			rs.load_tex_obj((GXTexObj*)pcmd->GetTexID());

			// Draw
			const auto vertex_count = (u16)pcmd->ElemCount;
			GX_Begin(GX_TRIANGLES, GX_VTXFMT0, vertex_count);

			for (u16 vertex = 0; vertex < vertex_count; vertex++) {
				const auto index = idx_buffer[pcmd->IdxOffset + vertex];
				gx_fifo->write(index, index, index);
			}
		}
	}

	// Restore cull mode to expected value
	GX_SetCullMode(GX_CULL_BACK);
}

static void *ImGui_ImplGX_ConvertAlpha8ToI8(unsigned char *in, int real_width, int real_height)
{
	constexpr auto block_width = 8;
	constexpr auto block_height = 4;
	constexpr auto block_size = block_width * block_height;
	const auto width = align_up(real_width, block_width);
	const auto height = align_up(real_height, block_height);
	const auto size = width * height;
	const auto block_num_x = width / block_width;

	auto *out = (unsigned char*)IM_ALLOC(size);

	for (auto pixel = 0; pixel < size; pixel++) {
		const auto block_index = pixel / block_size;
		const auto block_x = (block_index % block_num_x) * block_width;
		const auto block_y = (block_index / block_num_x) * block_height;
		const auto offset = pixel % block_size;
		const auto offset_x = offset % block_width;
		const auto offset_y = offset / block_width;
		const auto x = block_x + offset_x;
		const auto y = block_y + offset_y;

		if (x < real_width && y < real_height)
			out[pixel] = in[y * real_width + x];
		else
			out[pixel] = 0;
	}

	DCStoreRange(out, size);
	return out;
}

bool ImGui_ImplGX_CreateFontsTexture()
{
	// Build texture atlas
	auto &io = ImGui::GetIO();
	auto *bd = ImGui_ImplGX_GetBackendData();

	unsigned char *pixels;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

	auto *converted = ImGui_ImplGX_ConvertAlpha8ToI8(pixels, width, height);

	GX_InitTexObj(&bd->font_texture, converted, (u16)width, (u16)height,
	              GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);

	// Store our identifier
	io.Fonts->SetTexID(&bd->font_texture);

	return true;
}

bool ImGui_ImplGX_CreateDeviceObjects()
{
	return ImGui_ImplGX_CreateFontsTexture();
}
