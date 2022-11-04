#include "RayTracer.h"
#include "Components.h"
#include "BlackboardData.h"
#include "ShaderCache.h"
#include "RootSignatureCache.h"
#include "entt/entity/registry.hpp"
#include "../RenderGraph/RenderGraph.h"
#include "../Editor/GUICommand.h"
#include "../Graphics/Shader.h"
#include "../Logging/Logger.h"

using namespace DirectX;

namespace adria
{

	RayTracer::RayTracer(entt::registry& reg, GraphicsDevice* gfx, uint32 width, uint32 height)
		: reg(reg), gfx(gfx), width(width), height(height), accel_structure(gfx), blur_pass(width, height)
	{
		ID3D12Device* device = gfx->GetDevice();
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5{};
		HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
		ray_tracing_tier = features5.RaytracingTier;
		if (FAILED(hr) || ray_tracing_tier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		{
			ADRIA_LOG(INFO, "Ray Tracing is not supported! All Ray Tracing calls will be silently ignored!");
			return;
		}
		else if (ray_tracing_tier < D3D12_RAYTRACING_TIER_1_1)
		{
			ADRIA_LOG(INFO, "Ray Tracing Tier is less than Tier 1.1!"
				"Calls to Ray Traced Reflections will be silently ignored!");
		}
		OnResize(width, height);
		CreateStateObjects();
		ShaderCache::GetLibraryRecompiledEvent().AddMember(&RayTracer::OnLibraryRecompiled, *this);
	}

	bool RayTracer::IsSupported() const
	{
		return ray_tracing_tier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	}
	bool RayTracer::IsFeatureSupported(ERayTracingFeature feature) const
	{
		switch (feature)
		{
		case ERayTracingFeature::Shadows:
		case ERayTracingFeature::AmbientOcclusion:
			return ray_tracing_tier >= D3D12_RAYTRACING_TIER_1_0;
		case ERayTracingFeature::Reflections:
		case ERayTracingFeature::PathTracing:
			return ray_tracing_tier >= D3D12_RAYTRACING_TIER_1_1;
		default:
			return false;
		}
	}
	void RayTracer::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
		blur_pass.OnResize(w, h);

#ifdef _DEBUG
		TextureDesc debug_desc{};
		debug_desc.width = width;
		debug_desc.height = height;
		debug_desc.format = EFormat::R8_UNORM;
		debug_desc.bind_flags = EBindFlag::ShaderResource;
		debug_desc.initial_state = EResourceState::CopyDest;
		debug_desc.clear_value = ClearValue(0.0f, 0.0f, 0.0f, 0.0f);

		rtao_debug_texture = std::make_unique<Texture>(gfx, debug_desc);
		rtao_debug_texture->CreateSRV();

		rts_debug_texture = std::make_unique<Texture>(gfx, debug_desc);
		rts_debug_texture->CreateSRV();

		debug_desc.format = EFormat::R8G8B8A8_UNORM;
		rtr_debug_texture = std::make_unique<Texture>(gfx, debug_desc);
		rtr_debug_texture->CreateSRV();
#endif
	}
	void RayTracer::OnSceneInitialized()
	{
		if (!IsSupported()) return;

		auto ray_tracing_view = reg.view<Mesh, Transform, Material, RayTracing>();
		std::vector<GeoInfo> geo_info{};
		for (auto entity : ray_tracing_view)
		{
			auto const& [mesh, transform, material, ray_tracing] = ray_tracing_view.get<Mesh, Transform, Material, RayTracing>(entity);
			geo_info.push_back(GeoInfo{
				.vertex_offset = ray_tracing.vertex_offset,
				.index_offset = ray_tracing.index_offset,
				.albedo_idx = (int32)material.albedo_texture,
				.normal_idx = (int32)material.normal_texture,
				.metallic_roughness_idx = (int32)material.metallic_roughness_texture,
				.emissive_idx = (int32)material.emissive_texture,
				});
			accel_structure.AddInstance(mesh, transform);
		}
		accel_structure.Build();

		BufferDesc desc = StructuredBufferDesc<GeoInfo>(geo_info.size(), false);
		geo_buffer = std::make_unique<Buffer>(gfx, desc, geo_info.data());

		ID3D12Device5* device = gfx->GetDevice();
		if (RayTracing::rt_vertices.empty() || RayTracing::rt_indices.empty())
		{
			ADRIA_LOG(WARNING, "Ray tracing buffers are empty. This is expected if the meshes are loaded with ray-tracing support off");
			return;
		}

		BufferDesc vb_desc{};
		vb_desc.bind_flags = EBindFlag::ShaderResource;
		vb_desc.misc_flags = EBufferMiscFlag::VertexBuffer | EBufferMiscFlag::BufferStructured;
		vb_desc.size = RayTracing::rt_vertices.size() * sizeof(CompleteVertex);
		vb_desc.stride = sizeof(CompleteVertex);

		BufferDesc ib_desc{};
		ib_desc.bind_flags = EBindFlag::ShaderResource;
		ib_desc.misc_flags = EBufferMiscFlag::IndexBuffer | EBufferMiscFlag::BufferStructured;
		ib_desc.size = RayTracing::rt_indices.size() * sizeof(uint32);
		ib_desc.stride = sizeof(uint32);
		ib_desc.format = EFormat::R32_UINT;

		global_vb = std::make_unique<Buffer>(gfx, vb_desc, RayTracing::rt_vertices.data());
		global_ib = std::make_unique<Buffer>(gfx, ib_desc, RayTracing::rt_indices.data());
	}

