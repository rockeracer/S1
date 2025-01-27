#include "imgui.h"
#include "DX12GUI.h"

#include "GUIRender.h"

MTDX12GUI::MTDX12GUI()
{
}

void* MTDX12GUI::Init(const ComPtr<ID3D12Device>& DX12Device, const ComPtr<ID3D12GraphicsCommandList>& CommandList, MTInt32 RenderFrameCount, const MTGUIFontTextureDesc& FontTextureDesc, const MTGUIVertexDesc& VertexDesc)
{
	m_DX12Device = DX12Device;
	m_CommandList = CommandList;
	m_numFramesInFlight = RenderFrameCount;

	// Create the root signature
	{
		D3D12_DESCRIPTOR_RANGE descRange = {};
		descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descRange.NumDescriptors = 1;
		descRange.BaseShaderRegister = 0;
		descRange.RegisterSpace = 0;
		descRange.OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER param[2] = {};

		param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		param[0].Constants.ShaderRegister = 0;
		param[0].Constants.RegisterSpace = 0;
		param[0].Constants.Num32BitValues = 16;
		param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param[1].DescriptorTable.NumDescriptorRanges = 1;
		param[1].DescriptorTable.pDescriptorRanges = &descRange;
		param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC staticSampler = {};
		staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		staticSampler.MipLODBias = 0.f;
		staticSampler.MaxAnisotropy = 0;
		staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		staticSampler.MinLOD = 0.f;
		staticSampler.MaxLOD = 0.f;
		staticSampler.ShaderRegister = 0;
		staticSampler.RegisterSpace = 0;
		staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(param);
		desc.pParameters = param;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &staticSampler;
		desc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		ID3DBlob* blob = NULL;
		if (D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL) != S_OK)
		{
			return false;
		}

		m_DX12Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature));
		blob->Release();
	}

	// By using D3DCompile() from <d3dcompiler.h> / d3dcompiler.lib, we introduce a dependency to a given version of d3dcompiler_XX.dll (see D3DCOMPILER_DLL_A)
	// If you would like to use this DX12 sample code but remove this dependency you can: 
	//  1) compile once, save the compiled shader blobs into a file or source code and pass them to CreateVertexShader()/CreatePixelShader() [preferred solution]
	//  2) use code to detect any version of the DLL and grab a pointer to D3DCompile from the DLL. 
	// See https://github.com/ocornut/imgui/pull/638 for sources and details.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	memset(&psoDesc, 0, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.NodeMask = 1;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.pRootSignature = m_RootSignature.Get();
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	// Create the vertex shader
	{
		static const char* vertexShader =
			"cbuffer vertexBuffer : register(b0) \
			{\
				float4x4 ProjectionMatrix; \
			};\
			struct VS_INPUT\
			{\
				float2 pos : POSITION;\
				float4 col : COLOR0;\
				float2 uv  : TEXCOORD0;\
			};\
			\
			struct PS_INPUT\
			{\
				float4 pos : SV_POSITION;\
				float4 col : COLOR0;\
				float2 uv  : TEXCOORD0;\
			};\
			\
			PS_INPUT main(VS_INPUT input)\
			{\
				PS_INPUT output;\
				output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
				output.col = input.col;\
				output.uv  = input.uv;\
				return output;\
			}";

		D3DCompile(vertexShader, strlen(vertexShader), NULL, NULL, NULL, "main", "vs_5_0", 0, 0, &m_VertexShaderBlob, NULL);
		if (m_VertexShaderBlob.Get() == nullptr) // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
		{
			return false;
		}
		psoDesc.VS = { m_VertexShaderBlob->GetBufferPointer(), m_VertexShaderBlob->GetBufferSize() };

		// Create the input layout
		static D3D12_INPUT_ELEMENT_DESC local_layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (size_t)(&((ImDrawVert*)0)->pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (size_t)(&((ImDrawVert*)0)->uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (size_t)(&((ImDrawVert*)0)->col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		psoDesc.InputLayout = { local_layout, 3 };
	}

	// Create the pixel shader
	{
		static const char* pixelShader =
			"struct PS_INPUT\
			{\
				float4 pos : SV_POSITION;\
				float4 col : COLOR0;\
				float2 uv  : TEXCOORD0;\
			};\
			SamplerState sampler0 : register(s0);\
			Texture2D texture0 : register(t0);\
			\
			float4 main(PS_INPUT input) : SV_Target\
			{\
				float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
				return out_col; \
			}";

		D3DCompile(pixelShader, strlen(pixelShader), NULL, NULL, NULL, "main", "ps_5_0", 0, 0, &m_PixelShaderBlob, NULL);
		if (m_PixelShaderBlob.Get() == nullptr)  // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
		{
			return false;
		}
		psoDesc.PS = { m_PixelShaderBlob->GetBufferPointer(), m_PixelShaderBlob->GetBufferSize() };
	}

	// Create the blending setup
	{
		D3D12_BLEND_DESC& desc = psoDesc.BlendState;
		desc.AlphaToCoverageEnable = false;
		desc.RenderTarget[0].BlendEnable = true;
		desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}

	// Create the rasterizer state
	{
		D3D12_RASTERIZER_DESC& desc = psoDesc.RasterizerState;
		desc.FillMode = D3D12_FILL_MODE_SOLID;
		desc.CullMode = D3D12_CULL_MODE_NONE;
		desc.FrontCounterClockwise = FALSE;
		desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		desc.DepthClipEnable = true;
		desc.MultisampleEnable = FALSE;
		desc.AntialiasedLineEnable = FALSE;
		desc.ForcedSampleCount = 0;
		desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	}

	// Create depth-stencil State
	{
		D3D12_DEPTH_STENCIL_DESC& desc = psoDesc.DepthStencilState;
		desc.DepthEnable = false;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.StencilEnable = false;
		desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		desc.BackFace = desc.FrontFace;
	}

	if (m_DX12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)) != S_OK)
	{
		return false;
	}

	m_FrameResources = new FrameResources[m_numFramesInFlight];

	return CreateFontsTexture(FontTextureDesc);
}

