#pragma once
#include "Enums.h"
#include "BlurPass.h"
#include "../Graphics/GfxRayTracingUtil.h"
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceName.h"

namespace adria
{
	class RenderGraph;
	class GfxDevice;

	class RayTracedAmbientOcclusionPass
	{
	public:
		RayTracedAmbientOcclusionPass(GfxDevice* gfx, uint32 width, uint32 height);
		void AddPass(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h);
		bool IsSupported() const;

	private:
		GfxDevice* gfx;
		BlurPass blur_pass;
		ArcPtr<ID3D12StateObject> ray_traced_ambient_occlusion;
		uint32 width, height;
		bool is_supported;
		float ao_radius = 2.0f;
	private:
		void CreateStateObject();
		void OnLibraryRecompiled(GfxShaderID shader);
	};
}