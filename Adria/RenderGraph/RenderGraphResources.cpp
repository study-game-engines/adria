#include "RenderGraphResources.h"
#include "RenderGraph.h"

namespace adria
{

	RenderGraphResources::RenderGraphResources(RenderGraph& rg, RenderGraphPassBase& rg_pass) : rg(rg), rg_pass(rg_pass)
	{}

	RGBlackboard& RenderGraphResources::GetBlackboard()
	{
		return rg.GetBlackboard();
	}

	Texture const& RenderGraphResources::GetResource(RGTextureCopySrcId res_id) const
	{
		return rg.GetResource(res_id);
	}

	Texture const& RenderGraphResources::GetResource(RGTextureCopyDstId res_id) const
	{
		return rg.GetResource(res_id);
	}

	RGDescriptor RenderGraphResources::GetDescriptor(RGTextureReadOnlyId res_id) const
	{
		return rg.GetDescriptor(res_id);
	}

	RGDescriptor RenderGraphResources::GetDescriptor(RGTextureReadWriteId res_id) const
	{
		return rg.GetDescriptor(res_id);
	}

	RGDescriptor RenderGraphResources::GetDescriptor(RGRenderTargetId res_id) const
	{
		return rg.GetDescriptor(res_id);
	}

	RGDescriptor RenderGraphResources::GetDescriptor(RGDepthStencilId res_id) const
	{
		return rg.GetDescriptor(res_id);
	}

}