	void RayTracer::AddRayTracedShadowsPass(RenderGraph& rg, Light const& light, size_t light_id)
	{
		if (!IsFeatureSupported(ERayTracingFeature::Shadows)) return;

		GlobalBlackboardData const& global_data = rg.GetBlackboard().GetChecked<GlobalBlackboardData>();
		struct RayTracedShadowsPassData
		{
			RGTextureReadOnlyId depth;
			RGTextureReadWriteId shadow;
		};

		rg.AddPass<RayTracedShadowsPassData>("Ray Traced Shadows Pass",
			[=](RayTracedShadowsPassData& data, RGBuilder& builder) 
			{
				RGTextureDesc desc{};
				desc.width = width;
				desc.height = height;
				desc.format = EFormat::R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME_IDX(RayTracedShadows, light_id), desc);

				data.shadow = builder.WriteTexture(RG_RES_NAME_IDX(RayTracedShadows, light_id));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
			},
			[=](RayTracedShadowsPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
				auto dynamic_allocator = gfx->GetDynamicAllocator();

				uint32 i = (uint32)descriptor_allocator->AllocateRange(3);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 0), accel_structure.GetTLAS()->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 1), ctx.GetReadOnlyTexture(data.depth), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 2), ctx.GetReadWriteTexture(data.shadow), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				struct RayTracedShadowsConstants
				{
					uint32  accel_struct_idx;
					uint32  depth_idx;
					uint32  output_idx;
					uint32  light_idx;
				} constants = 
				{
					.accel_struct_idx = i, .depth_idx = i + 1, .output_idx = i + 2,
					.light_idx = 0 //todo: change this later
				};
				cmd_list->SetComputeRootSignature(RootSignatureCache::Get(ERootSignature::Common));
				cmd_list->SetPipelineState1(ray_traced_shadows.Get());

				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRoot32BitConstants(1, 4, &constants, 0);
				
				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				RayTracingShaderTable table(ray_traced_shadows.Get());
				table.SetRayGenShader("RTS_RayGen_Hard");
				table.AddMissShader("RTS_Miss", 0);
				table.AddHitGroup("ShadowAnyHitGroup", 0);
				table.Commit(*gfx->GetDynamicAllocator(), dispatch_desc);
				cmd_list->DispatchRays(&dispatch_desc);

			}, ERGPassType::Compute, ERGPassFlags::None);
#ifdef _DEBUG
		AddRayTracedShadowsDebugPass(rg, light_id);
		AddGUI_Debug([&](void* args)
			{
				std::string name = "Ray Traced Shadows " + std::to_string(light_id);
				AddGUI_Debug_Common(name, rts_debug_texture.get(), args);
			}
		);
