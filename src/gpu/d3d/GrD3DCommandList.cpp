/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/d3d/GrD3DCommandList.h"

#include "src/gpu/GrScissorState.h"
#include "src/gpu/d3d/GrD3DBuffer.h"
#include "src/gpu/d3d/GrD3DCommandSignature.h"
#include "src/gpu/d3d/GrD3DGpu.h"
#include "src/gpu/d3d/GrD3DPipelineState.h"
#include "src/gpu/d3d/GrD3DRenderTarget.h"
#include "src/gpu/d3d/GrD3DStencilAttachment.h"
#include "src/gpu/d3d/GrD3DTexture.h"
#include "src/gpu/d3d/GrD3DTextureResource.h"
#include "src/gpu/d3d/GrD3DUtil.h"

GrD3DCommandList::GrD3DCommandList(gr_cp<ID3D12CommandAllocator> allocator,
                                   gr_cp<ID3D12GraphicsCommandList> commandList)
    : fCommandList(std::move(commandList))
    , fAllocator(std::move(allocator)) {
}

bool GrD3DCommandList::close() {
    SkASSERT(fIsActive);
    this->submitResourceBarriers();
    HRESULT hr = fCommandList->Close();
    SkDEBUGCODE(fIsActive = false;)
    return SUCCEEDED(hr);
}

GrD3DCommandList::SubmitResult GrD3DCommandList::submit(ID3D12CommandQueue* queue) {
    SkASSERT(fIsActive);
    if (!this->hasWork()) {
        this->callFinishedCallbacks();
        return SubmitResult::kNoWork;
    }

    if (!this->close()) {
        return SubmitResult::kFailure;
    }
    SkASSERT(!fIsActive);
    ID3D12CommandList* ppCommandLists[] = { fCommandList.get() };
    queue->ExecuteCommandLists(1, ppCommandLists);

    return SubmitResult::kSuccess;
}

void GrD3DCommandList::reset() {
    SkASSERT(!fIsActive);
    GR_D3D_CALL_ERRCHECK(fAllocator->Reset());
    GR_D3D_CALL_ERRCHECK(fCommandList->Reset(fAllocator.get(), nullptr));
    this->onReset();

    this->releaseResources();

    SkDEBUGCODE(fIsActive = true;)
    fHasWork = false;
}

void GrD3DCommandList::releaseResources() {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);
    if (fTrackedResources.count() == 0 && fTrackedRecycledResources.count() == 0) {
        return;
    }
    SkASSERT(!fIsActive);
    for (int i = 0; i < fTrackedResources.count(); ++i) {
        fTrackedResources[i]->notifyFinishedWithWorkOnGpu();
    }
    for (int i = 0; i < fTrackedRecycledResources.count(); ++i) {
        fTrackedRecycledResources[i]->notifyFinishedWithWorkOnGpu();
        auto resource = fTrackedRecycledResources[i].release();
        resource->recycle();
    }

    fTrackedResources.reset();
    fTrackedRecycledResources.reset();
    fTrackedGpuBuffers.reset();

    this->callFinishedCallbacks();
}

void GrD3DCommandList::addFinishedCallback(sk_sp<GrRefCntedCallback> callback) {
    fFinishedCallbacks.push_back(std::move(callback));
}

////////////////////////////////////////////////////////////////////////////////
// GraphicsCommandList commands
////////////////////////////////////////////////////////////////////////////////

void GrD3DCommandList::resourceBarrier(sk_sp<GrManagedResource> resource,
                                       int numBarriers,
                                       const D3D12_RESOURCE_TRANSITION_BARRIER* barriers) {
    SkASSERT(fIsActive);
    SkASSERT(barriers);
    for (int i = 0; i < numBarriers; ++i) {
        // D3D will apply barriers in order so we can just add onto the end
        D3D12_RESOURCE_BARRIER& newBarrier = fResourceBarriers.push_back();
        newBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        newBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        newBarrier.Transition = barriers[i];
    }

    fHasWork = true;
    if (resource) {
        this->addResource(std::move(resource));
    }
}

