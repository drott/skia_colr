/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrMtlGpu_DEFINED
#define GrMtlGpu_DEFINED

#include "include/private/SkDeque.h"
#include "src/gpu/GrFinishCallbacks.h"
#include "src/gpu/GrGpu.h"
#include "src/gpu/GrRenderTarget.h"
#include "src/gpu/GrSemaphore.h"
#include "src/gpu/GrStagingBufferManager.h"
#include "src/gpu/GrTexture.h"

#include "src/gpu/mtl/GrMtlCaps.h"
#include "src/gpu/mtl/GrMtlCommandBuffer.h"
#include "src/gpu/mtl/GrMtlResourceProvider.h"
#include "src/gpu/mtl/GrMtlStencilAttachment.h"
#include "src/gpu/mtl/GrMtlUtil.h"

#import <Metal/Metal.h>

class GrMtlOpsRenderPass;
class GrMtlTexture;
class GrSemaphore;
struct GrMtlBackendContext;
class GrMtlCommandBuffer;

namespace SkSL {
    class Compiler;
}

class GrMtlGpu : public GrGpu {
public:
    static sk_sp<GrGpu> Make(GrDirectContext*, const GrContextOptions&,
                             id<MTLDevice>, id<MTLCommandQueue>);
    ~GrMtlGpu() override;

    void disconnect(DisconnectType) override;

    const GrMtlCaps& mtlCaps() const { return *fMtlCaps.get(); }

    id<MTLDevice> device() const { return fDevice; }

    GrMtlResourceProvider& resourceProvider() { return fResourceProvider; }

    GrMtlCommandBuffer* commandBuffer() {
        SkASSERT(fCurrentCmdBuffer);
        return fCurrentCmdBuffer.get();
     }

    enum SyncQueue {
        kForce_SyncQueue,
        kSkip_SyncQueue
    };

    void deleteBackendTexture(const GrBackendTexture&) override;

    bool compile(const GrProgramDesc&, const GrProgramInfo&) override;

#if GR_TEST_UTILS
    bool isTestingOnlyBackendTexture(const GrBackendTexture&) const override;

    GrBackendRenderTarget createTestingOnlyBackendRenderTarget(SkISize dimensions,
                                                               GrColorType,
                                                               int sampleCnt,
                                                               GrProtected) override;
    void deleteTestingOnlyBackendRenderTarget(const GrBackendRenderTarget&) override;

    void testingOnly_flushGpuAndSync() override;

    void resetShaderCacheForTesting() const override {
        fResourceProvider.resetShaderCacheForTesting();
    }
#endif

    void copySurfaceAsResolve(GrSurface* dst, GrSurface* src);

    void copySurfaceAsBlit(GrSurface* dst, GrSurface* src, const SkIRect& srcRect,
                           const SkIPoint& dstPoint);

    bool onCopySurface(GrSurface* dst, GrSurface* src, const SkIRect& srcRect,
                       const SkIPoint& dstPoint) override;

    GrOpsRenderPass* getOpsRenderPass(
            GrRenderTarget*, GrStencilAttachment*,
            GrSurfaceOrigin, const SkIRect& bounds,
            const GrOpsRenderPass::LoadAndStoreInfo&,
            const GrOpsRenderPass::StencilLoadAndStoreInfo&,
            const SkTArray<GrSurfaceProxy*, true>& sampledProxies,
            GrXferBarrierFlags renderPassXferBarriers) override;

    SkSL::Compiler* shaderCompiler() const { return fCompiler.get(); }

    void submit(GrOpsRenderPass* renderPass) override;

    GrFence SK_WARN_UNUSED_RESULT insertFence() override;
    bool waitFence(GrFence) override;
    void deleteFence(GrFence) const override;

