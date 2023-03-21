#include "MotionBlurPass.h"
#include "ConstantBuffers.h"
#include "Components.h"
#include "BlackboardData.h"
#include "PSOCache.h" 

#include "../Graphics/RingGPUDescriptorAllocator.h"
#include "../RenderGraph/RenderGraph.h"

namespace adria
{

	MotionBlurPass::MotionBlurPass(uint32 w, uint32 h) : width(w), height(h) {}

	RGResourceName MotionBlurPass::AddPass(RenderGraph& rg, RGResourceName input)
	{
		FrameBlackboardData const& global_data = rg.GetBlackboard().GetChecked<FrameBlackboardData>();
		RGResourceName last_resource = input;
		struct MotionBlurPassData
		{
			RGTextureReadOnlyId input;
			RGTextureReadOnlyId velocity;
			RGTextureReadWriteId output;
		};
		rg.AddPass<MotionBlurPassData>("Motion Blur Pass",
			[=](MotionBlurPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc motion_blur_desc{};
				motion_blur_desc.width = width;
				motion_blur_desc.height = height;
				motion_blur_desc.format = GfxFormat::R16G16B16A16_FLOAT;

				builder.DeclareTexture(RG_RES_NAME(MotionBlurOutput), motion_blur_desc);
				data.output = builder.WriteTexture(RG_RES_NAME(MotionBlurOutput));
				data.input = builder.ReadTexture(last_resource, ReadAccess_NonPixelShader);
				data.velocity = builder.ReadTexture(RG_RES_NAME(VelocityBuffer), ReadAccess_NonPixelShader);
			},
			[=](MotionBlurPassData const& data, RenderGraphContext& ctx, GfxDevice* gfx, CommandList* cmd_list)
			{
				ID3D12Device* device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetDescriptorAllocator();

				uint32 i = (uint32)descriptor_allocator->AllocateRange(3);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 0), ctx.GetReadOnlyTexture(data.input), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 1), ctx.GetReadOnlyTexture(data.velocity), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 2), ctx.GetReadWriteTexture(data.output), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				
				struct MotionBlurConstants
				{
					uint32 scene_idx;
					uint32 velocity_idx;
					uint32 output_idx;
				} constants =
				{
					.scene_idx = i, .velocity_idx = i + 1, .output_idx = i + 2
				};

				
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::MotionBlur));
				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRoot32BitConstants(1, 3, &constants, 0);
				cmd_list->Dispatch((UINT)std::ceil(width / 16.0f), (UINT)std::ceil(height / 16.0f), 1);
			}, RGPassType::Compute, RGPassFlags::None);

		return RG_RES_NAME(MotionBlurOutput);
	}

	void MotionBlurPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
	}

}