void GrD3DCommandList::submitResourceBarriers() {
    SkASSERT(fIsActive);

    if (fResourceBarriers.count()) {
        fCommandList->ResourceBarrier(fResourceBarriers.count(), fResourceBarriers.begin());
        fResourceBarriers.reset();
    }
    SkASSERT(!fResourceBarriers.count());
}

void GrD3DCommandList::copyBufferToTexture(ID3D12Resource* srcBuffer,
                                           const GrD3DTextureResource* dstTexture,
                                           uint32_t subresourceCount,
                                           D3D12_PLACED_SUBRESOURCE_FOOTPRINT* bufferFootprints,
                                           int left, int top) {
    SkASSERT(fIsActive);
    SkASSERT(subresourceCount == 1 || (left == 0 && top == 0));

    this->addingWork();
    this->addResource(dstTexture->resource());

    for (uint32_t subresource = 0; subresource < subresourceCount; ++subresource) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = srcBuffer;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = bufferFootprints[subresource];

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = dstTexture->d3dResource();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        fCommandList->CopyTextureRegion(&dst, left, top, 0, &src, nullptr);
    }
}

void GrD3DCommandList::copyTextureRegionToTexture(sk_sp<GrManagedResource> dst,
                                                  const D3D12_TEXTURE_COPY_LOCATION* dstLocation,
                                                  UINT dstX, UINT dstY,
                                                  sk_sp<GrManagedResource> src,
                                                  const D3D12_TEXTURE_COPY_LOCATION* srcLocation,
                                                  const D3D12_BOX* srcBox) {
    SkASSERT(fIsActive);
    SkASSERT(dst);
    this->addingWork();
    this->addResource(dst);
    this->addResource(std::move(src));
    fCommandList->CopyTextureRegion(dstLocation, dstX, dstY, 0, srcLocation, srcBox);
}

void GrD3DCommandList::copyTextureRegionToBuffer(sk_sp<const GrBuffer> dst,
                                                 const D3D12_TEXTURE_COPY_LOCATION* dstLocation,
                                                 UINT dstX,
                                                 UINT dstY,
                                                 sk_sp<GrManagedResource> src,
                                                 const D3D12_TEXTURE_COPY_LOCATION* srcLocation,
                                                 const D3D12_BOX* srcBox) {
    SkASSERT(fIsActive);
    SkASSERT(dst);
    this->addingWork();
    this->addGrBuffer(std::move(dst));
    this->addResource(std::move(src));
    fCommandList->CopyTextureRegion(dstLocation, dstX, dstY, 0, srcLocation, srcBox);
}

void GrD3DCommandList::copyBufferToBuffer(sk_sp<GrD3DBuffer> dst, uint64_t dstOffset,
                                          ID3D12Resource* srcBuffer, uint64_t srcOffset,
                                          uint64_t numBytes) {
    SkASSERT(fIsActive);

    this->addingWork();
    ID3D12Resource* dstBuffer = dst->d3dResource();
    uint64_t dstSize = dstBuffer->GetDesc().Width;
    uint64_t srcSize = srcBuffer->GetDesc().Width;
    if (dstSize == srcSize && srcSize == numBytes) {
        fCommandList->CopyResource(dstBuffer, srcBuffer);
    } else {
        fCommandList->CopyBufferRegion(dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
    }
    this->addGrBuffer(std::move(dst));
}

void GrD3DCommandList::addingWork() {
    this->submitResourceBarriers();
    fHasWork = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<GrD3DDirectCommandList> GrD3DDirectCommandList::Make(ID3D12Device* device) {
    gr_cp<ID3D12CommandAllocator> allocator;
    GR_D3D_CALL_ERRCHECK(device->CreateCommandAllocator(
                         D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));

    gr_cp<ID3D12GraphicsCommandList> commandList;
    GR_D3D_CALL_ERRCHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   allocator.get(), nullptr,
                                                   IID_PPV_ARGS(&commandList)));

    auto grCL = new GrD3DDirectCommandList(std::move(allocator), std::move(commandList));
    return std::unique_ptr<GrD3DDirectCommandList>(grCL);
}