#endif
	}
	void RayTracer::AddRayTracedReflectionsPass(RenderGraph& rg, D3D12_CPU_DESCRIPTOR_HANDLE envmap)
	{
		if (!IsFeatureSupported(ERayTracingFeature::Reflections)) return;

		GlobalBlackboardData const& global_data = rg.GetBlackboard().GetChecked<GlobalBlackboardData>();
		struct RayTracedReflectionsPassData
		{
			RGTextureReadOnlyId depth;
			RGTextureReadOnlyId normal;
			RGTextureReadWriteId output;

			RGBufferReadOnlyId vb;
			RGBufferReadOnlyId ib;
			RGBufferReadOnlyId geo;
		};

		rg.ImportBuffer(RG_RES_NAME(BigVertexBuffer), global_vb.get());
		rg.ImportBuffer(RG_RES_NAME(BigIndexBuffer), global_ib.get());
		rg.ImportBuffer(RG_RES_NAME(BigGeometryBuffer), geo_buffer.get());

		rg.AddPass<RayTracedReflectionsPassData>("Ray Traced Reflections Pass",
			[=](RayTracedReflectionsPassData& data, RGBuilder& builder)
			{
				RGTextureDesc desc{};
				desc.width = width;
				desc.height = height;
				desc.format = EFormat::R8G8B8A8_UNORM;
				builder.DeclareTexture(RG_RES_NAME(RTR_Output), desc);

				data.output = builder.WriteTexture(RG_RES_NAME(RTR_Output));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil));
				data.normal = builder.ReadTexture(RG_RES_NAME(GBufferNormal));

				data.vb = builder.ReadBuffer(RG_RES_NAME(BigVertexBuffer));
				data.ib = builder.ReadBuffer(RG_RES_NAME(BigIndexBuffer));
				data.geo = builder.ReadBuffer(RG_RES_NAME(BigGeometryBuffer));
			},
			[=](RayTracedReflectionsPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();

				uint32 i = (uint32)descriptor_allocator->AllocateRange(7); //pack this in one CopyDescriptors call
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 0), accel_structure.GetTLAS()->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 1), ctx.GetReadOnlyTexture(data.depth), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 2), envmap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 3), ctx.GetReadWriteTexture(data.output), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 4), ctx.GetReadOnlyBuffer(data.vb), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 5), ctx.GetReadOnlyBuffer(data.ib), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 6), ctx.GetReadOnlyBuffer(data.geo), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				struct RayTracedReflectionsConstants
				{
					uint32  accel_struct_idx;
					uint32  depth_idx;
					uint32  env_map_idx;
					uint32  output_idx;
					uint32  vertices_idx;
					uint32  indices_idx;
					uint32  geo_infos_idx;
				} constants = 
				{
					.accel_struct_idx = i, .depth_idx = i + 1, .env_map_idx = i + 2, .output_idx = i + 3,
					.vertices_idx = i + 4, .indices_idx = i + 5, .geo_infos_idx = i + 6
				};

				cmd_list->SetComputeRootSignature(RootSignatureCache::Get(ERootSignature::Common));
				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRoot32BitConstants(1, 7, &constants, 0);
				cmd_list->SetPipelineState1(ray_traced_reflections.Get());

				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				RayTracingShaderTable table(ray_traced_reflections.Get());
				table.SetRayGenShader("RTR_RayGen");
				table.AddMissShader("RTR_Miss", 0);
				table.AddHitGroup("RTRClosestHitGroupPrimaryRay", 0);
				table.AddHitGroup("RTRClosestHitGroupReflectionRay", 1);
				table.Commit(*gfx->GetDynamicAllocator(), dispatch_desc);
				cmd_list->DispatchRays(&dispatch_desc);
			}, ERGPassType::Compute, ERGPassFlags::None);
#ifdef _DEBUG
		AddRayTracedReflectionsDebugPass(rg);
		AddGUI_Debug([&](void* args)
			{
				std::string name = "Ray Traced Reflections";
				AddGUI_Debug_Common(name, rtr_debug_texture.get(), args);
			}
		);
