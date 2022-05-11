// dear imgui: Renderer Backend for GameCube GX
// This needs to be used along with the GC Backend

#include "imgui.h"
#include "imgui_impl_gx.h"
#include "os/gx.h"
#include "util/draw/render.h"
#include <ogc/gx.h>
#include <stdint.h>

struct ImGui_ImplGX_Data {
	GXTexObj font_texture;
	bool initialized = false;
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplGX_Data* ImGui_ImplGX_GetBackendData()
{
	if (ImGui::GetCurrentContext() == nullptr)
		return nullptr;

	return (ImGui_ImplGX_Data*)ImGui::GetIO().BackendRendererUserData;
}

// Functions
bool ImGui_ImplGX_Init()
{
	auto &io = ImGui::GetIO();
	IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

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
	// Set up tev/transforms/scissor
	render_state::get().reset_2d();

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
		GX_SetArray(GX_VA_POS,  &vtx_buffer->pos, sizeof(ImDrawVert));
		GX_SetArray(GX_VA_CLR0, &vtx_buffer->col, sizeof(ImDrawVert));
		GX_SetArray(GX_VA_TEX0, &vtx_buffer->uv,  sizeof(ImDrawVert));

		for (auto cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
			const auto *pcmd = &cmd_list->CmdBuffer[cmd_i];

			if (pcmd->UserCallback) {
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
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
			               (u32)clip_max.y,
			               (u32)(clip_max.x - clip_min.x),
			               (u32)(clip_max.y - clip_min.y));

			// Bind texture
			rs.load_tex_obj((GXTexObj*)pcmd->GetTexID());

			for (auto elem = 0u; elem < pcmd->ElemCount; elem++) {
				const auto index = idx_buffer[pcmd->IdxOffset + elem];
				gx_fifo->write(index, index, index);
			}
		}
	}
}

bool ImGui_ImplGX_CreateFontsTexture()
{
	// Build texture atlas
	auto &io = ImGui::GetIO();
	auto *bd = ImGui_ImplGX_GetBackendData();

	unsigned char *pixels;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

	GX_InitTexObj(&bd->font_texture, pixels, (u16)width, (u16)height,
	              GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);

	// Store our identifier
	io.Fonts->SetTexID(&bd->font_texture);

	return true;
}

bool ImGui_ImplGX_CreateDeviceObjects()
{
	return ImGui_ImplGX_CreateFontsTexture();
}