GrD3DDirectCommandList::GrD3DDirectCommandList(gr_cp<ID3D12CommandAllocator> allocator,
                                               gr_cp<ID3D12GraphicsCommandList> commandList)
    : GrD3DCommandList(std::move(allocator), std::move(commandList))
    , fCurrentPipelineState(nullptr)
    , fCurrentRootSignature(nullptr)
    , fCurrentVertexBuffer(nullptr)
    , fCurrentVertexStride(0)
    , fCurrentInstanceBuffer(nullptr)
    , fCurrentInstanceStride(0)
    , fCurrentIndexBuffer(nullptr)
    , fCurrentConstantBufferAddress(0)
    , fCurrentSRVCRVDescriptorHeap(nullptr)
    , fCurrentSamplerDescriptorHeap(nullptr) {
    sk_bzero(fCurrentRootDescriptorTable, sizeof(fCurrentRootDescriptorTable));
}

void GrD3DDirectCommandList::onReset() {
    fCurrentPipelineState = nullptr;
    fCurrentRootSignature = nullptr;
    fCurrentVertexBuffer = nullptr;
    fCurrentVertexStride = 0;
    fCurrentInstanceBuffer = nullptr;
    fCurrentInstanceStride = 0;
    fCurrentIndexBuffer = nullptr;
    fCurrentConstantBufferAddress = 0;
    sk_bzero(fCurrentRootDescriptorTable, sizeof(fCurrentRootDescriptorTable));
    fCurrentSRVCRVDescriptorHeap = nullptr;
    fCurrentSamplerDescriptorHeap = nullptr;
}

void GrD3DDirectCommandList::setPipelineState(sk_sp<GrD3DPipelineState> pipelineState) {
    SkASSERT(fIsActive);
    if (pipelineState.get() != fCurrentPipelineState) {
        fCommandList->SetPipelineState(pipelineState->pipelineState());
        this->addResource(std::move(pipelineState));
        fCurrentPipelineState = pipelineState.get();
        this->setDefaultSamplePositions();
    }
}

void GrD3DDirectCommandList::setStencilRef(unsigned int stencilRef) {
    SkASSERT(fIsActive);
    fCommandList->OMSetStencilRef(stencilRef);
}

void GrD3DDirectCommandList::setBlendFactor(const float blendFactor[4]) {
    SkASSERT(fIsActive);
    fCommandList->OMSetBlendFactor(blendFactor);
}

void GrD3DDirectCommandList::setPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    SkASSERT(fIsActive);
    fCommandList->IASetPrimitiveTopology(primitiveTopology);
}

void GrD3DDirectCommandList::setScissorRects(unsigned int numRects, const D3D12_RECT* rects) {
    SkASSERT(fIsActive);
    fCommandList->RSSetScissorRects(numRects, rects);
}

void GrD3DDirectCommandList::setViewports(unsigned int numViewports,
                                          const D3D12_VIEWPORT* viewports) {
    SkASSERT(fIsActive);
    fCommandList->RSSetViewports(numViewports, viewports);
}

void GrD3DDirectCommandList::setCenteredSamplePositions(unsigned int numSamples) {
    if (!fUsingCenteredSamples && numSamples > 1) {
        gr_cp<ID3D12GraphicsCommandList1> commandList1;
        GR_D3D_CALL_ERRCHECK(fCommandList->QueryInterface(IID_PPV_ARGS(&commandList1)));
        static D3D12_SAMPLE_POSITION kCenteredSampleLocations[16] = {};
        commandList1->SetSamplePositions(numSamples, 1, kCenteredSampleLocations);
        fUsingCenteredSamples = true;
    }
}

