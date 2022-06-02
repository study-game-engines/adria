#include "ToneMapPass.h"
#include "../GlobalBlackboardData.h"
#include "../RootSigPSOManager.h"
#include "../../RenderGraph/RenderGraph.h"
#include "pix3.h"

namespace adria
{

	ToneMapPassData const& ToneMapPass::AddPass(RenderGraph& rg, RGTextureRef hdr_texture, std::optional<RGTextureRef> ldr_texture)
	{
		GlobalBlackboardData const& global_data = rg.GetBlackboard().GetChecked<GlobalBlackboardData>();
		ERGPassFlags flags = !ldr_texture.has_value() ? ERGPassFlags::ForceNoCull | ERGPassFlags::SkipAutoRenderPass : ERGPassFlags::None;

		return rg.AddPass<ToneMapPassData>("ToneMap Pass",
			[=](ToneMapPassData& data, RenderGraphBuilder& builder)
			{
				data.hdr_srv = builder.CreateSRV(builder.Read(hdr_texture, ReadAccess_PixelShader));
				if (ldr_texture.has_value())
					data.target = builder.RenderTarget(builder.CreateRTV(*ldr_texture), ERGLoadStoreAccessOp::Discard_Preserve);
				else data.target = RGTextureRef();
				builder.SetViewport(width, height);
			},
			[=](ToneMapPassData const& data, RenderGraphResources& resources, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				if (!data.target.IsValid())
				{
					D3D12_VIEWPORT vp{};
					vp.Width = (float32)width;
					vp.Height = (float32)height;
					vp.MinDepth = 0.0f;
					vp.MaxDepth = 1.0f;
					vp.TopLeftX = 0;
					vp.TopLeftY = 0;
					cmd_list->RSSetViewports(1, &vp);
					D3D12_RECT rect{};
					rect.bottom = (int64)height;
					rect.left = 0;
					rect.right = (int64)width;
					rect.top = 0;
					cmd_list->RSSetScissorRects(1, &rect);
					gfx->SetBackbuffer(cmd_list);
				}

				ID3D12Device* device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();

				cmd_list->SetGraphicsRootSignature(RootSigPSOManager::GetRootSignature(ERootSignature::ToneMap));
				cmd_list->SetPipelineState(RootSigPSOManager::GetPipelineState(EPipelineStateObject::ToneMap));

				cmd_list->SetGraphicsRootConstantBufferView(0, global_data.postprocess_cbuffer_address);

				OffsetType descriptor_index = descriptor_allocator->Allocate();
				D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor = resources.GetSRV(data.hdr_srv);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(descriptor_index), cpu_descriptor,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetGraphicsRootDescriptorTable(1, descriptor_allocator->GetHandle(descriptor_index));
				cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				cmd_list->DrawInstanced(4, 1, 0, 0);
			}, ERGPassType::Graphics, flags);
	}

	void ToneMapPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
	}

}

