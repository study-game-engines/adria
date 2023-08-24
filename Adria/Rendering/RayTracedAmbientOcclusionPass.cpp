#include "RayTracedAmbientOcclusionPass.h"
#include "BlackboardData.h"
#include "ShaderCache.h"
#include "PSOCache.h"

#include "Graphics/GfxShader.h"
#include "Graphics/GfxRingDescriptorAllocator.h"
#include "RenderGraph/RenderGraph.h"
#include "Editor/GUICommand.h"

namespace adria
{

	RayTracedAmbientOcclusionPass::RayTracedAmbientOcclusionPass(GfxDevice* gfx, uint32 width, uint32 height)
		: gfx(gfx), width(width), height(height), blur_pass(width, height)
	{
		is_supported = gfx->GetCapabilities().SupportsRayTracing();
		if (IsSupported())
		{
			CreateStateObject();
			ShaderCache::GetLibraryRecompiledEvent().AddMember(&RayTracedAmbientOcclusionPass::OnLibraryRecompiled, *this);
		}
	}

	void RayTracedAmbientOcclusionPass::AddPass(RenderGraph& rg)
	{
		if (!IsSupported()) return;

		FrameBlackboardData const& global_data = rg.GetBlackboard().Get<FrameBlackboardData>();
		struct RayTracedAmbientOcclusionPassData
		{
			RGTextureReadOnlyId depth;
			RGTextureReadOnlyId normal;
			RGTextureReadWriteId output;
		};

		rg.AddPass<RayTracedAmbientOcclusionPassData>("Ray Traced Ambient Occlusion Pass",
			[=](RayTracedAmbientOcclusionPassData& data, RGBuilder& builder)
			{
				RGTextureDesc desc{};
				desc.width = width;
				desc.height = height;
				desc.format = GfxFormat::R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME(RTAO_Output), desc);

				data.output = builder.WriteTexture(RG_RES_NAME(RTAO_Output));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
				data.normal = builder.ReadTexture(RG_RES_NAME(GBufferNormal), ReadAccess_NonPixelShader);
			},
			[=](RayTracedAmbientOcclusionPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				uint32 i = gfx->AllocateDescriptorsGPU(3).GetIndex();
				gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + 0), ctx.GetReadOnlyTexture(data.depth));
				gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + 1), ctx.GetReadOnlyTexture(data.normal));
				gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + 2), ctx.GetReadWriteTexture(data.output));

				struct RayTracedAmbientOcclusionConstants
				{
					uint32  depth_idx;
					uint32  gbuf_normals_idx;
					uint32  output_idx;
					float   ao_radius;
					float   ao_power;
				} constants =
				{
					.depth_idx = i + 0, .gbuf_normals_idx = i + 1, .output_idx = i + 2,
					.ao_radius = params.radius, .ao_power = pow(2.f, params.power_log)
				};

				auto& table = cmd_list->SetStateObject(ray_traced_ambient_occlusion.Get());
				table.SetRayGenShader("RTAO_RayGen");
				table.AddMissShader("RTAO_Miss", 0);
				table.AddHitGroup("RTAOAnyHitGroup", 0);

				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);
				cmd_list->DispatchRays(width, height);
			}, RGPassType::Compute, RGPassFlags::None);

		struct RTAOFilterPassData
		{
			RGTextureReadOnlyId depth;
			RGTextureReadOnlyId input;
			RGTextureReadWriteId output;
		};

		rg.AddPass<RTAOFilterPassData>("RTAO Filter Pass",
			[=](RTAOFilterPassData& data, RGBuilder& builder)
			{
				RGTextureDesc desc{};
				desc.width = width;
				desc.height = height;
				desc.format = GfxFormat::R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME(AmbientOcclusion), desc);

				data.output = builder.WriteTexture(RG_RES_NAME(AmbientOcclusion));
				data.input = builder.ReadTexture(RG_RES_NAME(RTAO_Output), ReadAccess_NonPixelShader);
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
			},
			[=](RTAOFilterPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				uint32 i = gfx->AllocateDescriptorsGPU(3).GetIndex();
				gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + 0), ctx.GetReadOnlyTexture(data.depth));
				gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + 1), ctx.GetReadOnlyTexture(data.input));
				gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + 2), ctx.GetReadWriteTexture(data.output));

				struct RTAOFilterIndices
				{
					uint32  depth_idx;
					uint32  input_idx;
					uint32  output_idx;
				} indices =
				{
					.depth_idx = i + 0, .input_idx = i + 1, .output_idx = i + 2
				};

				float distance_kernel[6];
				for (size_t i = 0; i < 6; ++i)
				{
					distance_kernel[i] = (float)exp(-float(i * i) / (2.f * params.filter_distance_sigma * params.filter_distance_sigma));
				}

				struct RTAOFilterConstants
				{
					float filter_width;
					float filter_height;
					float filter_distance_sigma;
					float filter_depth_sigma;
					float filter_dist_kernel0;
					float filter_dist_kernel1;
					float filter_dist_kernel2;
					float filter_dist_kernel3;
					float filter_dist_kernel4;
					float filter_dist_kernel5;
				} constants =
				{
					.filter_width = (float)width, .filter_height = (float)height, .filter_distance_sigma = params.filter_distance_sigma, .filter_depth_sigma = params.filter_depth_sigma,
					.filter_dist_kernel0 = distance_kernel[0], .filter_dist_kernel1 = distance_kernel[1],
					.filter_dist_kernel2 = distance_kernel[2], .filter_dist_kernel3 = distance_kernel[3],
					.filter_dist_kernel4 = distance_kernel[4], .filter_dist_kernel5 = distance_kernel[5],
				};

				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::RTAOFilter));

				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, indices);
				cmd_list->SetRootCBV(2, constants);
				cmd_list->Dispatch((uint32)std::ceil(width / 32.0f), (uint32)std::ceil(height / 32.0f), 1);
			}, RGPassType::Compute, RGPassFlags::None);

		GUI_RunCommand([&]()
			{
				if (ImGui::TreeNodeEx("RTAO", ImGuiTreeNodeFlags_OpenOnDoubleClick))
				{
					ImGui::SliderFloat("Radius", &params.radius, 1.0f, 32.0f);
					ImGui::SliderFloat("Power (log2)", &params.power_log, -10.0f, 10.0f);
					ImGui::TreePop();
					ImGui::Separator();
				}
			}
		);
	}

	void RayTracedAmbientOcclusionPass::OnResize(uint32 w, uint32 h)
	{
		if (!IsSupported()) return;

		width = w, height = h;
		blur_pass.OnResize(w, h);
	}

	bool RayTracedAmbientOcclusionPass::IsSupported() const
	{
		return is_supported;
	}

	void RayTracedAmbientOcclusionPass::CreateStateObject()
	{
		ID3D12Device5* device = gfx->GetDevice();
		GfxShader const& rtao_blob = ShaderCache::GetShader(LIB_AmbientOcclusion);

		GfxStateObjectBuilder rtao_state_object_builder(5);
		{
			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc{};
			dxil_lib_desc.DXILLibrary.BytecodeLength = rtao_blob.GetLength();
			dxil_lib_desc.DXILLibrary.pShaderBytecode = rtao_blob.GetPointer();
			dxil_lib_desc.NumExports = 0;
			dxil_lib_desc.pExports = nullptr;
			rtao_state_object_builder.AddSubObject(dxil_lib_desc);

			D3D12_RAYTRACING_SHADER_CONFIG rtao_shader_config{};
			rtao_shader_config.MaxPayloadSizeInBytes = 4;
			rtao_shader_config.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
			rtao_state_object_builder.AddSubObject(rtao_shader_config);

			D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig{};
			global_root_sig.pGlobalRootSignature = gfx->GetCommonRootSignature();
			rtao_state_object_builder.AddSubObject(global_root_sig);

			// Add a state subobject for the ray tracing pipeline config
			D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
			pipeline_config.MaxTraceRecursionDepth = 1;
			rtao_state_object_builder.AddSubObject(pipeline_config);

			D3D12_HIT_GROUP_DESC anyhit_group{};
			anyhit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
			anyhit_group.AnyHitShaderImport = L"RTAO_AnyHit";
			anyhit_group.HitGroupExport = L"RTAOAnyHitGroup";
			rtao_state_object_builder.AddSubObject(anyhit_group);

			ray_traced_ambient_occlusion.Attach(rtao_state_object_builder.CreateStateObject(device));
		}

	}

	void RayTracedAmbientOcclusionPass::OnLibraryRecompiled(GfxShaderID shader)
	{
		if (shader == LIB_AmbientOcclusion) CreateStateObject();
	}

}


