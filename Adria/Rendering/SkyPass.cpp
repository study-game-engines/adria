#include "SkyPass.h"
#include "ShaderStructs.h"
#include "Components.h"
#include "Enums.h"
#include "BlackboardData.h"
#include "PSOCache.h"

#include "Graphics/GfxRingDescriptorAllocator.h"
#include "Graphics/GfxLinearDynamicAllocator.h"
#include "RenderGraph/RenderGraph.h"
#include "Editor/GUICommand.h"
#include "entt/entity/registry.hpp"

using namespace DirectX;

namespace adria
{
	static constexpr uint32 SKYCUBE_SIZE = 128;

	SkyPass::SkyPass(entt::registry& reg, uint32 w, uint32 h)
		: reg(reg), width(w), height(h), sky_type(SkyType::MinimalAtmosphere)
	{}

	void SkyPass::AddComputeSkyPass(RenderGraph& rg, DirectX::XMFLOAT3 const& dir)
	{
		if (sky_type == SkyType::Skybox) return;

		FrameBlackboardData const& global_data = rg.GetBlackboard().GetChecked<FrameBlackboardData>();
		struct ComputeSkyPassData
		{
			RGTextureReadWriteId sky_uav;
		};

		rg.ImportTexture(RG_RES_NAME(Sky), sky_texture.get());
		rg.AddPass<ComputeSkyPassData>("Compute Sky Pass",
			[=](ComputeSkyPassData& data, RenderGraphBuilder& builder)
			{
				data.sky_uav = builder.WriteTexture(RG_RES_NAME(Sky));
			},
			[&](ComputeSkyPassData const& data, RenderGraphContext& context, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();
				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);

				GfxDescriptor sky = gfx->AllocateDescriptorsGPU(1);
				gfx->CopyDescriptors(1, sky, context.GetReadWriteTexture(data.sky_uav));
				cmd_list->SetRootConstant(1, sky.GetIndex());

				switch (sky_type)
				{
				case SkyType::MinimalAtmosphere:
				{
					cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::MinimalAtmosphereSky));
					break;
				}
				case SkyType::HosekWilkie:
				{
					cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::HosekWilkieSky));
					SkyParameters parameters = CalculateSkyParameters(turbidity, ground_albedo, dir);
					struct HosekWilkieConstants
					{
						DECLSPEC_ALIGN(16) XMFLOAT3 A;
						DECLSPEC_ALIGN(16) XMFLOAT3 B;
						DECLSPEC_ALIGN(16) XMFLOAT3 C;
						DECLSPEC_ALIGN(16) XMFLOAT3 D;
						DECLSPEC_ALIGN(16) XMFLOAT3 E;
						DECLSPEC_ALIGN(16) XMFLOAT3 F;
						DECLSPEC_ALIGN(16) XMFLOAT3 G;
						DECLSPEC_ALIGN(16) XMFLOAT3 H;
						DECLSPEC_ALIGN(16) XMFLOAT3 I;
						DECLSPEC_ALIGN(16) XMFLOAT3 Z;
					} constants =
					{
							.A = parameters[ESkyParam_A],
							.B = parameters[ESkyParam_B],
							.C = parameters[ESkyParam_C],
							.D = parameters[ESkyParam_D],
							.E = parameters[ESkyParam_E],
							.F = parameters[ESkyParam_F],
							.G = parameters[ESkyParam_G],
							.H = parameters[ESkyParam_H],
							.I = parameters[ESkyParam_I],
							.Z = parameters[ESkyParam_Z],
					};
					cmd_list->SetRootCBV(3, constants);
					break;
				}
				case SkyType::Skybox:
				default:
					ADRIA_ASSERT(false);
				}
				cmd_list->Dispatch(SKYCUBE_SIZE / 16, SKYCUBE_SIZE / 16, 6);

			}, RGPassType::Compute, RGPassFlags::ForceNoCull);
	}

	void SkyPass::AddDrawSkyPass(RenderGraph& rg)
	{
		FrameBlackboardData const& global_data = rg.GetBlackboard().GetChecked<FrameBlackboardData>();
		rg.AddPass<void>("Draw Sky Pass",
			[=](RenderGraphBuilder& builder)
			{
				builder.WriteRenderTarget(RG_RES_NAME(HDR_RenderTarget), RGLoadStoreAccessOp::Preserve_Preserve);
				builder.ReadDepthStencil(RG_RES_NAME(DepthStencil), RGLoadStoreAccessOp::Preserve_Preserve);
				builder.SetViewport(width, height);
			},
			[=](RenderGraphContext& context, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::Sky));
				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);
				cmd_list->SetTopology(GfxPrimitiveTopology::TriangleList);
				BindVertexBuffer(cmd_list->GetNative(), cube_vb.get());
				BindIndexBuffer(cmd_list->GetNative(), cube_ib.get());
				cmd_list->DrawIndexed(cube_ib->GetCount());
			}, RGPassType::Graphics, RGPassFlags::None);

		AddGUI([&]()
			{
				if (ImGui::TreeNodeEx("Sky", ImGuiTreeNodeFlags_OpenOnDoubleClick))
				{
					static int current_sky_type = 0;
					const char* sky_types[] = { "Skybox", "Minimal Atmosphere", "Hosek-Wilkie" };
					const char* combo_label = sky_types[current_sky_type];
					if (ImGui::BeginCombo("Sky Type", combo_label, 0))
					{
						for (int n = 0; n < IM_ARRAYSIZE(sky_types); n++)
						{
							const bool is_selected = (current_sky_type == n);
							if (ImGui::Selectable(sky_types[n], is_selected)) current_sky_type = n;
							if (is_selected) ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					if (current_sky_type == 0) sky_type = SkyType::Skybox;
					else if (current_sky_type == 1) sky_type = SkyType::MinimalAtmosphere;
					else if (current_sky_type == 2)
					{
						sky_type = SkyType::HosekWilkie;
						ImGui::SliderFloat("Turbidity", &turbidity, 2.0f, 30.0f);
						ImGui::SliderFloat("Ground Albedo", &ground_albedo, 0.0f, 1.0f);
					}
					ImGui::TreePop();
					ImGui::Separator();
				}
			}
		);
	}

	void SkyPass::OnSceneInitialized(GfxDevice* _gfx)
	{
		gfx = _gfx;
		CreateCubeBuffers(gfx);
	}

	void SkyPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
	}

	int32 SkyPass::GetSkyIndex() const
	{
		if (sky_type == SkyType::Skybox)
		{
			auto skybox_view = reg.view<Skybox>();
			for (auto e : skybox_view)
			{
				auto const& skybox = skybox_view.get<Skybox>(e);
				if (!skybox.active) continue;

				ADRIA_ASSERT(skybox.cubemap_texture != INVALID_TEXTURE_HANDLE);
				return (int32)skybox.cubemap_texture;
			}
		}

		GfxDescriptor sky_srv_gpu = gfx->AllocateDescriptorsGPU();
		gfx->CopyDescriptors(1, sky_srv_gpu, sky_texture_srv);
		return (int32)sky_srv_gpu.GetIndex();
	}

	void SkyPass::CreateCubeBuffers(GfxDevice* gfx)
	{
		GfxTextureDesc sky_desc{};
		sky_desc.type = GfxTextureType_2D;
		sky_desc.width = SKYCUBE_SIZE;
		sky_desc.height = SKYCUBE_SIZE;
		sky_desc.misc_flags = GfxTextureMiscFlag::TextureCube;
		sky_desc.array_size = 6;
		sky_desc.format = GfxFormat::R16G16B16A16_FLOAT;
		sky_desc.bind_flags = GfxBindFlag::ShaderResource | GfxBindFlag::UnorderedAccess;
		sky_desc.initial_state = GfxResourceState::UnorderedAccess;
		sky_texture = std::make_unique<GfxTexture>(gfx, sky_desc);

		GfxTextureSubresourceDesc sky_srv_desc{};
		sky_srv_desc.first_slice = 0;
		sky_srv_desc.slice_count = 6;
		sky_texture_srv = gfx->CreateTextureSRV(sky_texture.get(), &sky_srv_desc);

		SimpleVertex const cube_vertices[8] =
		{
			XMFLOAT3{ -0.5f, -0.5f,  0.5f },
			XMFLOAT3{  0.5f, -0.5f,  0.5f },
			XMFLOAT3{  0.5f,  0.5f,  0.5f },
			XMFLOAT3{ -0.5f,  0.5f,  0.5f },
			XMFLOAT3{ -0.5f, -0.5f, -0.5f },
			XMFLOAT3{  0.5f, -0.5f, -0.5f },
			XMFLOAT3{  0.5f,  0.5f, -0.5f },
			XMFLOAT3{ -0.5f,  0.5f, -0.5f }
		};

		uint16 const cube_indices[36] =
		{
			// front
			0, 1, 2,
			2, 3, 0,
			// right
			1, 5, 6,
			6, 2, 1,
			// back
			7, 6, 5,
			5, 4, 7,
			// left
			4, 0, 3,
			3, 7, 4,
			// bottom
			4, 5, 1,
			1, 0, 4,
			// top
			3, 2, 6,
			6, 7, 3
		};

		GfxBufferDesc vb_desc{};
		vb_desc.bind_flags = GfxBindFlag::None;
		vb_desc.size = sizeof(cube_vertices);
		vb_desc.stride = sizeof(SimpleVertex);
		cube_vb = std::make_unique<GfxBuffer>(gfx, vb_desc, cube_vertices);

		GfxBufferDesc ib_desc{};
		ib_desc.bind_flags = GfxBindFlag::None;
		ib_desc.format = GfxFormat::R16_UINT;
		ib_desc.stride = sizeof(uint16);
		ib_desc.size = sizeof(cube_indices);
		cube_ib = std::make_unique<GfxBuffer>(gfx, ib_desc, cube_indices);
	}

}