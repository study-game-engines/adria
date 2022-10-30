#include "DirectXTex.h"
#ifdef _DEBUG
#pragma comment(lib, "Debug\\DirectXTex.lib")
#else
#pragma comment(lib, "Release\\DirectXTex.lib")
#endif

#include "TextureManager.h"
#include "Texture.h"
#include "GraphicsDeviceDX12.h"
#include "d3dx12.h"
#include "DDSTextureLoader12.h"
#include "WICTextureLoader12.h"
#include "ShaderCompiler.h"
#include "../Utilities/StringUtil.h"
#include "../Utilities/Image.h"
#include "../Core/Macros.h"
#include "../Utilities/FilesUtil.h"

namespace adria
{

	namespace
	{
        enum class TextureFormat
        {
            eDDS,
            eBMP,
            eJPG,
            ePNG,
            eTIFF,
            eGIF,
            eICO,
            eTGA,
            eHDR,
            ePIC,
            eNotSupported
        };
        TextureFormat GetTextureFormat(std::string const& path)
        {
            std::string extension = GetExtension(path);
            std::transform(std::begin(extension), std::end(extension), std::begin(extension), [](char c) {return std::tolower(c); });

            if (extension == ".dds")
                return TextureFormat::eDDS;
            else if (extension == ".bmp")
                return TextureFormat::eBMP;
            else if (extension == ".jpg" || extension == ".jpeg")
                return TextureFormat::eJPG;
            else if (extension == ".png")
                return TextureFormat::ePNG;
            else if (extension == ".tiff" || extension == ".tif")
                return TextureFormat::eTIFF;
            else if (extension == ".gif")
                return TextureFormat::eGIF;
            else if (extension == ".ico")
                return TextureFormat::eICO;
            else if (extension == ".tga")
                return TextureFormat::eTGA;
            else if (extension == ".hdr")
                return TextureFormat::eHDR;
            else if (extension == ".pic")
                return TextureFormat::ePIC;
            else
                return TextureFormat::eNotSupported;
        }
        TextureFormat GetTextureFormat(std::wstring const& path)
        {
            return GetTextureFormat(ToString(path));
        }
	}