void GrD3DDirectCommandList::setDefaultSamplePositions() {
    if (fUsingCenteredSamples) {
        gr_cp<ID3D12GraphicsCommandList1> commandList1;
        GR_D3D_CALL_ERRCHECK(fCommandList->QueryInterface(IID_PPV_ARGS(&commandList1)));
        commandList1->SetSamplePositions(0, 0, nullptr);
        fUsingCenteredSamples = false;
    }
}

void GrD3DDirectCommandList::setGraphicsRootSignature(const sk_sp<GrD3DRootSignature>& rootSig) {
    SkASSERT(fIsActive);
    if (fCurrentRootSignature != rootSig.get()) {
        fCommandList->SetGraphicsRootSignature(rootSig->rootSignature());
        this->addResource(rootSig);
        fCurrentRootSignature = rootSig.get();
        // need to reset the current descriptor tables as well
        sk_bzero(fCurrentRootDescriptorTable, sizeof(fCurrentRootDescriptorTable));
    }
}

void GrD3DDirectCommandList::setVertexBuffers(unsigned int startSlot,
                                              sk_sp<const GrBuffer> vertexBuffer,
                                              size_t vertexStride,
                                              sk_sp<const GrBuffer> instanceBuffer,
                                              size_t instanceStride) {
    if (fCurrentVertexBuffer != vertexBuffer.get() ||
        fCurrentVertexStride != vertexStride ||
        fCurrentInstanceBuffer != instanceBuffer.get() ||
        fCurrentInstanceStride != instanceStride) {

        fCurrentVertexBuffer = vertexBuffer.get();
        fCurrentVertexStride = vertexStride;
        fCurrentInstanceBuffer = instanceBuffer.get();
        fCurrentInstanceStride = instanceStride;

        D3D12_VERTEX_BUFFER_VIEW views[2];
        int numViews = 0;
        if (vertexBuffer) {
            auto* d3dBuffer = static_cast<const GrD3DBuffer*>(vertexBuffer.get());
            views[numViews].BufferLocation = d3dBuffer->d3dResource()->GetGPUVirtualAddress();
            views[numViews].SizeInBytes = vertexBuffer->size();
            views[numViews++].StrideInBytes = vertexStride;
            this->addGrBuffer(std::move(vertexBuffer));
        }
        if (instanceBuffer) {
            auto* d3dBuffer = static_cast<const GrD3DBuffer*>(instanceBuffer.get());
            views[numViews].BufferLocation = d3dBuffer->d3dResource()->GetGPUVirtualAddress();
            views[numViews].SizeInBytes = instanceBuffer->size();
            views[numViews++].StrideInBytes = instanceStride;
            this->addGrBuffer(std::move(instanceBuffer));
        }
        fCommandList->IASetVertexBuffers(startSlot, numViews, views);
    }
}

void GrD3DDirectCommandList::setIndexBuffer(sk_sp<const GrBuffer> indexBuffer) {
    if (fCurrentIndexBuffer != indexBuffer.get()) {
        auto* d3dBuffer = static_cast<const GrD3DBuffer*>(indexBuffer.get());

        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = d3dBuffer->d3dResource()->GetGPUVirtualAddress();
        view.SizeInBytes = indexBuffer->size();
        view.Format = DXGI_FORMAT_R16_UINT;
        fCommandList->IASetIndexBuffer(&view);

        fCurrentIndexBuffer = indexBuffer.get();
        this->addGrBuffer(std::move(indexBuffer));
    }
}

void GrD3DDirectCommandList::drawInstanced(unsigned int vertexCount, unsigned int instanceCount,
                                           unsigned int startVertex, unsigned int startInstance) {
    SkASSERT(fIsActive);
    this->addingWork();
    fCommandList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void GrD3DDirectCommandList::drawIndexedInstanced(unsigned int indexCount,
                                                  unsigned int instanceCount,
                                                  unsigned int startIndex,
                                                  unsigned int baseVertex,
                                                  unsigned int startInstance) {
    SkASSERT(fIsActive);
    this->addingWork();
    fCommandList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex,
                                       startInstance);
}

