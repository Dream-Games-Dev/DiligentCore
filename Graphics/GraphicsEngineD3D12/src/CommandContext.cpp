/*     Copyright 2015-2018 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "d3dx12_win.h"
#include "CommandContext.h"
#include "TextureD3D12Impl.h"
#include "BufferD3D12Impl.h"
#include "CommandListManager.h"
#include "D3D12TypeConversions.h"


namespace Diligent
{

CommandContext::CommandContext( CommandListManager& CmdListManager) :
	m_pCurGraphicsRootSignature (nullptr),
	m_pCurPipelineState         (nullptr),
	m_pCurComputeRootSignature  (nullptr),
    m_PendingResourceBarriers( STD_ALLOCATOR_RAW_MEM(D3D12_RESOURCE_BARRIER, GetRawAllocator(), "Allocator for vector<D3D12_RESOURCE_BARRIER>") )
{
    m_PendingResourceBarriers.reserve(MaxPendingBarriers);
    CmdListManager.CreateNewCommandList(&m_pCommandList, &m_pCurrentAllocator);
}

CommandContext::~CommandContext( void )
{
    DEV_CHECK_ERR(m_pCurrentAllocator == nullptr, "Command allocator must be released prior to destroying the command context");
}

void CommandContext::Reset( CommandListManager& CmdListManager )
{
	// We only call Reset() on previously freed contexts. The command list persists, but we need to
	// request a new allocator
	VERIFY_EXPR(m_pCommandList != nullptr);
    if( !m_pCurrentAllocator )
    {
        CmdListManager.RequestAllocator(&m_pCurrentAllocator);
        // Unlike ID3D12CommandAllocator::Reset, ID3D12GraphicsCommandList::Reset can be called while the 
        // command list is still being executed. A typical pattern is to submit a command list and then 
        // immediately reset it to reuse the allocated memory for another command list.
        m_pCommandList->Reset(m_pCurrentAllocator, nullptr);
    }

    m_pCurPipelineState = nullptr;
	m_pCurGraphicsRootSignature = nullptr;
	m_pCurComputeRootSignature = nullptr;
	m_PendingResourceBarriers.clear();
    m_BoundDescriptorHeaps = ShaderDescriptorHeaps();
    
    m_DynamicGPUDescriptorAllocators = nullptr;

    m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
#if 0
	BindDescriptorHeaps();
#endif
}

ID3D12GraphicsCommandList* CommandContext::Close(CComPtr<ID3D12CommandAllocator>& pAllocator)
{
	FlushResourceBarriers();

	//if (m_ID.length() > 0)
	//	EngineProfiling::EndBlock(this);

	VERIFY_EXPR(m_pCurrentAllocator != nullptr);
	auto hr = m_pCommandList->Close();
    DEV_CHECK_ERR(SUCCEEDED(hr), "Failed to close the command list");
    
    pAllocator = std::move(m_pCurrentAllocator);
    return m_pCommandList;
}

void GraphicsContext::SetRenderTargets( UINT NumRTVs, ITextureViewD3D12** ppRTVs, ITextureViewD3D12* pDSV, SET_RENDER_TARGETS_FLAGS Flags )
{
    D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[8]; // Do not waste time initializing array to zero

	for (UINT i = 0; i < NumRTVs; ++i)
	{
        auto *pRTV = ppRTVs[i];
        if( pRTV )
        {
            auto* pTexture = ValidatedCast<TextureD3D12Impl>( pRTV->GetTexture() );
            if (Flags & SET_RENDER_TARGETS_FLAG_TRANSITION_COLOR)
            {
                if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_RENDER_TARGET))
	                TransitionResource(pTexture, RESOURCE_STATE_RENDER_TARGET);
            }
#ifdef DEVELOPMENT
            else if (Flags & SET_RENDER_TARGETS_FLAG_VERIFY_STATES)
            {
                if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_RENDER_TARGET))
                {
                    LOG_ERROR_MESSAGE("Texture '", pTexture->GetDesc().Name, "' being set as render target at slot ", i, " is not transitioned to RESOURCE_STATE_RENDER_TARGET state. "
                                      "Actual texture state: ", GetResourceStateString(pTexture->GetState()), ". "
                                      "Use SET_RENDER_TARGETS_FLAG_TRANSITION_COLOR flag or explicitly transition the resource using IDeviceContext::TransitionResourceStates() method.");
                }
            }
#endif

		    RTVHandles[i] = pRTV->GetCPUDescriptorHandle();
            VERIFY_EXPR(RTVHandles[i].ptr != 0);
        }
	}

	if (pDSV)
	{
        auto* pTexture = ValidatedCast<TextureD3D12Impl>( pDSV->GetTexture() );
		//if (bReadOnlyDepth)
		//{
		//	TransitionResource(*pTexture, D3D12_RESOURCE_STATE_DEPTH_READ);
		//	m_pCommandList->OMSetRenderTargets( NumRTVs, RTVHandles, FALSE, &DSV->GetDSV_DepthReadOnly() );
		//}
		//else
		{
            if (Flags & SET_RENDER_TARGETS_FLAG_TRANSITION_DEPTH)
            {
                if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_DEPTH_WRITE))
			        TransitionResource(pTexture, RESOURCE_STATE_DEPTH_WRITE);
            }
#ifdef DEVELOPMENT
            else if (Flags & SET_RENDER_TARGETS_FLAG_VERIFY_STATES)
            {
                if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_DEPTH_WRITE))
                {
                    LOG_ERROR_MESSAGE("Texture '", pTexture->GetDesc().Name, "' being set as depth-stencil buffer is not transitioned to RESOURCE_STATE_DEPTH_WRITE state. "
                                      "Actual texture state: ", GetResourceStateString(pTexture->GetState()), ". "
                                      "Use SET_RENDER_TARGETS_FLAG_TRANSITION_DEPTH flag or explicitly transition the resource using IDeviceContext::TransitionResourceStates() method.");
                }

            }
#endif
            auto DSVHandle = pDSV->GetCPUDescriptorHandle();
            VERIFY_EXPR(DSVHandle.ptr != 0);
			m_pCommandList->OMSetRenderTargets( NumRTVs, RTVHandles, FALSE, &DSVHandle );
		}
	}
	else if(NumRTVs > 0)
	{
		m_pCommandList->OMSetRenderTargets( NumRTVs, RTVHandles, FALSE, nullptr );
	}
}

void CommandContext::ClearUAVFloat( ITextureViewD3D12* pTexView, const float* Color )
{
    auto* pTexture = ValidatedCast<TextureD3D12Impl>( pTexView->GetTexture() );
    if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_UNORDERED_ACCESS))
	    TransitionResource(pTexture, RESOURCE_STATE_UNORDERED_ACCESS);
    FlushResourceBarriers();
	// After binding a UAV, we can get a GPU handle that is required to clear it as a UAV (because it essentially runs
	// a shader to set all of the values).
    UNSUPPORTED("Not yet implemented");
    D3D12_GPU_DESCRIPTOR_HANDLE GpuVisibleHandle = {};//m_DynamicDescriptorHeap.UploadDirect(Target.GetUAV());
	m_pCommandList->ClearUnorderedAccessViewFloat(GpuVisibleHandle, pTexView->GetCPUDescriptorHandle(), pTexture->GetD3D12Resource(), Color, 0, nullptr);
}

void CommandContext::ClearUAVUint( ITextureViewD3D12* pTexView, const UINT* Color  )
{
    auto* pTexture = ValidatedCast<TextureD3D12Impl>( pTexView->GetTexture() );
    if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_UNORDERED_ACCESS))
	    TransitionResource(pTexture, RESOURCE_STATE_UNORDERED_ACCESS);

	// After binding a UAV, we can get a GPU handle that is required to clear it as a UAV (because it essentially runs
	// a shader to set all of the values).
    UNSUPPORTED("Not yet implemented");
    D3D12_GPU_DESCRIPTOR_HANDLE GpuVisibleHandle = {};//m_DynamicDescriptorHeap.UploadDirect(Target.GetUAV());
	//CD3DX12_RECT ClearRect(0, 0, (LONG)Target.GetWidth(), (LONG)Target.GetHeight());
    FlushResourceBarriers();
	//TODO: My Nvidia card is not clearing UAVs with either Float or Uint variants.
	m_pCommandList->ClearUnorderedAccessViewUint(GpuVisibleHandle, pTexView->GetCPUDescriptorHandle(), pTexture->GetD3D12Resource(), Color, 0, nullptr/*1, &ClearRect*/);
}


