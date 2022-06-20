#include "RayTracer.h"
#include "Components.h"
#include "GlobalBlackboardData.h"
#include "../tecs/registry.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Graphics/ShaderUtility.h"
#include "../Logging/Logger.h"

using namespace DirectX;

namespace adria
{

	RayTracer::RayTracer(tecs::registry& reg, GraphicsDevice* gfx, uint32 width, uint32 height)
		: reg(reg), gfx(gfx), width(width), height(height), accel_structure(gfx),
		ray_tracing_cbuffer(gfx->GetDevice(), gfx->BackbufferCount()), blur_pass(width, height)
	{
		ID3D12Device* device = gfx->GetDevice();
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5{};
		HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
		ray_tracing_tier = features5.RaytracingTier;
		if (FAILED(hr) || ray_tracing_tier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		{
			ADRIA_LOG(INFO, "Ray Tracing is not supported! All Ray Tracing calls will be silently ignored!");
		}
		else if (ray_tracing_tier < D3D12_RAYTRACING_TIER_1_1)
		{
			ADRIA_LOG(INFO, "Ray Tracing Tier is less than Tier 1.1!"
				"Calls to Ray Traced Reflections will be silently ignored!");
		}

		CreateRootSignatures();
		CreateStateObjects();
		CreateShaderTables();
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
			return ray_tracing_tier >= D3D12_RAYTRACING_TIER_1_1;
		default:
			return false;
		}
	}

	void RayTracer::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
		blur_pass.OnResize(w, h);
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
		vb_desc.misc_flags = EBufferMiscFlag::RayTracing | EBufferMiscFlag::VertexBuffer;
		vb_desc.size = RayTracing::rt_vertices.size() * sizeof(CompleteVertex);
		vb_desc.stride = sizeof(CompleteVertex);

		BufferDesc ib_desc{};
		ib_desc.bind_flags = EBindFlag::ShaderResource;
		ib_desc.misc_flags = EBufferMiscFlag::RayTracing | EBufferMiscFlag::IndexBuffer;
		ib_desc.size = RayTracing::rt_indices.size() * sizeof(uint32);
		ib_desc.stride = sizeof(uint32);
		ib_desc.format = DXGI_FORMAT_R32_UINT;