void GrD3DDirectCommandList::executeIndirect(const sk_sp<GrD3DCommandSignature> commandSignature,
                                             unsigned int maxCommandCount,
                                             const GrD3DBuffer* argumentBuffer,
                                             size_t argumentBufferOffset) {
    SkASSERT(fIsActive);
    this->addingWork();
    this->addResource(commandSignature);
    fCommandList->ExecuteIndirect(commandSignature->commandSignature(), maxCommandCount,
                                  argumentBuffer->d3dResource(), argumentBufferOffset,
                                  nullptr, 0);
    this->addGrBuffer(sk_ref_sp<const GrBuffer>(argumentBuffer));
}

void GrD3DDirectCommandList::clearRenderTargetView(const GrD3DRenderTarget* renderTarget,
                                                   const SkPMColor4f& color,
                                                   const D3D12_RECT* rect) {
    this->addingWork();
    this->addResource(renderTarget->resource());
    const GrD3DTextureResource* msaaTextureResource = renderTarget->msaaTextureResource();
    if (msaaTextureResource && msaaTextureResource != renderTarget) {
        this->addResource(msaaTextureResource->resource());
    }
    unsigned int numRects = rect ? 1 : 0;
    fCommandList->ClearRenderTargetView(renderTarget->colorRenderTargetView(),
                                        color.vec(), numRects, rect);
}

void GrD3DDirectCommandList::clearDepthStencilView(const GrD3DStencilAttachment* stencil,
                                                   uint8_t stencilClearValue,
                                                   const D3D12_RECT* rect) {
    this->addingWork();
    this->addResource(stencil->resource());
    unsigned int numRects = rect ? 1 : 0;
    fCommandList->ClearDepthStencilView(stencil->view(), D3D12_CLEAR_FLAG_STENCIL, 0,
                                        stencilClearValue, numRects, rect);
}