void GraphicsContext::ClearRenderTarget( ITextureViewD3D12* pRTV, const float* Color )
{
    auto *pTexture = ValidatedCast<TextureD3D12Impl>( pRTV->GetTexture() );
    if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_RENDER_TARGET))
	    TransitionResource(pTexture, RESOURCE_STATE_RENDER_TARGET);
    FlushResourceBarriers();
	m_pCommandList->ClearRenderTargetView(pRTV->GetCPUDescriptorHandle(), Color, 0, nullptr);
}

void GraphicsContext::ClearDepthStencil( ITextureViewD3D12* pDSV, D3D12_CLEAR_FLAGS ClearFlags, float Depth, UINT8 Stencil )
{
    auto *pTexture = ValidatedCast<TextureD3D12Impl>( pDSV->GetTexture() );
    if (pTexture->IsInKnownState() && !pTexture->CheckState(RESOURCE_STATE_DEPTH_WRITE))
	    TransitionResource( pTexture, RESOURCE_STATE_DEPTH_WRITE);
    FlushResourceBarriers();
	m_pCommandList->ClearDepthStencilView(pDSV->GetCPUDescriptorHandle(), ClearFlags, Depth, Stencil, 0, nullptr);
}


void CommandContext::TransitionResource(ITextureD3D12* pTexture, RESOURCE_STATE NewState)
{
    VERIFY_EXPR( pTexture != nullptr );
    auto* pTexD3D12 = ValidatedCast<TextureD3D12Impl>(pTexture);
    VERIFY(pTexD3D12->IsInKnownState(), "Texture state can't be unknown");
    StateTransitionDesc TextureBarrier(pTexture, RESOURCE_STATE_UNKNOWN, NewState, true);
    TransitionResource(TextureBarrier);
}