		global_vb = std::make_unique<Buffer>(gfx, vb_desc, RayTracing::rt_vertices.data());
		global_ib = std::make_unique<Buffer>(gfx, ib_desc, RayTracing::rt_indices.data());

	}

	void RayTracer::Update(RayTracingSettings const& params)
	{
		if (!IsSupported()) return;

		RayTracingCBuffer ray_tracing_cbuf_data{};
		ray_tracing_cbuf_data.frame_count = gfx->FrameIndex();
		ray_tracing_cbuf_data.rtao_radius = params.ao_radius;
		ray_tracing_cbuffer.Update(ray_tracing_cbuf_data, gfx->BackbufferIndex());
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
				desc.format = DXGI_FORMAT_R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME_IDX(RayTracedShadows, light_id), desc);

				data.shadow = builder.WriteTexture(RG_RES_NAME_IDX(RayTracedShadows, light_id));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
			},
			[=](RayTracedShadowsPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
				auto dynamic_allocator = gfx->GetDynamicAllocator();

				LightCBuffer light_cbuf_data{};
				light_cbuf_data.active = light.active;
				light_cbuf_data.color = light.color * light.energy;
				light_cbuf_data.direction = light.direction;
				light_cbuf_data.inner_cosine = light.inner_cosine;
				light_cbuf_data.outer_cosine = light.outer_cosine;
				light_cbuf_data.position = light.position;
				light_cbuf_data.range = light.range;
				light_cbuf_data.type = static_cast<int32>(light.type);
				XMMATRIX camera_view = global_data.camera_view;
				light_cbuf_data.position = DirectX::XMVector4Transform(light_cbuf_data.position, camera_view);
				light_cbuf_data.direction = DirectX::XMVector4Transform(light_cbuf_data.direction, camera_view);

				DynamicAllocation light_allocation = dynamic_allocator->Allocate(GetCBufferSize<LightCBuffer>(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
				light_allocation.Update(light_cbuf_data);

				cmd_list->SetComputeRootSignature(ray_traced_shadows.root_signature.Get());

				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRootConstantBufferView(1, light_allocation.gpu_address);
				cmd_list->SetComputeRootConstantBufferView(2, ray_tracing_cbuffer.View(gfx->BackbufferIndex()).BufferLocation);
				cmd_list->SetComputeRootShaderResourceView(3, accel_structure.GetTLAS()->GetGPUAddress());

				OffsetType descriptor_index = descriptor_allocator->AllocateRange(1);
				auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, ctx.GetReadOnlyTexture(data.depth),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(4, dst_descriptor);

				descriptor_index = descriptor_allocator->AllocateRange(1);
				dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, ctx.GetReadWriteTexture(data.shadow), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(5, dst_descriptor);
				cmd_list->SetPipelineState1(ray_traced_shadows.state_object.Get());

				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.HitGroupTable = ray_traced_shadows.shader_table_hit->GetRangeAndStride();
				dispatch_desc.MissShaderTable = ray_traced_shadows.shader_table_miss->GetRangeAndStride();
				dispatch_desc.RayGenerationShaderRecord = ray_traced_shadows.shader_table_raygen->GetRange(static_cast<UINT>(light.soft_rts));
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				cmd_list->DispatchRays(&dispatch_desc);

			}, ERGPassType::Compute, ERGPassFlags::None);
	}

	void RayTracer::AddRayTracedReflectionsPass(RenderGraph& rg, Descriptor envmap)
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
				desc.format = DXGI_FORMAT_R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME(RTR_Output), desc);

				data.output = builder.WriteTexture(RG_RES_NAME(RTR_Output));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
				data.normal = builder.ReadTexture(RG_RES_NAME(GBufferNormals), ReadAccess_NonPixelShader);

				data.vb = builder.ReadBuffer(RG_RES_NAME(BigVertexBuffer), ReadAccess_NonPixelShader);
				data.ib = builder.ReadBuffer(RG_RES_NAME(BigIndexBuffer), ReadAccess_NonPixelShader);
				data.geo = builder.ReadBuffer(RG_RES_NAME(BigGeometryBuffer), ReadAccess_NonPixelShader);
			},
			[=](RayTracedReflectionsPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();

				cmd_list->SetComputeRootSignature(ray_traced_reflections.root_signature.Get());
				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRootConstantBufferView(1, ray_tracing_cbuffer.View(gfx->BackbufferIndex()).BufferLocation);
				cmd_list->SetComputeRootShaderResourceView(2, accel_structure.GetTLAS()->GetGPUAddress());

				OffsetType descriptor_index = descriptor_allocator->AllocateRange(2);
				auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, ctx.GetReadOnlyTexture(data.depth), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(descriptor_index + 1), envmap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(3, dst_descriptor);

				descriptor_index = descriptor_allocator->AllocateRange(1);
				dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, ctx.GetReadWriteTexture(data.output), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(4, dst_descriptor);
				cmd_list->SetComputeRootDescriptorTable(5, descriptor_allocator->GetFirstHandle());

				descriptor_index = descriptor_allocator->AllocateRange(3);
				device->CopyDescriptorsSimple(3, descriptor_allocator->GetHandle(descriptor_index + 0), ctx.GetReadOnlyBuffer(data.vb), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(3, descriptor_allocator->GetHandle(descriptor_index + 1), ctx.GetReadOnlyBuffer(data.ib), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(3, descriptor_allocator->GetHandle(descriptor_index + 2), ctx.GetReadOnlyBuffer(data.geo), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(6, dst_descriptor);

				cmd_list->SetPipelineState1(ray_traced_reflections.state_object.Get());

				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.HitGroupTable = ray_traced_reflections.shader_table_hit->GetRangeAndStride();
				dispatch_desc.MissShaderTable = ray_traced_reflections.shader_table_miss->GetRangeAndStride();
				dispatch_desc.RayGenerationShaderRecord = ray_traced_reflections.shader_table_raygen->GetRange(0);
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				cmd_list->DispatchRays(&dispatch_desc);
			}, ERGPassType::Compute, ERGPassFlags::None);

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

		rg.AddPass<RayTracedAmbientOcclusionPassData>("Ray Traced Shadows Pass",
			[=](RayTracedAmbientOcclusionPassData& data, RGBuilder& builder)
			{
				RGTextureDesc desc{};
				desc.width = width;
				desc.height = height;
				desc.format = DXGI_FORMAT_R8_UNORM;
				builder.DeclareTexture(RG_RES_NAME(RTAO_Output), desc);

				data.output = builder.WriteTexture(RG_RES_NAME(RTAO_Output));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
				data.normal = builder.ReadTexture(RG_RES_NAME(GBufferNormals), ReadAccess_NonPixelShader);
			},
			[=](RayTracedAmbientOcclusionPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();

				cmd_list->SetComputeRootSignature(ray_traced_ambient_occlusion.root_signature.Get());

				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRootConstantBufferView(1, ray_tracing_cbuffer.View(gfx->BackbufferIndex()).BufferLocation);
				cmd_list->SetComputeRootShaderResourceView(2, accel_structure.GetTLAS()->GetGPUAddress());

				OffsetType descriptor_index = descriptor_allocator->AllocateRange(2);
				auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, ctx.GetReadOnlyTexture(data.depth), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(descriptor_index + 1), ctx.GetReadOnlyTexture(data.normal), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(3, dst_descriptor);

				descriptor_index = descriptor_allocator->AllocateRange(1);
				dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, ctx.GetReadWriteTexture(data.output), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(4, dst_descriptor);
				cmd_list->SetPipelineState1(ray_traced_ambient_occlusion.state_object.Get());

				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.HitGroupTable = ray_traced_ambient_occlusion.shader_table_hit->GetRangeAndStride();
				dispatch_desc.MissShaderTable = ray_traced_ambient_occlusion.shader_table_miss->GetRangeAndStride();
				dispatch_desc.RayGenerationShaderRecord = ray_traced_ambient_occlusion.shader_table_raygen->GetRange(0);
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				cmd_list->DispatchRays(&dispatch_desc);
			}, ERGPassType::Compute, ERGPassFlags::None);
	}

	void RayTracer::CreateRootSignatures()
	{
		ID3D12Device5* device = gfx->GetDevice();

		D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data{};
		feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		//RTS
		{
			std::array<CD3DX12_ROOT_PARAMETER1, 6> root_parameters{};
			root_parameters[0].InitAsConstantBufferView(0);
			root_parameters[1].InitAsConstantBufferView(2);
			root_parameters[2].InitAsConstantBufferView(10);
			root_parameters[3].InitAsShaderResourceView(0);

			D3D12_DESCRIPTOR_RANGE1 srv_range = {};
			srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			srv_range.NumDescriptors = 1;
			srv_range.BaseShaderRegister = 1;
			srv_range.RegisterSpace = 0;
			srv_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[4].InitAsDescriptorTable(1, &srv_range);

			D3D12_DESCRIPTOR_RANGE1 uav_range = {};
			uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uav_range.NumDescriptors = 1;
			uav_range.BaseShaderRegister = 0;
			uav_range.RegisterSpace = 0;
			uav_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[5].InitAsDescriptorTable(1, &uav_range);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc{};
			root_signature_desc.Init_1_1((uint32)root_parameters.size(), root_parameters.data(), 0, nullptr);

			Microsoft::WRL::ComPtr<ID3DBlob> signature;
			Microsoft::WRL::ComPtr<ID3DBlob> error;
			D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error);
			if (error)
			{
				ADRIA_LOG(ERROR, (char*)error->GetBufferPointer());
				ADRIA_ASSERT(FALSE);
			}
			BREAK_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&ray_traced_shadows.root_signature)));
		}

		//RTAO
		{
			std::array<CD3DX12_ROOT_PARAMETER1, 5> root_parameters{};
			root_parameters[0].InitAsConstantBufferView(0);
			root_parameters[1].InitAsConstantBufferView(10);
			root_parameters[2].InitAsShaderResourceView(0);

			D3D12_DESCRIPTOR_RANGE1 srv_range = {};
			srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			srv_range.NumDescriptors = 2;
			srv_range.BaseShaderRegister = 1;
			srv_range.RegisterSpace = 0;
			srv_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[3].InitAsDescriptorTable(1, &srv_range);

			D3D12_DESCRIPTOR_RANGE1 uav_range = {};
			uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uav_range.NumDescriptors = 1;
			uav_range.BaseShaderRegister = 0;
			uav_range.RegisterSpace = 0;
			uav_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[4].InitAsDescriptorTable(1, &uav_range);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc{};
			root_signature_desc.Init_1_1((uint32)root_parameters.size(), root_parameters.data(), 0, nullptr);

			Microsoft::WRL::ComPtr<ID3DBlob> signature;
			Microsoft::WRL::ComPtr<ID3DBlob> error;
			D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error);
			if (error)
			{
				ADRIA_LOG(ERROR, (char*)error->GetBufferPointer());
				ADRIA_ASSERT(FALSE);
			}
			BREAK_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&ray_traced_ambient_occlusion.root_signature)));
		}

		//RTR
		{
			std::array<CD3DX12_ROOT_PARAMETER1, 7> root_parameters{};
			root_parameters[0].InitAsConstantBufferView(0);
			root_parameters[1].InitAsConstantBufferView(10);
			root_parameters[2].InitAsShaderResourceView(0);

			D3D12_DESCRIPTOR_RANGE1 srv_range{};
			srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			srv_range.NumDescriptors = 2;
			srv_range.BaseShaderRegister = 1;
			srv_range.RegisterSpace = 0;
			srv_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[3].InitAsDescriptorTable(1, &srv_range);

			D3D12_DESCRIPTOR_RANGE1 uav_range{};
			uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uav_range.NumDescriptors = 1;
			uav_range.BaseShaderRegister = 0;
			uav_range.RegisterSpace = 0;
			uav_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[4].InitAsDescriptorTable(1, &uav_range);

			D3D12_DESCRIPTOR_RANGE1 unbounded_srv_range{};
			unbounded_srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			unbounded_srv_range.NumDescriptors = UINT_MAX;
			unbounded_srv_range.BaseShaderRegister = 0;
			unbounded_srv_range.RegisterSpace = 1;
			unbounded_srv_range.OffsetInDescriptorsFromTableStart = 0;
			unbounded_srv_range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
			root_parameters[5].InitAsDescriptorTable(1, &unbounded_srv_range);

			D3D12_DESCRIPTOR_RANGE1 geometry_srv_range{};
			geometry_srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			geometry_srv_range.NumDescriptors = 3;
			geometry_srv_range.BaseShaderRegister = 0;
			geometry_srv_range.RegisterSpace = 2;
			geometry_srv_range.OffsetInDescriptorsFromTableStart = 0;
			root_parameters[6].InitAsDescriptorTable(1, &geometry_srv_range);

			D3D12_STATIC_SAMPLER_DESC linear_wrap_sampler = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc{};
			root_signature_desc.Init_1_1((uint32)root_parameters.size(), root_parameters.data(), 1, &linear_wrap_sampler);

			Microsoft::WRL::ComPtr<ID3DBlob> signature;
			Microsoft::WRL::ComPtr<ID3DBlob> error;
			D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error);
			if (error)
			{
				ADRIA_LOG(ERROR, (char*)error->GetBufferPointer());
				ADRIA_ASSERT(FALSE);
			}
			BREAK_IF_FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&ray_traced_reflections.root_signature)));
		}
	}

	void RayTracer::CreateStateObjects()
	{
		ID3D12Device5* device = gfx->GetDevice();

		ShaderInfo compile_info{};
		compile_info.stage = EShaderStage::LIB;
		compile_info.shadersource = "Resources/Shaders/RayTracing/RayTracedShadows.hlsl";
		ShaderBlob rt_shadows_blob;
		ShaderUtility::CompileShader(compile_info, rt_shadows_blob);

		compile_info.macros.emplace_back(L"SOFT_SHADOWS", L"");
		ShaderBlob rt_soft_shadows_blob;
		ShaderUtility::CompileShader(compile_info, rt_soft_shadows_blob);
		compile_info.macros.clear();

		compile_info.shadersource = "Resources/Shaders/RayTracing/RayTracedAmbientOcclusion.hlsl";
		ShaderBlob rtao_blob;
		ShaderUtility::CompileShader(compile_info, rtao_blob);

		compile_info.shadersource = "Resources/Shaders/RayTracing/RayTracedReflections.hlsl";
		ShaderBlob rtr_blob;
		ShaderUtility::CompileShader(compile_info, rtr_blob);

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
			global_root_sig.pGlobalRootSignature = ray_traced_shadows.root_signature.Get();
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

			ray_traced_shadows.state_object = rt_shadows_state_object_builder.CreateStateObject(device);
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
			global_root_sig.pGlobalRootSignature = ray_traced_ambient_occlusion.root_signature.Get();
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

			ray_traced_ambient_occlusion.state_object = rtao_state_object_builder.CreateStateObject(device);
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
			rtr_shader_config.MaxPayloadSizeInBytes = sizeof(float32) * 4;
			rtr_shader_config.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
			rtr_state_object_builder.AddSubObject(rtr_shader_config);

			D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig{};
			global_root_sig.pGlobalRootSignature = ray_traced_reflections.root_signature.Get();
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

			ray_traced_reflections.state_object = rtr_state_object_builder.CreateStateObject(device);
		}
	}

	void RayTracer::CreateShaderTables()
	{
		ID3D12Device5* device = gfx->GetDevice();
		{
			Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> pso_info = nullptr;
			BREAK_IF_FAILED(ray_traced_shadows.state_object->QueryInterface(IID_PPV_ARGS(&pso_info)));

			void const* rts_ray_gen_hard_id = pso_info->GetShaderIdentifier(L"RTS_RayGen_Hard");
			void const* rts_ray_gen_soft_id = pso_info->GetShaderIdentifier(L"RTS_RayGen_Soft");
			void const* rts_anyhit_id = pso_info->GetShaderIdentifier(L"ShadowAnyHitGroup");
			void const* rts_miss_id = pso_info->GetShaderIdentifier(L"RTS_Miss");

			ray_traced_shadows.shader_table_raygen = std::make_unique<ShaderTable>(device, 2);
			ray_traced_shadows.shader_table_raygen->AddShaderRecord(ShaderRecord(rts_ray_gen_hard_id));
			ray_traced_shadows.shader_table_raygen->AddShaderRecord(ShaderRecord(rts_ray_gen_soft_id));

			ray_traced_shadows.shader_table_hit = std::make_unique<ShaderTable>(device, 1);
			ray_traced_shadows.shader_table_hit->AddShaderRecord(ShaderRecord(rts_anyhit_id));

			ray_traced_shadows.shader_table_miss = std::make_unique<ShaderTable>(device, 1);
			ray_traced_shadows.shader_table_miss->AddShaderRecord(ShaderRecord(rts_miss_id));
		}
		{
			Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> pso_info = nullptr;
			BREAK_IF_FAILED(ray_traced_ambient_occlusion.state_object->QueryInterface(IID_PPV_ARGS(&pso_info)));

			void const* rtao_ray_gen_id = pso_info->GetShaderIdentifier(L"RTAO_RayGen");
			void const* rtao_anyhit_id = pso_info->GetShaderIdentifier(L"RTAOAnyHitGroup");
			void const* rtao_miss_id = pso_info->GetShaderIdentifier(L"RTAO_Miss");

			ray_traced_ambient_occlusion.shader_table_raygen = std::make_unique<ShaderTable>(device, 1);
			ray_traced_ambient_occlusion.shader_table_raygen->AddShaderRecord(ShaderRecord(rtao_ray_gen_id));

			ray_traced_ambient_occlusion.shader_table_hit = std::make_unique<ShaderTable>(device, 1);
			ray_traced_ambient_occlusion.shader_table_hit->AddShaderRecord(ShaderRecord(rtao_anyhit_id));

			ray_traced_ambient_occlusion.shader_table_miss = std::make_unique<ShaderTable>(device, 1);
			ray_traced_ambient_occlusion.shader_table_miss->AddShaderRecord(ShaderRecord(rtao_miss_id));
		}
		{
			Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> pso_info = nullptr;
			BREAK_IF_FAILED(ray_traced_reflections.state_object->QueryInterface(IID_PPV_ARGS(&pso_info)));

			void const* rtr_ray_gen_id = pso_info->GetShaderIdentifier(L"RTR_RayGen");
			void const* rtr_closesthit_primary_ray_id = pso_info->GetShaderIdentifier(L"RTRClosestHitGroupPrimaryRay");
			void const* rtr_closesthit_reflection_ray_id = pso_info->GetShaderIdentifier(L"RTRClosestHitGroupReflectionRay");
			void const* rtr_miss_id = pso_info->GetShaderIdentifier(L"RTR_Miss");

			ray_traced_reflections.shader_table_raygen = std::make_unique<ShaderTable>(device, 1);
			ray_traced_reflections.shader_table_raygen->AddShaderRecord(ShaderRecord(rtr_ray_gen_id));

			ray_traced_reflections.shader_table_hit = std::make_unique<ShaderTable>(device, 2);
			ray_traced_reflections.shader_table_hit->AddShaderRecord(ShaderRecord(rtr_closesthit_primary_ray_id));
			ray_traced_reflections.shader_table_hit->AddShaderRecord(ShaderRecord(rtr_closesthit_reflection_ray_id));

			ray_traced_reflections.shader_table_miss = std::make_unique<ShaderTable>(device, 1);
			ray_traced_reflections.shader_table_miss->AddShaderRecord(ShaderRecord(rtr_miss_id));
		}
	}

}