    std::unique_ptr<GrSemaphore> SK_WARN_UNUSED_RESULT makeSemaphore(bool isOwned) override;
    std::unique_ptr<GrSemaphore> wrapBackendSemaphore(
            const GrBackendSemaphore& semaphore,
            GrResourceProvider::SemaphoreWrapType wrapType,
            GrWrapOwnership ownership) override;
    void insertSemaphore(GrSemaphore* semaphore) override;
    void waitSemaphore(GrSemaphore* semaphore) override;
    void checkFinishProcs() override { this->checkForFinishedCommandBuffers(); }
    std::unique_ptr<GrSemaphore> prepareTextureForCrossContextUsage(GrTexture*) override;

    // When the Metal backend actually uses indirect command buffers, this function will actually do
    // what it says. For now, every command is encoded directly into the primary command buffer, so
    // this function is pretty useless, except for indicating that a render target has been drawn
    // to.
    void submitIndirectCommandBuffer(GrSurface* surface, GrSurfaceOrigin origin,
                                     const SkIRect* bounds) {
        this->didWriteToSurface(surface, origin, bounds);
    }

private:
    GrMtlGpu(GrDirectContext*, const GrContextOptions&, id<MTLDevice>,
             id<MTLCommandQueue>, MTLFeatureSet);

    void destroyResources();

    void onResetContext(uint32_t resetBits) override {}

    void querySampleLocations(GrRenderTarget*, SkTArray<SkPoint>*) override {
        SkASSERT(!this->caps()->sampleLocationsSupport());
        SK_ABORT("Sample locations not yet implemented for Metal.");
    }

    void xferBarrier(GrRenderTarget*, GrXferBarrierType) override {}

    GrStagingBufferManager* stagingBufferManager() override { return &fStagingBufferManager; }
    void takeOwnershipOfBuffer(sk_sp<GrGpuBuffer>) override;

    GrBackendTexture onCreateBackendTexture(SkISize dimensions,
                                            const GrBackendFormat&,
                                            GrRenderable,
                                            GrMipmapped,
                                            GrProtected) override;

    bool onUpdateBackendTexture(const GrBackendTexture&,
                                sk_sp<GrRefCntedCallback> finishedCallback,
                                const BackendTextureData*) override;

    GrBackendTexture onCreateCompressedBackendTexture(SkISize dimensions,
                                                      const GrBackendFormat&,
                                                      GrMipmapped,
                                                      GrProtected) override;

    bool onUpdateCompressedBackendTexture(const GrBackendTexture&,
                                          sk_sp<GrRefCntedCallback> finishedCallback,
                                          const BackendTextureData*) override;

    sk_sp<GrTexture> onCreateTexture(SkISize,
                                     const GrBackendFormat&,
                                     GrRenderable,
                                     int renderTargetSampleCnt,
                                     SkBudgeted,
                                     GrProtected,
                                     int mipLevelCount,
                                     uint32_t levelClearMask) override;
    sk_sp<GrTexture> onCreateCompressedTexture(SkISize dimensions,
                                               const GrBackendFormat&,
                                               SkBudgeted,
                                               GrMipmapped,
                                               GrProtected,
                                               const void* data, size_t dataSize) override;

    sk_sp<GrTexture> onWrapBackendTexture(const GrBackendTexture&,
                                          GrWrapOwnership,
                                          GrWrapCacheable,
                                          GrIOType) override;

    sk_sp<GrTexture> onWrapCompressedBackendTexture(const GrBackendTexture&,
                                                    GrWrapOwnership,
                                                    GrWrapCacheable) override;

    sk_sp<GrTexture> onWrapRenderableBackendTexture(const GrBackendTexture&,
                                                    int sampleCnt,
                                                    GrWrapOwnership,
                                                    GrWrapCacheable) override;

    sk_sp<GrRenderTarget> onWrapBackendRenderTarget(const GrBackendRenderTarget&) override;

    sk_sp<GrRenderTarget> onWrapBackendTextureAsRenderTarget(const GrBackendTexture&,
                                                             int sampleCnt) override;

    sk_sp<GrGpuBuffer> onCreateBuffer(size_t, GrGpuBufferType, GrAccessPattern,
                                      const void*) override;