void MTDX12GUI::Destroy()
{
	if (m_FrameResources)
	{
		delete[] m_FrameResources;
	}
}

void MTDX12GUI::RenderGUI(MTRDIWindowView* View, ImDrawData* DrawData)
{
	// FIXME: I'm assuming that this only gets called once per frame!
	// If not, we can't just re-allocate the IB or VB, we'll have to do a proper allocator.
	m_FrameIndex = m_FrameIndex + 1;
	FrameResources* frameResources = &m_FrameResources[m_FrameIndex % m_numFramesInFlight];
	ID3D12Resource* g_pVB = frameResources->VB.Get();
	ID3D12Resource* g_pIB = frameResources->IB.Get();
	int g_VertexBufferSize = frameResources->VertexBufferSize;
	int g_IndexBufferSize = frameResources->IndexBufferSize;

	// Create and grow vertex/index buffers if needed
	if (!g_pVB || g_VertexBufferSize < DrawData->TotalVtxCount)
	{
		if (g_pVB) { g_pVB->Release(); g_pVB = NULL; }
		g_VertexBufferSize = DrawData->TotalVtxCount + 5000;
		D3D12_HEAP_PROPERTIES props;
		memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		D3D12_RESOURCE_DESC desc;
		memset(&desc, 0, sizeof(D3D12_RESOURCE_DESC));
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = g_VertexBufferSize * sizeof(ImDrawVert);
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		if (m_DX12Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&g_pVB)) < 0)
		{
			return;
		}
		frameResources->VB = g_pVB;
		frameResources->VertexBufferSize = g_VertexBufferSize;
	}

	if (!g_pIB || g_IndexBufferSize < DrawData->TotalIdxCount)
	{
		if (g_pIB)
		{
			g_pIB->Release();
			g_pIB = NULL;
		}
		g_IndexBufferSize = DrawData->TotalIdxCount + 10000;
		D3D12_HEAP_PROPERTIES props;
		memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		D3D12_RESOURCE_DESC desc;
		memset(&desc, 0, sizeof(D3D12_RESOURCE_DESC));
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = g_IndexBufferSize * sizeof(ImDrawIdx);
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		if (m_DX12Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&g_pIB)) < 0)
		{
			return;
		}
		frameResources->IB = g_pIB;
		frameResources->IndexBufferSize = g_IndexBufferSize;
	}

	// Copy and convert all vertices into a single contiguous buffer
	void* vtx_resource, *idx_resource;
	D3D12_RANGE range;
	memset(&range, 0, sizeof(D3D12_RANGE));
	if (g_pVB->Map(0, &range, &vtx_resource) != S_OK)
	{
		return;
	}
	if (g_pIB->Map(0, &range, &idx_resource) != S_OK)
	{
		return;
	}
	ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource;
	ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource;
	for (int n = 0; n < DrawData->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = DrawData->CmdLists[n];
		memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vtx_dst += cmd_list->VtxBuffer.Size;
		idx_dst += cmd_list->IdxBuffer.Size;
	}
	g_pVB->Unmap(0, &range);
	g_pIB->Unmap(0, &range);

	// Setup orthographic projection matrix into our constant buffer
	// Our visible imgui space lies from DrawData->DisplayPos (top left) to DrawData->DisplayPos+data_data->DisplaySize (bottom right). 
	VERTEX_CONSTANT_BUFFER vertex_constant_buffer;
	{
		VERTEX_CONSTANT_BUFFER* constant_buffer = &vertex_constant_buffer;
		float L = DrawData->DisplayPos.x;
		float R = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
		float T = DrawData->DisplayPos.y;
		float B = DrawData->DisplayPos.y + DrawData->DisplaySize.y;
		float mvp[4][4] =
		{
			{ 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
			{ 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
			{ 0.0f,         0.0f,           0.5f,       0.0f },
			{ (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
		};
		memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
	}

	// Setup viewport
	D3D12_VIEWPORT vp;
	memset(&vp, 0, sizeof(D3D12_VIEWPORT));
	vp.Width = DrawData->DisplaySize.x;
	vp.Height = DrawData->DisplaySize.y;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = vp.TopLeftY = 0.0f;
	m_CommandList->RSSetViewports(1, &vp);

	// Bind shader and vertex buffers
	unsigned int stride = sizeof(ImDrawVert);
	unsigned int offset = 0;
	D3D12_VERTEX_BUFFER_VIEW vbv;
	memset(&vbv, 0, sizeof(D3D12_VERTEX_BUFFER_VIEW));
	vbv.BufferLocation = g_pVB->GetGPUVirtualAddress() + offset;
	vbv.SizeInBytes = g_VertexBufferSize * stride;
	vbv.StrideInBytes = stride;
	m_CommandList->IASetVertexBuffers(0, 1, &vbv);
	D3D12_INDEX_BUFFER_VIEW ibv;
	memset(&ibv, 0, sizeof(D3D12_INDEX_BUFFER_VIEW));
	ibv.BufferLocation = g_pIB->GetGPUVirtualAddress();
	ibv.SizeInBytes = g_IndexBufferSize * sizeof(ImDrawIdx);
	ibv.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	m_CommandList->IASetIndexBuffer(&ibv);
	m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_CommandList->SetPipelineState(m_PipelineState.Get());
	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());
	m_CommandList->SetGraphicsRoot32BitConstants(0, 16, &vertex_constant_buffer, 0);
	ID3D12DescriptorHeap* ppHeaps[] = { m_FontDescHeap.Get() };
	m_CommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// Setup render state
	const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_CommandList->OMSetBlendFactor(blend_factor);

	// Render command lists
	int vtx_offset = 0;
	int idx_offset = 0;
	ImVec2 pos = DrawData->DisplayPos;
	for (int n = 0; n < DrawData->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = DrawData->CmdLists[n];
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				const D3D12_RECT r = { (LONG)(pcmd->ClipRect.x - pos.x), (LONG)(pcmd->ClipRect.y - pos.y), (LONG)(pcmd->ClipRect.z - pos.x), (LONG)(pcmd->ClipRect.w - pos.y) };
				m_CommandList->SetGraphicsRootDescriptorTable(1, *(D3D12_GPU_DESCRIPTOR_HANDLE*)&pcmd->TextureId);
				m_CommandList->RSSetScissorRects(1, &r);
				m_CommandList->DrawIndexedInstanced(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
			}
			idx_offset += pcmd->ElemCount;
		}
		vtx_offset += cmd_list->VtxBuffer.Size;
	}
}

void MTDX12GUI::BeginRender(MTRDIWindowView* View)
{
}

void MTDX12GUI::EndRender(MTRDIWindowView* View)
{
}

void* MTDX12GUI::CreateFontsTexture(const MTGUIFontTextureDesc& FontTextureDesc)
{
	// Upload texture to graphics system
	{
		D3D12_HEAP_PROPERTIES props;
		memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Alignment = 0;
		desc.Width = FontTextureDesc.Width;
		desc.Height = FontTextureDesc.Height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		ID3D12Resource* pTexture = NULL;
		m_DX12Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&pTexture));

		UINT uploadPitch = (FontTextureDesc.Width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		UINT uploadSize = FontTextureDesc.Height * uploadPitch;
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = uploadSize;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		ID3D12Resource* uploadBuffer = NULL;
		HRESULT hr = m_DX12Device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&uploadBuffer));
		IM_ASSERT(SUCCEEDED(hr));

		void* mapped = NULL;
		D3D12_RANGE range = { 0, uploadSize };
		hr = uploadBuffer->Map(0, &range, &mapped);
		IM_ASSERT(SUCCEEDED(hr));
		for (int y = 0; y < FontTextureDesc.Height; y++)
		{
			memcpy((void*)((uintptr_t)mapped + y * uploadPitch), FontTextureDesc.Pixels + y * FontTextureDesc.Width * 4, FontTextureDesc.Width * 4);
		}
		uploadBuffer->Unmap(0, &range);

		D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
		srcLocation.pResource = uploadBuffer;
		srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srcLocation.PlacedFootprint.Footprint.Width = FontTextureDesc.Width;
		srcLocation.PlacedFootprint.Footprint.Height = FontTextureDesc.Height;
		srcLocation.PlacedFootprint.Footprint.Depth = 1;
		srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;

		D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
		dstLocation.pResource = pTexture;
		dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLocation.SubresourceIndex = 0;

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = pTexture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		ID3D12Fence* fence = NULL;
		hr = m_DX12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		IM_ASSERT(SUCCEEDED(hr));

		HANDLE event = CreateEvent(0, 0, 0, 0);
		IM_ASSERT(event != NULL);

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 1;

		ID3D12CommandQueue* cmdQueue = NULL;
		hr = m_DX12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
		IM_ASSERT(SUCCEEDED(hr));

		ID3D12CommandAllocator* cmdAlloc = NULL;
		hr = m_DX12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
		IM_ASSERT(SUCCEEDED(hr));

		ID3D12GraphicsCommandList* cmdList = NULL;
		hr = m_DX12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
		IM_ASSERT(SUCCEEDED(hr));

		cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, NULL);
		cmdList->ResourceBarrier(1, &barrier);

		hr = cmdList->Close();
		IM_ASSERT(SUCCEEDED(hr));

		cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&cmdList);
		hr = cmdQueue->Signal(fence, 1);
		IM_ASSERT(SUCCEEDED(hr));

		fence->SetEventOnCompletion(1, event);
		WaitForSingleObject(event, INFINITE);

		cmdList->Release();
		cmdAlloc->Release();
		cmdQueue->Release();
		CloseHandle(event);
		fence->Release();
		uploadBuffer->Release();

		// Create texture view
		D3D12_DESCRIPTOR_HEAP_DESC FontHeapDesc = {};
		FontHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		FontHeapDesc.NumDescriptors = 1;
		FontHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_DX12Device->CreateDescriptorHeap(&FontHeapDesc, IID_PPV_ARGS(&m_FontDescHeap)));
		m_FontSrvCPUDescHandle = m_FontDescHeap->GetCPUDescriptorHandleForHeapStart();
		m_FontSrvGPUDescHandle = m_FontDescHeap->GetGPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		m_DX12Device->CreateShaderResourceView(pTexture, &srvDesc, m_FontSrvCPUDescHandle);
		m_FontTextureResource.Reset();
		m_FontTextureResource = pTexture;
	}

	// Store our identifier
	static_assert(sizeof(ImTextureID) >= sizeof(m_FontSrvGPUDescHandle.ptr), "Can't pack descriptor handle into TexID, 32-bit not supported yet.");
	return (void*)m_FontSrvGPUDescHandle.ptr;
}
