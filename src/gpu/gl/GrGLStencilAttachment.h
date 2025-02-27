/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef GrGLStencilAttachment_DEFINED
#define GrGLStencilAttachment_DEFINED

#include "include/gpu/gl/GrGLInterface.h"
#include "src/gpu/GrStencilAttachment.h"

class GrGLStencilAttachment : public GrStencilAttachment {
public:
    struct IDDesc {
        IDDesc() : fRenderbufferID(0) {}
        GrGLuint fRenderbufferID;
    };

    GrGLStencilAttachment(GrGpu* gpu,
                          const IDDesc& idDesc,
                          SkISize dimensions,
                          int sampleCnt,
                          GrGLFormat format)
        : GrStencilAttachment(gpu, dimensions, sampleCnt, GrProtected::kNo)
        , fFormat(format)
        , fRenderbufferID(idDesc.fRenderbufferID) {
        this->registerWithCache(SkBudgeted::kYes);
    }

    GrBackendFormat backendFormat() const override;

    GrGLuint renderbufferID() const {
        return fRenderbufferID;
    }

    GrGLFormat format() const { return fFormat; }

protected:
    // overrides of GrResource
    void onRelease() override;
    void onAbandon() override;
    void setMemoryBacking(SkTraceMemoryDump* traceMemoryDump,
                          const SkString& dumpName) const override;

private:
    size_t onGpuMemorySize() const override;

    GrGLFormat fFormat;

    // may be zero for external SBs associated with external RTs
    // (we don't require the client to give us the id, just tell
    // us how many bits of stencil there are).
    GrGLuint fRenderbufferID;

    using INHERITED = GrStencilAttachment;
};

#endif