#endif
	}
	void RayTracer::AddRayTracedAmbientOcclusionPass(RenderGraph& rg)
	{
		if (!IsFeatureSupported(ERayTracingFeature::AmbientOcclusion)) return;

		GlobalBlackboardData const& global_data = rg.GetBlackboard().GetChecked<GlobalBlackboardData>();
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
				desc.format = EFormat::R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME(RTAO_Output), desc);

				data.output = builder.WriteTexture(RG_RES_NAME(RTAO_Output));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
				data.normal = builder.ReadTexture(RG_RES_NAME(GBufferNormal), ReadAccess_NonPixelShader);
			},
			[=](RayTracedAmbientOcclusionPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();

				uint32 i = (uint32)descriptor_allocator->AllocateRange(4);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 0), accel_structure.GetTLAS()->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 1), ctx.GetReadOnlyTexture(data.depth), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 2), ctx.GetReadOnlyTexture(data.normal), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 3), ctx.GetReadWriteTexture(data.output), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				struct RayTracedAmbientOcclusionConstants
				{
					uint32  accel_struct_idx;
					uint32  depth_idx;
					uint32  gbuf_normals_idx;
					uint32  output_idx;
					float ao_radius;
				} constants = 
				{
					.accel_struct_idx = i, .depth_idx = i + 1, .gbuf_normals_idx = i + 2, .output_idx = i + 3,
					.ao_radius = ao_radius
				};
				cmd_list->SetComputeRootSignature(RootSignatureCache::Get(ERootSignature::Common));
				cmd_list->SetPipelineState1(ray_traced_ambient_occlusion.Get());

				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRoot32BitConstants(1, 5, &constants, 0);

				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				RayTracingShaderTable table(ray_traced_ambient_occlusion.Get());
				table.SetRayGenShader("RTAO_RayGen");
				table.AddMissShader("RTAO_Miss", 0);
				table.AddHitGroup("RTAOAnyHitGroup", 0);
				table.Commit(*gfx->GetDynamicAllocator(), dispatch_desc);
				cmd_list->DispatchRays(&dispatch_desc);
			}, ERGPassType::Compute, ERGPassFlags::None);
#ifdef _DEBUG
		AddRayTracedAmbientOcclusionDebugPass(rg);
		AddGUI_Debug([&](void* args)
			{
				std::string name = "Ray Traced AO";
				AddGUI_Debug_Common(name, rtao_debug_texture.get(), args);
			}
		);
#endif
		blur_pass.AddPass(rg, RG_RES_NAME(RTAO_Output), RG_RES_NAME(AmbientOcclusion));

		AddGUI([&]() 
			{
				if (ImGui::TreeNodeEx("RTAO", ImGuiTreeNodeFlags_OpenOnDoubleClick))
				{
					ImGui::SliderFloat("Radius", &ao_radius, 1.0f, 16.0f);
					ImGui::TreePop();
					ImGui::Separator();
				}
			}
		);
	}

