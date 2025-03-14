/*
 *  Copyright 2023-2025 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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

#include "FenceWebGPUImpl.hpp"
#include "GraphicsAccessories.hpp"
#include "RenderDeviceWebGPUImpl.hpp"

namespace Diligent
{

FenceWebGPUImpl::FenceWebGPUImpl(IReferenceCounters*     pRefCounters,
                                 RenderDeviceWebGPUImpl* pDevice,
                                 const FenceDesc&        Desc) :
    TFenceBase{pRefCounters, pDevice, Desc}
{
    if (m_Desc.Type != FENCE_TYPE_CPU_WAIT_ONLY)
        LOG_ERROR_AND_THROW("Description of Fence '", m_Desc.Name, "' is invalid: ", GetFenceTypeString(m_Desc.Type), " is not supported in WebGPU.");
}

void FenceWebGPUImpl::ProcessSyncPoints()
{
    while (!m_SyncGroups.empty())
    {
        SyncPointGroup& SyncGroup  = m_SyncGroups.front();
        auto&           SyncPoints = SyncGroup.second;
        while (!SyncPoints.empty())
        {
            if (SyncPoints.front()->IsTriggered())
            {
                std::swap(SyncPoints.front(), SyncPoints.back());
                SyncPoints.pop_back();
            }
            else
            {
                break;
            }
        }

        if (SyncPoints.empty())
        {
            UpdateLastCompletedFenceValue(SyncGroup.first);
            m_SyncGroups.pop_front();
        }
        else
        {
            break;
        }
    }
}

Uint64 FenceWebGPUImpl::GetCompletedValue()
{
    ProcessSyncPoints();
    return m_LastCompletedFenceValue.load();
}

void FenceWebGPUImpl::Signal(Uint64 Value)
{
    DEV_ERROR("Signal() is not supported in WebGPU backend");
}

void FenceWebGPUImpl::Wait(Uint64 Value)
{
#if PLATFORM_WEB
    LOG_ERROR_MESSAGE("IFence::Wait() is not supported on the Web. Use non-blocking synchronization methods.");
#else
    while (GetCompletedValue() < Value)
        m_pDevice->DeviceTick();
#endif
}

void FenceWebGPUImpl::AppendSyncPoints(const std::vector<RefCntAutoPtr<SyncPointWebGPUImpl>>& SyncPoints, Uint64 Value)
{
    DEV_CHECK_ERR(m_SyncGroups.empty() || m_SyncGroups.back().first < Value, "Sync points must be appended in strictly increasing order");
    m_SyncGroups.emplace_back(std::make_pair(Value, SyncPoints));
    ProcessSyncPoints();
}

} // namespace Diligent