    TextureManager::TextureManager(GraphicsDevice* gfx, UINT max_textures) : gfx(gfx)
    {
        mips_generator = std::make_unique<MipsGenerator>(gfx->GetDevice(), max_textures);

		CD3DX12_DESCRIPTOR_RANGE1 const descriptor_ranges[] =
		{
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
			{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
		};
		CD3DX12_ROOT_PARAMETER1 root_parameters[2];
		root_parameters[0].InitAsDescriptorTable(1, &descriptor_ranges[0]);
		root_parameters[1].InitAsDescriptorTable(1, &descriptor_ranges[1]);
		CD3DX12_STATIC_SAMPLER_DESC sampler_desc{ 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR };

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC signature_desc;
		signature_desc.Init_1_1(2, root_parameters, 1, &sampler_desc);
		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> error;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(&signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
		if (error) ADRIA_LOG(ERROR, (char const*)error->GetBufferPointer());
		BREAK_IF_FAILED(gfx->GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&equirect_root_signature)));

		ShaderBlob equirect_cs_shader;
		ShaderCompiler::ReadBlobFromFile(L"Resources/Compiled Shaders/Equirect2cubeCS.cso", equirect_cs_shader);

		D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
		pso_desc.pRootSignature = equirect_root_signature.Get();
		pso_desc.CS = D3D12_SHADER_BYTECODE{.pShaderBytecode = equirect_cs_shader.data(), .BytecodeLength = equirect_cs_shader.size()};
		BREAK_IF_FAILED(gfx->GetDevice()->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&equirect_pso)));
	}

	void TextureManager::Tick()
	{
		mips_generator->Generate(gfx->GetDefaultCommandList());
	}

    [[nodiscard]]
    TextureHandle TextureManager::LoadTexture(std::wstring const& name)
    {
        TextureFormat format = GetTextureFormat(name);

        switch (format)
        {
        case TextureFormat::eDDS:
            return LoadDDSTexture(name);
        case TextureFormat::eBMP:
        case TextureFormat::ePNG:
        case TextureFormat::eJPG:
        case TextureFormat::eTIFF:
        case TextureFormat::eGIF:
        case TextureFormat::eICO:
            return LoadWICTexture(name);
        case TextureFormat::eTGA:
        case TextureFormat::eHDR:
        case TextureFormat::ePIC:
            return LoadTexture_HDR_TGA_PIC(ToString(name));
        case TextureFormat::eNotSupported:
        default:
            ADRIA_ASSERT(false && "Unsupported Texture Format!");
        }
        return INVALID_TEXTURE_HANDLE;
    }

    [[nodiscard]]
    TextureHandle TextureManager::LoadCubemap(std::wstring const& name)
    {
        TextureFormat format = GetTextureFormat(name);
        ADRIA_ASSERT(format == TextureFormat::eDDS || format == TextureFormat::eHDR && "Cubemap in one file has to be .dds or .hdr format");

        if (auto it = loaded_textures.find(name); it == loaded_textures.end())
        {
            ++handle;
            auto device = gfx->GetDevice();
            auto allocator = gfx->GetAllocator();
            auto cmd_list = gfx->GetDefaultCommandList();
            if (format == TextureFormat::eDDS)
            {
                loaded_textures.insert({ name, handle });
                Microsoft::WRL::ComPtr<ID3D12Resource> cubemap = nullptr;
                std::unique_ptr<uint8_t[]> decoded_data;
                std::vector<TextureInitialData> subresources;

                bool is_cubemap;
                BREAK_IF_FAILED(
                    DirectX::LoadDDSTextureFromFile(device, name.c_str(), cubemap.GetAddressOf(),
                        decoded_data, subresources, 0, nullptr, &is_cubemap));
                ADRIA_ASSERT(is_cubemap);

				D3D12_RESOURCE_DESC _desc = cubemap->GetDesc();
				TextureDesc desc{};
				desc.type = TextureType_2D;
                desc.misc_flags = ETextureMiscFlag::TextureCube;
				desc.width = (uint32)_desc.Width;
				desc.height = _desc.Height;
				desc.array_size = 6;
				desc.bind_flags = EBindFlag::ShaderResource;
				desc.format = ConvertDXGIFormat(_desc.Format);
				desc.initial_state = EResourceState::PixelShaderResource | EResourceState::NonPixelShaderResource;
				desc.heap_type = EResourceUsage::Default;
				desc.mip_levels = _desc.MipLevels;

				std::unique_ptr<Texture> tex = std::make_unique<Texture>(gfx, desc, subresources.data());
                texture_map.insert({ handle, std::move(tex)});
                CreateViewForTexture(handle);
            }
            else //format == TextureFormat::eHDR
            {
                auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
                loaded_textures.insert({ name, handle });
                Image equirect_hdr_image(ToString(name));

                TextureDesc desc;
                desc.type = TextureType_2D;
                desc.misc_flags = ETextureMiscFlag::TextureCube;
                desc.heap_type = EResourceUsage::Default;
                desc.width = 1024;
                desc.height = 1024;
                desc.array_size = 6;
                desc.mip_levels = 1;
                desc.format = EFormat::R16G16B16A16_FLOAT;
                desc.bind_flags = EBindFlag::ShaderResource | EBindFlag::UnorderedAccess;
                desc.initial_state = EResourceState::Common;

                std::unique_ptr<Texture> cubemap_tex = std::make_unique<Texture>(gfx, desc);
                cubemap_tex->CreateSRV();
                cubemap_tex->CreateUAV();

                TextureDesc equirect_desc{};
                equirect_desc.width = equirect_hdr_image.Width();
                equirect_desc.height = equirect_hdr_image.Height();
                equirect_desc.type = TextureType_2D;
                equirect_desc.mip_levels = 1;
                equirect_desc.array_size = 1;
                equirect_desc.format = EFormat::R32G32B32A32_FLOAT;
                equirect_desc.bind_flags = EBindFlag::ShaderResource;
                equirect_desc.initial_state = EResourceState::CopyDest;

                TextureInitialData subresource_data{};
                subresource_data.pData = equirect_hdr_image.Data<void>();
                subresource_data.RowPitch = equirect_hdr_image.Pitch();

				Texture equirect_tex(gfx, desc,&subresource_data);
                equirect_tex.CreateSRV();

                D3D12_RESOURCE_BARRIER barriers[] = {
                    CD3DX12_RESOURCE_BARRIER::Transition(cubemap_tex->GetNative(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), 
                    CD3DX12_RESOURCE_BARRIER::Transition(equirect_tex.GetNative(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) };
                cmd_list->ResourceBarrier(_countof(barriers), barriers);

                ID3D12DescriptorHeap* heap = descriptor_allocator->Heap();
                cmd_list->SetDescriptorHeaps(1, &heap);

                //Set root signature, pso and descriptor heap
                cmd_list->SetComputeRootSignature(equirect_root_signature.Get());
                cmd_list->SetPipelineState(equirect_pso.Get());

                cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetHandle(1));
                cmd_list->SetComputeRootDescriptorTable(1, descriptor_allocator->GetHandle(0));
                cmd_list->Dispatch(1024 / 32, 1024 / 32, 6);

                auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap_tex->GetNative(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                cmd_list->ResourceBarrier(1, &barrier);

                texture_map.insert({ handle, std::move(cubemap_tex)});
                CreateViewForTexture(handle);
            }

            return handle;
        }
        else return it->second;
    }

    [[nodiscard]]
    TextureHandle TextureManager::LoadCubemap(std::array<std::wstring, 6> const& cubemap_textures)
    {
        TextureFormat format = GetTextureFormat(cubemap_textures[0]);
        ADRIA_ASSERT(format == TextureFormat::eJPG || format == TextureFormat::ePNG || format == TextureFormat::eTGA ||
            format == TextureFormat::eBMP || format == TextureFormat::eHDR || format == TextureFormat::ePIC);

        auto device = gfx->GetDevice();
        auto allocator = gfx->GetAllocator();
        auto cmd_list = gfx->GetDefaultCommandList();

        ++handle;
        TextureDesc desc{};
        desc.type = TextureType_2D;
        desc.mip_levels = 1;
        desc.misc_flags = ETextureMiscFlag::TextureCube;
        desc.array_size = 6;
        desc.bind_flags = EBindFlag::ShaderResource;

        std::vector<Image> images{};
        std::vector<TextureInitialData> subresources;
        for (UINT i = 0; i < cubemap_textures.size(); ++i)
        {
            images.emplace_back(ToString(cubemap_textures[i]), 4);
            TextureInitialData subresource_data{};
            subresource_data.pData = images.back().Data<void>();
            subresource_data.RowPitch = images.back().Pitch();
            subresources.push_back(subresource_data);
        }
        desc.width = images[0].Width();
        desc.height = images[0].Height();
        desc.format = images[0].IsHDR() ? EFormat::R32G32B32A32_FLOAT : EFormat::R8G8B8A8_UNORM;
        std::unique_ptr<Texture> cubemap = std::make_unique<Texture>(gfx, desc, subresources.data());
        texture_map.insert({ handle, std::move(cubemap)});
        CreateViewForTexture(handle);
        return handle;
    }

    [[nodiscard]]
	D3D12_CPU_DESCRIPTOR_HANDLE TextureManager::GetSRV(TextureHandle tex_handle)
	{
		return texture_map[tex_handle]->GetSRV();
	}

	Texture* TextureManager::GetTexture(TextureHandle handle) const
	{
		if (handle == INVALID_TEXTURE_HANDLE) return nullptr;
		else if (auto it = texture_map.find(handle); it != texture_map.end()) return it->second.get();
		else return nullptr;
	}

	void TextureManager::EnableMipMaps(bool mips)
    {
        mipmaps = mips;
    }

	void TextureManager::OnSceneInitialized()
	{
		TextureDesc desc{};
		desc.width = 1;
		desc.height = 1;
		desc.format = EFormat::R32_FLOAT;
		desc.bind_flags = EBindFlag::ShaderResource;
		desc.initial_state = EResourceState::AllShaderResource;

		float v = 0.0f;
		TextureInitialData init_data{};
		init_data.pData = &v;
		init_data.RowPitch = sizeof(float);
		init_data.SlicePitch = 0;
		std::unique_ptr<Texture> black_default_texture = std::make_unique<Texture>(gfx, desc, &init_data);
		texture_map[INVALID_TEXTURE_HANDLE] = std::move(black_default_texture);

		mips_generator->Generate(gfx->GetDefaultCommandList());

		gfx->ReserveOnlineDescriptors(1024);
		ID3D12Device* device = gfx->GetDevice();
		RingOnlineDescriptorAllocator* online_descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
        for (size_t i = 0; i <= handle; ++i)
        {
            Texture* texture = texture_map[TextureHandle(i)].get();
            if (texture)
            {
                texture->CreateSRV();
				device->CopyDescriptorsSimple(1, online_descriptor_allocator->GetHandle(i),
                                              texture->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
        is_scene_initialized = true;
	}

	TextureHandle TextureManager::LoadDDSTexture(std::wstring const& texture_path)
    {
        if (auto it = loaded_textures.find(texture_path); it == loaded_textures.end())
        {
            ++handle;
            loaded_textures.insert({ texture_path, handle });

            auto device = gfx->GetDevice();
            auto cmd_list = gfx->GetDefaultCommandList();
            auto allocator = gfx->GetAllocator();

            Microsoft::WRL::ComPtr<ID3D12Resource> tex2d = nullptr;
            std::unique_ptr<uint8_t[]> decoded_data;
            std::vector<TextureInitialData> subresources;
			BREAK_IF_FAILED(
				DirectX::LoadDDSTextureFromFile(device, texture_path.data(), tex2d.GetAddressOf(),
					decoded_data, subresources));

            D3D12_RESOURCE_DESC _desc = tex2d->GetDesc();
            TextureDesc desc{};
            desc.type = ConvertTextureType(_desc.Dimension);
            desc.misc_flags = ETextureMiscFlag::None;
            desc.width = (uint32)_desc.Width;
            desc.height = _desc.Height;
            desc.array_size = _desc.DepthOrArraySize;
            desc.depth = _desc.DepthOrArraySize;
            desc.bind_flags = EBindFlag::ShaderResource;
			
            desc.format = ConvertDXGIFormat(_desc.Format);
            desc.initial_state = EResourceState::PixelShaderResource | EResourceState::NonPixelShaderResource;
            desc.heap_type = EResourceUsage::Default;
            desc.mip_levels = _desc.MipLevels;
            std::unique_ptr<Texture> tex = std::make_unique<Texture>(gfx, desc, subresources.data(), subresources.size());
            
            texture_map.insert({ handle, std::move(tex)});
            CreateViewForTexture(handle);
            return handle;
        }
        else return it->second;
    }

    TextureHandle TextureManager::LoadWICTexture(std::wstring const& texture_path)
    {
        if (auto it = loaded_textures.find(texture_path); it == loaded_textures.end())
        {
            auto device = gfx->GetDevice();
            auto cmd_list = gfx->GetDefaultCommandList();
            auto allocator = gfx->GetAllocator();

            ++handle;
            loaded_textures.insert({ texture_path, handle });

            Microsoft::WRL::ComPtr<ID3D12Resource> d3d12_tex = nullptr;
            std::unique_ptr<uint8_t[]> decoded_data;
            TextureInitialData subresource{};
            if (mipmaps)
            {
                BREAK_IF_FAILED(DirectX::LoadWICTextureFromFileEx(device, texture_path.data(), 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    DirectX::WIC_LOADER_MIP_RESERVE | DirectX::WIC_LOADER_IGNORE_SRGB | DirectX::WIC_LOADER_FORCE_RGBA32, &d3d12_tex,
                    decoded_data, subresource));
            }
            else
            {
                BREAK_IF_FAILED(DirectX::LoadWICTextureFromFile(device, texture_path.data(), d3d12_tex.GetAddressOf(),
                    decoded_data, subresource));
            }
			D3D12_RESOURCE_DESC _desc = d3d12_tex->GetDesc();
			TextureDesc desc{};
			desc.type = TextureType_2D;
			desc.misc_flags = ETextureMiscFlag::None;
			desc.width = (uint32)_desc.Width;
			desc.height = _desc.Height;
			desc.array_size = _desc.DepthOrArraySize;
			desc.depth = _desc.DepthOrArraySize;
            desc.bind_flags = EBindFlag::ShaderResource;
            if (mipmaps && _desc.MipLevels != 1)
            {
                desc.bind_flags |= EBindFlag::UnorderedAccess;
            }
			desc.format = ConvertDXGIFormat(_desc.Format);
			desc.initial_state = EResourceState::PixelShaderResource | EResourceState::NonPixelShaderResource;
			desc.heap_type = EResourceUsage::Default;
			desc.mip_levels = _desc.MipLevels;
			std::unique_ptr<Texture> tex = std::make_unique<Texture>(gfx, desc, &subresource, 1);

            texture_map.insert({ handle, std::move(tex)});
            if (mipmaps) mips_generator->Add(texture_map[handle]->GetNative());
            CreateViewForTexture(handle);
            return handle;
        }
        else return it->second;
    }

    TextureHandle TextureManager::LoadTexture_HDR_TGA_PIC(std::string const& texture_path)
    {
        if (auto it = loaded_textures.find(texture_path); it == loaded_textures.end())
        {
            auto device = gfx->GetDevice();
            auto cmd_list = gfx->GetDefaultCommandList();
            auto allocator = gfx->GetAllocator();
            ++handle;
			loaded_textures.insert({ texture_path, handle });
            Image img(texture_path, 4);

			TextureDesc desc{};
			desc.type = TextureType_2D;
			desc.misc_flags = ETextureMiscFlag::None;
			desc.width = img.Width();
			desc.height = img.Height();
			desc.array_size = 1;
			desc.depth = 1;
			desc.bind_flags = EBindFlag::ShaderResource;
			if (mipmaps)
			{
				desc.bind_flags |= EBindFlag::UnorderedAccess;
			}
			desc.format = img.IsHDR() ? EFormat::R32G32B32A32_FLOAT : EFormat::R8G8B8A8_UNORM;
			desc.initial_state = EResourceState::PixelShaderResource | EResourceState::NonPixelShaderResource;
			desc.heap_type = EResourceUsage::Default;
			desc.mip_levels = mipmaps ? 0 : 1;

			TextureInitialData data{};
			data.pData = img.Data<void>();
			data.RowPitch = img.Pitch();

			std::unique_ptr<Texture> tex = std::make_unique<Texture>(gfx, desc, &data);
            texture_map.insert({ handle, std::move(tex)});
            if (mipmaps) mips_generator->Add(texture_map[handle]->GetNative());
            CreateViewForTexture(handle);
            return handle;
        }
        else return it->second;

    }

	void TextureManager::CreateViewForTexture(TextureHandle handle)
	{
        if (!is_scene_initialized) return;

		ID3D12Device* device = gfx->GetDevice();
		RingOnlineDescriptorAllocator* online_descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
		Texture* texture = texture_map[handle].get();
		ADRIA_ASSERT(texture);
		texture->CreateSRV();
		device->CopyDescriptorsSimple(1, online_descriptor_allocator->GetHandle((size_t)handle),
			texture->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

}