#ifdef _DEBUG
	void RayTracer::AddGUI_Debug_Common(std::string const& name, Texture* texture, void* args)
	{
		if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_OpenOnDoubleClick))
		{
			auto descriptor_allocator = static_cast<RingOnlineDescriptorAllocator*>(args);
			ImVec2 v_min = ImGui::GetWindowContentRegionMin();
			ImVec2 v_max = ImGui::GetWindowContentRegionMax();
			v_min.x += ImGui::GetWindowPos().x;
			v_min.y += ImGui::GetWindowPos().y;
			v_max.x += ImGui::GetWindowPos().x;
			v_max.y += ImGui::GetWindowPos().y;
			ImVec2 size(v_max.x - v_min.x, v_max.y - v_min.y);

			D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = texture->GetSRV();
			OffsetType descriptor_index = descriptor_allocator->Allocate();
			auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
			gfx->GetDevice()->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr, size);
			ImGui::TreePop();
			ImGui::Separator();
		}
	}
	void RayTracer::AddRayTracedAmbientOcclusionDebugPass(RenderGraph& rg)
	{
		struct CopyPassData
		{
			RGTextureCopySrcId copy_src;
			RGTextureCopyDstId copy_dst;
		};
		rg.ImportTexture(RG_RES_NAME(RTAO_Debug), rtao_debug_texture.get());
		rg.AddPass<CopyPassData>("Copy RTAO Pass",
			[=](CopyPassData& data, RenderGraphBuilder& builder)
			{
				data.copy_dst = builder.WriteCopyDstTexture(RG_RES_NAME(RTAO_Debug));
				data.copy_src = builder.ReadCopySrcTexture(RG_RES_NAME(RTAO_Output));
			},
			[=](CopyPassData const& data, RenderGraphContext& context, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				Texture const& src_texture = context.GetCopySrcTexture(data.copy_src);
				Texture const& dst_texture = context.GetCopyDstTexture(data.copy_dst);
				cmd_list->CopyResource(dst_texture.GetNative(), src_texture.GetNative());
			}, ERGPassType::Copy, ERGPassFlags::ForceNoCull);
	}
	void RayTracer::AddRayTracedShadowsDebugPass(RenderGraph& rg, size_t light_id)
	{
		struct CopyPassData
		{
			RGTextureCopySrcId copy_src;
			RGTextureCopyDstId copy_dst;
		};

		rg.ImportTexture(RG_RES_NAME(RayTracedShadows_Debug), rts_debug_texture.get());
		rg.AddPass<CopyPassData>("Copy RTS Pass",
			[=](CopyPassData& data, RenderGraphBuilder& builder)
			{
				data.copy_dst = builder.WriteCopyDstTexture(RG_RES_NAME(RayTracedShadows_Debug));
				data.copy_src = builder.ReadCopySrcTexture(RG_RES_NAME_IDX(RayTracedShadows, light_id));
			},
			[=](CopyPassData const& data, RenderGraphContext& context, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				Texture const& src_texture = context.GetCopySrcTexture(data.copy_src);
				Texture const& dst_texture = context.GetCopyDstTexture(data.copy_dst);
				cmd_list->CopyResource(dst_texture.GetNative(), src_texture.GetNative());
			}, ERGPassType::Copy, ERGPassFlags::ForceNoCull);
	}
	void RayTracer::AddRayTracedReflectionsDebugPass(RenderGraph& rg)
	{
		struct CopyPassData
		{
			RGTextureCopySrcId copy_src;
			RGTextureCopyDstId copy_dst;
		};
		rg.ImportTexture(RG_RES_NAME(RTR_Debug), rtr_debug_texture.get());
		rg.AddPass<CopyPassData>("Copy RTR Pass",
			[=](CopyPassData& data, RenderGraphBuilder& builder)
			{
				data.copy_dst = builder.WriteCopyDstTexture(RG_RES_NAME(RTR_Debug));
				data.copy_src = builder.ReadCopySrcTexture(RG_RES_NAME(RTR_Output));
			},
			[=](CopyPassData const& data, RenderGraphContext& context, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				Texture const& src_texture = context.GetCopySrcTexture(data.copy_src);
				Texture const& dst_texture = context.GetCopyDstTexture(data.copy_dst);
				cmd_list->CopyResource(dst_texture.GetNative(), src_texture.GetNative());
			}, ERGPassType::Copy, ERGPassFlags::ForceNoCull);
	}