    bool onReadPixels(GrSurface* surface, int left, int top, int width, int height,
                      GrColorType surfaceColorType, GrColorType bufferColorType, void* buffer,
                      size_t rowBytes) override;

    bool onWritePixels(GrSurface*, int left, int top, int width, int height,
                       GrColorType surfaceColorType, GrColorType bufferColorType,
                       const GrMipLevel[], int mipLevelCount,
                       bool prepForTexSampling) override;

    bool onTransferPixelsTo(GrTexture*, int left, int top, int width, int height,
                            GrColorType textureColorType, GrColorType bufferColorType, GrGpuBuffer*,
                            size_t offset, size_t rowBytes) override;
    bool onTransferPixelsFrom(GrSurface*, int left, int top, int width, int height,
                              GrColorType surfaceColorType, GrColorType bufferColorType,
                              GrGpuBuffer*, size_t offset) override;

    bool onRegenerateMipMapLevels(GrTexture*) override;

    void onResolveRenderTarget(GrRenderTarget* target, const SkIRect& resolveRect) override;

    void resolveTexture(id<MTLTexture> colorTexture, id<MTLTexture> resolveTexture);

    void addFinishedProc(GrGpuFinishedProc finishedProc,
                         GrGpuFinishedContext finishedContext) override;
    void addFinishedCallback(sk_sp<GrRefCntedCallback> finishedCallback);

    bool onSubmitToGpu(bool syncCpu) override;

    // Commits the current command buffer to the queue and then creates a new command buffer. If
    // sync is set to kForce_SyncQueue, the function will wait for all work in the committed
    // command buffer to finish before returning.
    bool submitCommandBuffer(SyncQueue sync);

    void checkForFinishedCommandBuffers();

    // Function that uploads data onto textures with private storage mode (GPU access only).
    bool uploadToTexture(GrMtlTexture* tex, int left, int top, int width, int height,
                         GrColorType dataColorType, const GrMipLevel texels[], int mipLevels);
    // Function that fills texture levels with transparent black based on levelMask.
    bool clearTexture(GrMtlTexture*, size_t bbp, uint32_t levelMask);
    bool readOrTransferPixels(GrSurface* surface, int left, int top, int width, int height,
                              GrColorType dstColorType, id<MTLBuffer> transferBuffer, size_t offset,
                              size_t imageBytes, size_t rowBytes);

    GrStencilAttachment* createStencilAttachmentForRenderTarget(
            const GrRenderTarget*, SkISize dimensions, int numStencilSamples) override;

    bool createMtlTextureForBackendSurface(MTLPixelFormat,
                                           SkISize dimensions,
                                           int sampleCnt,
                                           GrTexturable,
                                           GrRenderable,
                                           GrMipmapped,
                                           GrMtlTextureInfo*);

#if GR_TEST_UTILS
    void testingOnly_startCapture() override;
    void testingOnly_endCapture() override;
#endif

#ifdef SK_ENABLE_DUMP_GPU
    void onDumpJSON(SkJSONWriter*) const override;
#endif

    sk_sp<GrMtlCaps> fMtlCaps;

    id<MTLDevice> fDevice;
    id<MTLCommandQueue> fQueue;

    sk_sp<GrMtlCommandBuffer> fCurrentCmdBuffer;

    struct OutstandingCommandBuffer {
        OutstandingCommandBuffer(sk_sp<GrMtlCommandBuffer> commandBuffer, GrFence fence)
            : fCommandBuffer(std::move(commandBuffer))
            , fFence(fence) {}
        sk_sp<GrMtlCommandBuffer> fCommandBuffer;
        GrFence fFence;
    };
    SkDeque fOutstandingCommandBuffers;

    std::unique_ptr<SkSL::Compiler> fCompiler;

    GrMtlResourceProvider fResourceProvider;
    GrStagingBufferManager fStagingBufferManager;

    bool fDisconnected;

    using INHERITED = GrGpu;
};

#endif

