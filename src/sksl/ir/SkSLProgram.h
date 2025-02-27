/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_PROGRAM
#define SKSL_PROGRAM

#include <vector>
#include <memory>

#include "src/sksl/ir/SkSLBoolLiteral.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLFloatLiteral.h"
#include "src/sksl/ir/SkSLIntLiteral.h"
#include "src/sksl/ir/SkSLModifiers.h"
#include "src/sksl/ir/SkSLProgramElement.h"
#include "src/sksl/ir/SkSLSymbolTable.h"

#ifdef SK_VULKAN
#include "src/gpu/vk/GrVkCaps.h"
#endif

// name of the render target width uniform
#define SKSL_RTWIDTH_NAME "u_skRTWidth"

// name of the render target height uniform
#define SKSL_RTHEIGHT_NAME "u_skRTHeight"

namespace SkSL {

class Context;

/**
 * Represents a fully-digested program, ready for code generation.
 */
struct Program {
    struct Settings {
        struct Value {
            Value(bool b)
            : fKind(kBool_Kind)
            , fValue(b) {}

            Value(int i)
            : fKind(kInt_Kind)
            , fValue(i) {}

            Value(unsigned int i)
            : fKind(kInt_Kind)
            , fValue(i) {}

            Value(float f)
            : fKind(kFloat_Kind)
            , fValueF(f) {}

            std::unique_ptr<Expression> literal(const Context& context, int offset) const {
                switch (fKind) {
                    case Program::Settings::Value::kBool_Kind:
                        return std::unique_ptr<Expression>(new BoolLiteral(context,
                                                                           offset,
                                                                           fValue));
                    case Program::Settings::Value::kInt_Kind:
                        return std::unique_ptr<Expression>(new IntLiteral(context,
                                                                          offset,
                                                                          fValue));
                    case Program::Settings::Value::kFloat_Kind:
                        return std::unique_ptr<Expression>(new FloatLiteral(context,
                                                                            offset,
                                                                            fValueF));
                    default:
                        SkASSERT(false);
                        return nullptr;
                }
            }

            enum {
                kBool_Kind,
                kInt_Kind,
                kFloat_Kind,
            } fKind;

            union {
                int   fValue;  // for kBool_Kind and kInt_Kind
                float fValueF; // for kFloat_Kind
            };
        };

#if defined(SKSL_STANDALONE) || !SK_SUPPORT_GPU
        const StandaloneShaderCaps* fCaps = &standaloneCaps;
#else
        const GrShaderCaps* fCaps = nullptr;
#endif
        // if false, sk_FragCoord is exactly the same as gl_FragCoord. If true, the y coordinate
        // must be flipped.
        bool fFlipY = false;
        // if false, sk_FragCoord is exactly the same as gl_FragCoord. If true, the w coordinate
        // must be inversed.
        bool fInverseW = false;
        // If true the destination fragment color is read sk_FragColor. It must be declared inout.
        bool fFragColorIsInOut = false;
        // if true, Setting objects (e.g. sk_Caps.fbFetchSupport) should be replaced with their
        // constant equivalents during compilation
        bool fReplaceSettings = true;
        // if true, all halfs are forced to be floats
        bool fForceHighPrecision = false;
        // if true, add -0.5 bias to LOD of all texture lookups
        bool fSharpenTextures = false;
        // if the program needs to create an RTHeight uniform, this is its offset in the uniform
        // buffer
        int fRTHeightOffset = -1;
        // if the program needs to create an RTHeight uniform and is creating spriv, this is the
        // binding and set number of the uniform buffer.
        int fRTHeightBinding = -1;
        int fRTHeightSet = -1;
        // If true, remove any uncalled functions other than main(). Note that a function which
        // starts out being used may end up being uncalled after optimization.
        bool fRemoveDeadFunctions = true;
        // Functions larger than this (measured in IR nodes) will not be inlined. The default value
        // is arbitrary.
        int fInlineThreshold = 49;
        // true to enable optimization passes
        bool fOptimize = true;
        // If true, implicit conversions to lower precision numeric types are allowed
        // (eg, float to half)
        bool fAllowNarrowingConversions = false;
    };

    struct Inputs {
        // if true, this program requires the render target width uniform to be defined
        bool fRTWidth;

        // if true, this program requires the render target height uniform to be defined
        bool fRTHeight;

        // if true, this program must be recompiled if the flipY setting changes. If false, the
        // program will compile to the same code regardless of the flipY setting.
        bool fFlipY;

        void reset() {
            fRTWidth = false;
            fRTHeight = false;
            fFlipY = false;
        }

        bool isEmpty() {
            return !fRTWidth && !fRTHeight && !fFlipY;
        }
    };

    enum Kind {
        kFragment_Kind,
        kVertex_Kind,
        kGeometry_Kind,
        kFragmentProcessor_Kind,
        kPipelineStage_Kind,
        kGeneric_Kind,
    };

    Program(Kind kind,
            std::unique_ptr<String> source,
            Settings settings,
            std::shared_ptr<Context> context,
            std::vector<std::unique_ptr<ProgramElement>> elements,
            std::unique_ptr<ModifiersPool> modifiers,
            std::shared_ptr<SymbolTable> symbols,
            Inputs inputs)
    : fKind(kind)
    , fSource(std::move(source))
    , fSettings(settings)
    , fContext(context)
    , fSymbols(symbols)
    , fInputs(inputs)
    , fElements(std::move(elements))
    , fModifiers(std::move(modifiers)) {}

    const std::vector<std::unique_ptr<ProgramElement>>& elements() const { return fElements; }

    void finish() {
        fModifiers->finish();
    }

    Kind fKind;
    std::unique_ptr<String> fSource;
    Settings fSettings;
    std::shared_ptr<Context> fContext;
    // it's important to keep fElements defined after (and thus destroyed before) fSymbols,
    // because destroying elements can modify reference counts in symbols
    std::shared_ptr<SymbolTable> fSymbols;
    Inputs fInputs;

private:
    std::vector<std::unique_ptr<ProgramElement>> fElements;
    std::unique_ptr<ModifiersPool> fModifiers;

    friend class Compiler;
};

}  // namespace SkSL

#endif
