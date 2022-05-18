#pragma once
#include <memory>
#include <optional>
#include "GraphicsDeviceDX12.h"
#include "d3dx12.h"
#include "../Utilities/EnumUtil.h"

namespace adria
{
	inline constexpr uint32_t GetFormatStride(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC4_UNORM:
			return 8u;

		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 16u;

		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R32G32B32_UINT:
		case DXGI_FORMAT_R32G32B32_SINT:
			return 12u;

		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_SINT:
			return 8u;

		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			return 8u;

		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return 4u;

		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_SINT:
			return 2u;

		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_SINT:
			return 1u;
		default:
			ADRIA_ASSERT(false);
			break;
		}

		return 16u;
	}


	enum class EBindFlag : uint32
	{
		None = 0,
		VertexBuffer = 1 << 0,
		IndexBuffer = 1 << 1,
		ConstantBuffer = 1 << 2,
		ShaderResource = 1 << 3,
		RenderTarget = 1 << 4,
		DepthStencil = 1 << 5,
		UnorderedAccess = 1 << 6,
	};
	DEFINE_ENUM_BIT_OPERATORS(EBindFlag);

	enum class EHeapType : uint8
	{
		Default,	// CPU no access, GPU read/write
		Upload,	    // CPU write, GPU read
		Readback,	// CPU read, GPU write
	};

	enum class EResourceMiscFlag : uint32
	{
		None = 0,
		TextureCube = 1 << 0,
		IndirectArgs = 1 << 1,
		BufferRaw = 1 << 2,
		BufferStructured = 1 << 3,
		RayTracing = 1 << 4,
	};
	DEFINE_ENUM_BIT_OPERATORS(EResourceMiscFlag);

	struct BufferDesc
	{
		uint64 size = 0;
		EHeapType heap_type = EHeapType::Default;
		EBindFlag bind_flags = EBindFlag::None;
		EResourceMiscFlag misc_flags = EResourceMiscFlag::None;
		uint32 stride = 0; //structured buffers
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; //typed buffers
	};

	enum class EResourceViewType : uint8
	{
		SRV,
		UAV,
		RTV,
		DSV,
		Invalid
	};

	struct BufferViewDesc
	{
		EResourceViewType view_type = EResourceViewType::Invalid;
		uint64 offset = 0;
		uint64 size = uint64(-1);
		std::optional<DXGI_FORMAT> new_format;
	};

	class Buffer
	{
	public:

		Buffer(GraphicsDevice* gfx, BufferDesc const& desc, void const* initial_data = nullptr)
			: desc(desc)
		{
			UINT64 buffer_size = desc.size;
			if (HasAllFlags(desc.bind_flags, EBindFlag::ConstantBuffer))
				buffer_size = Align(buffer_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

			D3D12_RESOURCE_DESC resource_desc{};
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Format = DXGI_FORMAT_UNKNOWN;
			resource_desc.Width = buffer_size;
			resource_desc.Height = 1;
			resource_desc.MipLevels = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.Alignment = 0;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.SampleDesc.Quality = 0;

			if (HasAllFlags(desc.bind_flags, EBindFlag::UnorderedAccess))
				resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			if (!HasAllFlags(desc.bind_flags, EBindFlag::ShaderResource) && !HasAllFlags(desc.misc_flags, EResourceMiscFlag::RayTracing))
				resource_desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

			D3D12_RESOURCE_STATES resource_state = D3D12_RESOURCE_STATE_COMMON;

			D3D12MA::ALLOCATION_DESC allocation_desc{};
			allocation_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
			if (desc.heap_type == EHeapType::Readback)
			{
				allocation_desc.HeapType = D3D12_HEAP_TYPE_READBACK;
				resource_state = D3D12_RESOURCE_STATE_COPY_DEST;
				resource_desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
			}
			else if (desc.heap_type == EHeapType::Upload)
			{
				allocation_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
				resource_state = D3D12_RESOURCE_STATE_GENERIC_READ;
			}

			auto allocator = gfx->GetAllocator();
			auto device = gfx->GetDevice();

			device->GetCopyableFootprints(&resource_desc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);

			D3D12MA::Allocation* alloc = allocation.get();
			HRESULT hr = allocator->CreateResource(
				&allocation_desc,
				&resource_desc,
				resource_state,
				nullptr,
				&alloc,
				IID_PPV_ARGS(&resource)
			);
			BREAK_IF_FAILED(hr);

			if (desc.heap_type == EHeapType::Readback)
			{
				hr = resource->Map(0, nullptr, &mapped_data);
				BREAK_IF_FAILED(hr);
				mapped_rowpitch = static_cast<uint32_t>(desc.size);
			}
			else if (desc.heap_type == EHeapType::Upload)
			{
				D3D12_RANGE read_range{};
				hr = resource->Map(0, &read_range, &mapped_data);
				BREAK_IF_FAILED(hr);
				mapped_rowpitch = static_cast<uint32>(desc.size);
			}

			if (initial_data != nullptr)
			{
				auto cmd_list = gfx->GetDefaultCommandList();
				auto upload_buffer = gfx->GetUploadBuffer();
				DynamicAllocation upload_alloc = upload_buffer->Allocate(buffer_size);
				upload_alloc.Update(initial_data, desc.size);
				cmd_list->CopyBufferRegion(
					resource.Get(),
					0,
					upload_alloc.buffer,
					upload_alloc.offset,
					desc.size);
			}
		}
		Buffer(Buffer const&) = delete;
		Buffer& operator=(Buffer const&) = delete;
		~Buffer() = default;

		[[nodiscard]] size_t CreateView(BufferViewDesc const& view_desc, D3D12_CPU_DESCRIPTOR_HANDLE heap_descriptor, 
			ID3D12Resource* uav_counter = nullptr)
		{
			if (uav_counter) ADRIA_ASSERT(view_desc.view_type == EResourceViewType::UAV);

			DXGI_FORMAT format = desc.format;
			if (view_desc.new_format.has_value()) format = view_desc.new_format.value();

			switch (view_desc.view_type)
			{
			case EResourceViewType::SRV:
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
				srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

				if (format == DXGI_FORMAT_UNKNOWN)
				{
					if (HasAllFlags(desc.misc_flags, EResourceMiscFlag::BufferRaw))
					{
						// This is a Raw Buffer
						srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
						srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						srv_desc.Buffer.FirstElement = (UINT)view_desc.offset / sizeof(uint32_t);
						srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
						srv_desc.Buffer.NumElements = (UINT)std::min<UINT64>(view_desc.size, desc.size - view_desc.offset) / sizeof(uint32_t);
					}
					else if (HasAllFlags(desc.misc_flags, EResourceMiscFlag::BufferStructured))
					{
						// This is a Structured Buffer
						srv_desc.Format = DXGI_FORMAT_UNKNOWN;
						srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
						srv_desc.Buffer.FirstElement = (UINT)view_desc.offset / desc.stride;
						srv_desc.Buffer.NumElements = (UINT)std::min<UINT64>(view_desc.size, desc.size - view_desc.offset) / desc.stride;
						srv_desc.Buffer.StructureByteStride = desc.stride;
					}
				}
				else
				{
					// This is a Typed Buffer
					uint32_t stride = GetFormatStride(format);
					srv_desc.Format = format;
					srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					srv_desc.Buffer.FirstElement = view_desc.offset / stride;
					srv_desc.Buffer.NumElements = (UINT)std::min<UINT64>(view_desc.size, desc.size - view_desc.offset) / stride;
				}

				ID3D12Device* device;
				resource->GetDevice(IID_PPV_ARGS(&device));

				device->CreateShaderResourceView(resource.Get(), &srv_desc, heap_descriptor);
				srvs.push_back(heap_descriptor);
				return srvs.size() - 1;
			}
			break;
			case EResourceViewType::UAV:
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
				uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				uav_desc.Buffer.FirstElement = 0;

				if (format == DXGI_FORMAT_UNKNOWN)
				{
					if (HasAllFlags(desc.misc_flags, EResourceMiscFlag::BufferRaw))
					{
						// This is a Raw Buffer
						uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
						uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
						uav_desc.Buffer.FirstElement = (UINT)view_desc.offset / sizeof(uint32_t);
						uav_desc.Buffer.NumElements = (UINT)std::min<UINT64>(view_desc.size, desc.size - view_desc.offset) / sizeof(uint32_t);
					}
					else if (HasAllFlags(desc.misc_flags, EResourceMiscFlag::BufferStructured))
					{
						// This is a Structured Buffer
						uav_desc.Format = DXGI_FORMAT_UNKNOWN;
						uav_desc.Buffer.FirstElement = (UINT)view_desc.offset / desc.stride;
						uav_desc.Buffer.NumElements = (UINT)std::min<UINT64>(view_desc.size, desc.size - view_desc.offset) / desc.stride;
						uav_desc.Buffer.StructureByteStride = desc.stride;
					}
				}
				else
				{
					// This is a Typed Buffer
					uint32_t stride = GetFormatStride(format);
					uav_desc.Format = format;
					uav_desc.Buffer.FirstElement = (UINT)view_desc.offset / stride;
					uav_desc.Buffer.NumElements = (UINT)std::min<UINT64>(view_desc.size, desc.size - view_desc.offset) / stride;
				}

				ID3D12Device* device;
				resource->GetDevice(IID_PPV_ARGS(&device));

				device->CreateUnorderedAccessView(resource.Get(), uav_counter, &uav_desc, heap_descriptor);
				uavs.push_back(heap_descriptor);
				return uavs.size() - 1;
			}
			break;
			case EResourceViewType::RTV:
			case EResourceViewType::DSV:
			default:
				ADRIA_ASSERT(false && "Buffer View can only be UAV or SRV!");
			}
		}
		D3D12_CPU_DESCRIPTOR_HANDLE GetView(EResourceViewType type, size_t index = 0) const
		{
			switch (type)
			{
			case EResourceViewType::SRV:
				ADRIA_ASSERT(index < srvs.size());
				return srvs[index];
			case EResourceViewType::UAV:
				ADRIA_ASSERT(index < uavs.size());
				return uavs[index];
			case EResourceViewType::RTV:
			case EResourceViewType::DSV:
			default:
				ADRIA_ASSERT(false && "Invalid view type for buffer!");
			}
			return {.ptr = NULL};
		}

		ID3D12Resource* GetNative() const { return resource.Get(); }
		void* GetMappedData() const { return mapped_data; }
		uint32 GetMappedRowPitch() const { return mapped_rowpitch; }

	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		BufferDesc desc;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvs;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> uavs;

		ReleasablePtr<D3D12MA::Allocation> allocation = nullptr;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;

		void* mapped_data = nullptr;
		uint32 mapped_rowpitch = 0;

	};

}