void GrD3DDirectCommandList::setRenderTarget(const GrD3DRenderTarget* renderTarget) {
    this->addingWork();
    this->addResource(renderTarget->resource());
    const GrD3DTextureResource* msaaTextureResource = renderTarget->msaaTextureResource();
    if (msaaTextureResource && msaaTextureResource != renderTarget) {
        this->addResource(msaaTextureResource->resource());
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = renderTarget->colorRenderTargetView();

    D3D12_CPU_DESCRIPTOR_HANDLE dsDescriptor;
    D3D12_CPU_DESCRIPTOR_HANDLE* dsDescriptorPtr = nullptr;
    if (auto stencil = renderTarget->getStencilAttachment()) {
        GrD3DStencilAttachment* d3dStencil = static_cast<GrD3DStencilAttachment*>(stencil);
        this->addResource(d3dStencil->resource());
        dsDescriptor = d3dStencil->view();
        dsDescriptorPtr = &dsDescriptor;
    }

    fCommandList->OMSetRenderTargets(1, &rtvDescriptor, false, dsDescriptorPtr);
}

void GrD3DDirectCommandList::resolveSubresourceRegion(const GrD3DTextureResource* dstTexture,
                                                      unsigned int dstX, unsigned int dstY,
                                                      const GrD3DTextureResource* srcTexture,
                                                      D3D12_RECT* srcRect) {
    SkASSERT(dstTexture->dxgiFormat() == srcTexture->dxgiFormat());
    SkASSERT(dstTexture->currentState() == D3D12_RESOURCE_STATE_RESOLVE_DEST);
    SkASSERT(srcTexture->currentState() == D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    this->addingWork();
    this->addResource(dstTexture->resource());
    this->addResource(srcTexture->resource());

    gr_cp<ID3D12GraphicsCommandList1> commandList1;
    HRESULT result = fCommandList->QueryInterface(IID_PPV_ARGS(&commandList1));
    if (SUCCEEDED(result)) {
        commandList1->ResolveSubresourceRegion(dstTexture->d3dResource(), 0, dstX, dstY,
                                               srcTexture->d3dResource(), 0, srcRect,
                                               srcTexture->dxgiFormat(),
                                               D3D12_RESOLVE_MODE_AVERAGE);
    } else {
        fCommandList->ResolveSubresource(dstTexture->d3dResource(), 0, srcTexture->d3dResource(), 0,
                                         srcTexture->dxgiFormat());
    }
}

void GrD3DDirectCommandList::setGraphicsRootConstantBufferView(
        unsigned int rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation) {
    SkASSERT(rootParameterIndex ==
                (unsigned int) GrD3DRootSignature::ParamIndex::kConstantBufferView);
    if (bufferLocation != fCurrentConstantBufferAddress) {
        fCommandList->SetGraphicsRootConstantBufferView(rootParameterIndex, bufferLocation);
        fCurrentConstantBufferAddress = bufferLocation;
    }
}

void GrD3DDirectCommandList::setGraphicsRootDescriptorTable(
        unsigned int rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor) {
    SkASSERT(rootParameterIndex ==
                    (unsigned int)GrD3DRootSignature::ParamIndex::kSamplerDescriptorTable ||
             rootParameterIndex ==
                    (unsigned int)GrD3DRootSignature::ParamIndex::kTextureDescriptorTable);
    if (fCurrentRootDescriptorTable[rootParameterIndex].ptr != baseDescriptor.ptr) {
        fCommandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
        fCurrentRootDescriptorTable[rootParameterIndex] = baseDescriptor;
    }
}

void GrD3DDirectCommandList::setDescriptorHeaps(sk_sp<GrRecycledResource> srvCrvHeapResource,
                                                ID3D12DescriptorHeap* srvCrvDescriptorHeap,
                                                sk_sp<GrRecycledResource> samplerHeapResource,
                                                ID3D12DescriptorHeap* samplerDescriptorHeap) {
    if (srvCrvDescriptorHeap != fCurrentSRVCRVDescriptorHeap ||
        samplerDescriptorHeap != fCurrentSamplerDescriptorHeap) {
        ID3D12DescriptorHeap* heaps[2] = {
            srvCrvDescriptorHeap,
            samplerDescriptorHeap
        };

        fCommandList->SetDescriptorHeaps(2, heaps);
        this->addRecycledResource(std::move(srvCrvHeapResource));
        this->addRecycledResource(std::move(samplerHeapResource));
        fCurrentSRVCRVDescriptorHeap = srvCrvDescriptorHeap;
        fCurrentSamplerDescriptorHeap = samplerDescriptorHeap;
    }
}

void GrD3DDirectCommandList::addSampledTextureRef(GrD3DTexture* texture) {
    this->addResource(texture->resource());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<GrD3DCopyCommandList> GrD3DCopyCommandList::Make(ID3D12Device* device) {
    gr_cp<ID3D12CommandAllocator> allocator;
    GR_D3D_CALL_ERRCHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&allocator)));

    gr_cp<ID3D12GraphicsCommandList> commandList;
    GR_D3D_CALL_ERRCHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, allocator.get(),
                                                   nullptr, IID_PPV_ARGS(&commandList)));
    auto grCL = new GrD3DCopyCommandList(std::move(allocator), std::move(commandList));
    return std::unique_ptr<GrD3DCopyCommandList>(grCL);
}

GrD3DCopyCommandList::GrD3DCopyCommandList(gr_cp<ID3D12CommandAllocator> allocator,
                                           gr_cp<ID3D12GraphicsCommandList> commandList)
    : GrD3DCommandList(std::move(allocator), std::move(commandList)) {
}