#endif
	void RayTracer::CreateStateObjects()
	{
		ID3D12Device5* device = gfx->GetDevice();

		Shader const& rt_shadows_blob		= ShaderCache::GetShader(LIB_Shadows);
		Shader const& rt_soft_shadows_blob	= ShaderCache::GetShader(LIB_SoftShadows);
		Shader const& rtao_blob				= ShaderCache::GetShader(LIB_AmbientOcclusion);
		Shader const& rtr_blob				= ShaderCache::GetShader(LIB_Reflections);

		StateObjectBuilder rt_shadows_state_object_builder(6);
		{
			D3D12_EXPORT_DESC export_descs[] =
			{
				D3D12_EXPORT_DESC{.Name = L"RTS_RayGen_Hard", .ExportToRename = L"RTS_RayGen"},
				D3D12_EXPORT_DESC{.Name = L"RTS_AnyHit", .ExportToRename = NULL},
				D3D12_EXPORT_DESC{.Name = L"RTS_Miss", .ExportToRename = NULL}
			};

			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc{};
			dxil_lib_desc.DXILLibrary.BytecodeLength = rt_shadows_blob.GetLength();
			dxil_lib_desc.DXILLibrary.pShaderBytecode = rt_shadows_blob.GetPointer();
			dxil_lib_desc.NumExports = ARRAYSIZE(export_descs);
			dxil_lib_desc.pExports = export_descs;
			rt_shadows_state_object_builder.AddSubObject(dxil_lib_desc);

			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc2{};
			D3D12_EXPORT_DESC export_desc2{};
			export_desc2.ExportToRename = L"RTS_RayGen";
			export_desc2.Name = L"RTS_RayGen_Soft";
			dxil_lib_desc2.DXILLibrary.BytecodeLength = rt_soft_shadows_blob.GetLength();
			dxil_lib_desc2.DXILLibrary.pShaderBytecode = rt_soft_shadows_blob.GetPointer();
			dxil_lib_desc2.NumExports = 1;
			dxil_lib_desc2.pExports = &export_desc2;
			rt_shadows_state_object_builder.AddSubObject(dxil_lib_desc2);

			// Add a state subobject for the shader payload configuration
			D3D12_RAYTRACING_SHADER_CONFIG rt_shadows_shader_config{};
			rt_shadows_shader_config.MaxPayloadSizeInBytes = 4;	//bool in hlsl is 4 bytes
			rt_shadows_shader_config.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
			rt_shadows_state_object_builder.AddSubObject(rt_shadows_shader_config);

			D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig{};
			global_root_sig.pGlobalRootSignature = RootSignatureCache::Get(ERootSignature::Common);
			rt_shadows_state_object_builder.AddSubObject(global_root_sig);

			// Add a state subobject for the ray tracing pipeline config
			D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {};
			pipeline_config.MaxTraceRecursionDepth = 1;
			rt_shadows_state_object_builder.AddSubObject(pipeline_config);

			D3D12_HIT_GROUP_DESC anyhit_group{};
			anyhit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
			anyhit_group.AnyHitShaderImport = L"RTS_AnyHit";
			anyhit_group.HitGroupExport = L"ShadowAnyHitGroup";
			rt_shadows_state_object_builder.AddSubObject(anyhit_group);

			ray_traced_shadows.Attach(rt_shadows_state_object_builder.CreateStateObject(device));
		}

		StateObjectBuilder rtao_state_object_builder(5);
		{
			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc{};
			dxil_lib_desc.DXILLibrary.BytecodeLength = rtao_blob.GetLength();
			dxil_lib_desc.DXILLibrary.pShaderBytecode = rtao_blob.GetPointer();
			dxil_lib_desc.NumExports = 0;
			dxil_lib_desc.pExports = nullptr;
			rtao_state_object_builder.AddSubObject(dxil_lib_desc);

			D3D12_RAYTRACING_SHADER_CONFIG rtao_shader_config{};
			rtao_shader_config.MaxPayloadSizeInBytes = 4;	//bool in hlsl is 4 bytes
			rtao_shader_config.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
			rtao_state_object_builder.AddSubObject(rtao_shader_config);

			D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig{};
			global_root_sig.pGlobalRootSignature = RootSignatureCache::Get(ERootSignature::Common);
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

		StateObjectBuilder rtr_state_object_builder(6);
		{
			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc{};
			dxil_lib_desc.DXILLibrary.BytecodeLength = rtr_blob.GetLength();
			dxil_lib_desc.DXILLibrary.pShaderBytecode = rtr_blob.GetPointer();
			dxil_lib_desc.NumExports = 0;
			dxil_lib_desc.pExports = nullptr;
			rtr_state_object_builder.AddSubObject(dxil_lib_desc);

			D3D12_RAYTRACING_SHADER_CONFIG rtr_shader_config{};
			rtr_shader_config.MaxPayloadSizeInBytes = sizeof(float) * 4;
			rtr_shader_config.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
			rtr_state_object_builder.AddSubObject(rtr_shader_config);

			D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig{};
			global_root_sig.pGlobalRootSignature = RootSignatureCache::Get(ERootSignature::Common);
			rtr_state_object_builder.AddSubObject(global_root_sig);

			// Add a state subobject for the ray tracing pipeline config
			D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
			pipeline_config.MaxTraceRecursionDepth = 2;
			rtr_state_object_builder.AddSubObject(pipeline_config);

			D3D12_HIT_GROUP_DESC closesthit_group{};
			closesthit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
			closesthit_group.ClosestHitShaderImport = L"RTR_ClosestHitPrimaryRay";
			closesthit_group.HitGroupExport = L"RTRClosestHitGroupPrimaryRay";
			rtr_state_object_builder.AddSubObject(closesthit_group);

			closesthit_group.ClosestHitShaderImport = L"RTR_ClosestHitReflectionRay";
			closesthit_group.HitGroupExport = L"RTRClosestHitGroupReflectionRay";
			rtr_state_object_builder.AddSubObject(closesthit_group);

			ray_traced_reflections.Attach(rtr_state_object_builder.CreateStateObject(device));
		}
	}
	void RayTracer::OnLibraryRecompiled(EShaderId shader)
	{
		CreateStateObjects();
	}
}