void CommandContext::TransitionResource(IBufferD3D12* pBuffer, RESOURCE_STATE NewState)
{
    VERIFY_EXPR( pBuffer != nullptr );
    auto* pBuffD3D12 = ValidatedCast<BufferD3D12Impl>(pBuffer);
    VERIFY(pBuffD3D12->IsInKnownState(), "Buffer state can't be unknown");
    StateTransitionDesc BufferBarrier(pBuffer, RESOURCE_STATE_UNKNOWN, NewState, true);
    TransitionResource(BufferBarrier);
}

void CommandContext::InsertUAVBarrier(ID3D12Resource* pd3d12Resource)
{
    m_PendingResourceBarriers.emplace_back();
	D3D12_RESOURCE_BARRIER& BarrierDesc = m_PendingResourceBarriers.back();
    // UAV barrier indicates that all UAV accesses (reads or writes) to a particular resource
    // must complete before any future UAV accesses (read or write) can begin.
	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	BarrierDesc.UAV.pResource = pd3d12Resource;
}


void CommandContext::TransitionResource(const StateTransitionDesc& Barrier)
{
    DEV_CHECK_ERR( (Barrier.pTexture != nullptr) ^ (Barrier.pBuffer != nullptr), "Exactly one of pTexture or pBuffer must not be null");
   
    DEV_CHECK_ERR(Barrier.NewState != RESOURCE_STATE_UNKNOWN, "New resource state can't be unknown");
    RESOURCE_STATE    OldState          = RESOURCE_STATE_UNKNOWN;
    ID3D12Resource*   pd3d12Resource    = nullptr;
    TextureD3D12Impl* pTextureD3D12Impl = nullptr;
    BufferD3D12Impl*  pBufferD3D12Impl  = nullptr;
    if (Barrier.pTexture)
    {
        pTextureD3D12Impl = ValidatedCast<TextureD3D12Impl>(Barrier.pTexture);
        pd3d12Resource = pTextureD3D12Impl->GetD3D12Resource();
        OldState = pTextureD3D12Impl->GetState();
    }
    else
    {
        VERIFY_EXPR(Barrier.pBuffer != nullptr);
        pBufferD3D12Impl = ValidatedCast<BufferD3D12Impl>(Barrier.pBuffer);
        pd3d12Resource = pBufferD3D12Impl->GetD3D12Resource();
        OldState = pBufferD3D12Impl->GetState();

#ifdef DEVELOPMENT
        // Dynamic buffers wtih no SRV/UAV bind flags are suballocated in 
        // the upload heap when Map() is called and must always be in 
        // D3D12_RESOURCE_STATE_GENERIC_READ state
        if (pBufferD3D12Impl->GetDesc().Usage == USAGE_DYNAMIC && (pBufferD3D12Impl->GetDesc().BindFlags & (BIND_SHADER_RESOURCE|BIND_UNORDERED_ACCESS)) == 0)
        {
            DEV_CHECK_ERR(pBufferD3D12Impl->GetState() == RESOURCE_STATE_GENERIC_READ, "Dynamic buffers that cannot be bound as SRV or UAV are expected to always be in D3D12_RESOURCE_STATE_GENERIC_READ state");
            VERIFY( (Barrier.NewState & RESOURCE_STATE_GENERIC_READ) == Barrier.NewState, "Dynamic buffers can only transition to one of RESOURCE_STATE_GENERIC_READ states");
        }
#endif
    }

    if (OldState == RESOURCE_STATE_UNKNOWN)
    {
        DEV_CHECK_ERR(Barrier.OldState != RESOURCE_STATE_UNKNOWN, "When resource state is unknown (which means it is managed by the app), OldState member must not be RESOURCE_STATE_UNKNOWN");
        OldState = Barrier.OldState;
    }
    else
    {
        DEV_CHECK_ERR(Barrier.OldState == RESOURCE_STATE_UNKNOWN || Barrier.OldState == OldState, "Resource state is known (", OldState, ") and does not match OldState (", Barrier.OldState, ") specified in resource barrier. Set OldState member to RESOURCE_STATE_UNKNOWN to make the engine use current resource state");
    }

    // Check if required state is already set
    if ((OldState & Barrier.NewState) != Barrier.NewState)
    {
        auto NewState = Barrier.NewState;
        // If both old state and new state are read-only states, combine the two
        if( (OldState & RESOURCE_STATE_GENERIC_READ) == OldState &&
            (NewState & RESOURCE_STATE_GENERIC_READ) == NewState )
            NewState = static_cast<RESOURCE_STATE>(OldState | NewState);

	    D3D12_RESOURCE_BARRIER BarrierDesc;
		BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		BarrierDesc.Transition.pResource   = pd3d12Resource;
		BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        BarrierDesc.Transition.StateBefore = ResourceStateFlagsToD3D12ResourceStates(OldState);
        BarrierDesc.Transition.StateAfter  = ResourceStateFlagsToD3D12ResourceStates(NewState);

        if (pTextureD3D12Impl)
        {
            const auto& TexDesc = pTextureD3D12Impl->GetDesc();
            VERIFY(Barrier.FirstMipLevel < TexDesc.MipLevels, "First mip level is out of range");
            VERIFY(Barrier.MipLevelsCount == StateTransitionDesc::RemainingMipLevels || Barrier.FirstMipLevel + Barrier.MipLevelsCount < TexDesc.MipLevels,
                   "Invalid mip level range ");
            VERIFY(Barrier.FirstArraySlice < TexDesc.ArraySize, "First array slice is out of range");
            VERIFY(Barrier.ArraySliceCount == StateTransitionDesc::RemainingArraySlices || Barrier.FirstArraySlice + Barrier.ArraySliceCount < TexDesc.ArraySize,
                   "Invalid array slice range ");

            if (Barrier.FirstMipLevel   == 0 && (Barrier.MipLevelsCount  == StateTransitionDesc::RemainingMipLevels   || Barrier.MipLevelsCount  == TexDesc.MipLevels) &&
                Barrier.FirstArraySlice == 0 && (Barrier.ArraySliceCount == StateTransitionDesc::RemainingArraySlices || Barrier.ArraySliceCount == TexDesc.ArraySize))
            {
                BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_PendingResourceBarriers.emplace_back(BarrierDesc);
            }
            else
            {
                Uint32 EndMip   = Barrier.MipLevelsCount  == StateTransitionDesc::RemainingMipLevels   ? TexDesc.MipLevels : Barrier.FirstMipLevel   + Barrier.MipLevelsCount;
                Uint32 EndSlice = Barrier.ArraySliceCount == StateTransitionDesc::RemainingArraySlices ? TexDesc.ArraySize : Barrier.FirstArraySlice + Barrier.ArraySliceCount;
                for(Uint32 mip = Barrier.FirstMipLevel; mip < EndMip; ++mip)
                {
                    for(Uint32 slice = Barrier.FirstArraySlice; slice < EndSlice; ++slice)
                    {
                        BarrierDesc.Transition.Subresource = D3D12CalcSubresource(mip, slice, 0, TexDesc.MipLevels, TexDesc.ArraySize);
                        m_PendingResourceBarriers.emplace_back(BarrierDesc);
                    }
                }
            }
        }
        else
            m_PendingResourceBarriers.emplace_back(BarrierDesc);

        if (pTextureD3D12Impl)
        {
            pTextureD3D12Impl->SetState(Barrier.UpdateResourceState ? NewState : RESOURCE_STATE_UNKNOWN);
        }
        else
        {
            VERIFY_EXPR(pBufferD3D12Impl);

            pBufferD3D12Impl->SetState(Barrier.UpdateResourceState ? NewState : RESOURCE_STATE_UNKNOWN);

            if (pBufferD3D12Impl->GetDesc().Usage == USAGE_DYNAMIC && (pBufferD3D12Impl->GetDesc().BindFlags & (BIND_SHADER_RESOURCE|BIND_UNORDERED_ACCESS)) == 0)
                VERIFY(pBufferD3D12Impl->GetState() == RESOURCE_STATE_GENERIC_READ, "Dynamic buffers without SRV/UAV bind flag are expected to never transition from RESOURCE_STATE_GENERIC_READ state");
        }
    }

    if (OldState == RESOURCE_STATE_UNORDERED_ACCESS && Barrier.NewState == RESOURCE_STATE_UNORDERED_ACCESS)
    {
        InsertUAVBarrier(pd3d12Resource);
    }

	if (m_PendingResourceBarriers.size() >= MaxPendingBarriers)
        FlushResourceBarriers();
}

void CommandContext::InsertAliasBarrier(D3D12ResourceBase& Before, D3D12ResourceBase& After, bool FlushImmediate)
{
    m_PendingResourceBarriers.emplace_back();
	D3D12_RESOURCE_BARRIER& BarrierDesc = m_PendingResourceBarriers.back();

	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	BarrierDesc.Aliasing.pResourceBefore = Before.GetD3D12Resource();
	BarrierDesc.Aliasing.pResourceAfter = After.GetD3D12Resource();

	if (FlushImmediate)
        FlushResourceBarriers();
}

}
