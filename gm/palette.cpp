/*
 * Copyright 2022 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gm/gm.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "tools/Resources.h"
#include "tools/ToolUtils.h"

#include <string.h>
#include <initializer_list>
#include <vector>

namespace skiagm {

namespace {
const char kColrCpalTestFontPath[] = "fonts/more_samples-glyf_colr_1.ttf";

std::vector<SkFontArguments::PaletteOverride::ColorOverride> colorOverridesAll = {
        // A gradient of dark to light purple for the circle palette test glyph.
        {0, 0xff310b55},
        {1, 0xff510970},
        {2, 0xff76078f},
        {3, 0xff9606aa},
        {4, 0xffb404c4},
        {5, 0xffd802e2},
        {6, 0xfffa00ff},
        {7, 0xff888888},
        {8, 0xff888888},
        {9, 0xff888888},
        {10, 0xff888888},
        {11, 0xff888888}};

std::vector<SkFontArguments::PaletteOverride::ColorOverride> colorOverridesOne = {
        {2, 0xff02dfe2},
};
}

class FontPaletteGM : public GM {
public:
    enum PaletteTestType {
        kPaletteDefault,
        kPaletteSwitchLight,
        kPaletteSwitchDark,
        kPaletteOverrideOne,
        kPaletteOverrideAll
    };

    FontPaletteGM(PaletteTestType testType) : fTestType(testType) {}

protected:
    static SkString testTypeToString(PaletteTestType testType) {
        switch (testType) {
            case kPaletteDefault:
                return SkString("default");
            case kPaletteSwitchLight:
                return SkString("light");
            case kPaletteSwitchDark:
                return SkString("dark");
            case kPaletteOverrideOne:
                return SkString("override_one");
            case kPaletteOverrideAll:
                return SkString("override_all");
        }
        SkASSERT(false); /* not reached */
        return SkString();
    }

    sk_sp<SkTypeface> fTypefaceDefault;
    sk_sp<SkTypeface> fTypefaceFromStream;
    sk_sp<SkTypeface> fTypefaceCloned;
    std::vector<uint16_t> fGlyphs{{56, 57}};

    void onOnceBeforeDraw() override {
        SkFontArguments paletteArguments;
        SkFontArguments::PaletteOverride paletteOverride;
        switch (fTestType) {
            case kPaletteDefault: {
                break;
            }
            case kPaletteSwitchDark: {
                paletteOverride.basePalette = 1;
                paletteArguments.setPaletteOverride(paletteOverride);
                break;
            }
            case kPaletteSwitchLight: {
                paletteOverride.basePalette = 2;
                paletteArguments.setPaletteOverride(paletteOverride);
                break;
            }
            case kPaletteOverrideOne: {
                paletteOverride.basePalette = 0;
                paletteOverride.colorOverrides = colorOverridesOne.data();
                paletteOverride.colorOverrideCount = colorOverridesOne.size();
                paletteArguments.setPaletteOverride(paletteOverride);
                break;
            }
            case kPaletteOverrideAll: {
                paletteOverride.basePalette = 0;
                paletteOverride.colorOverrides = colorOverridesAll.data();
                paletteOverride.colorOverrideCount = colorOverridesAll.size();
                paletteArguments.setPaletteOverride(paletteOverride);
                break;
            }
            default:
                SkASSERT(false);
        }

        fTypefaceDefault = MakeResourceAsTypeface(kColrCpalTestFontPath);
        fTypefaceCloned =
                fTypefaceDefault ? fTypefaceDefault->makeClone(paletteArguments) : nullptr;

        fTypefaceFromStream = SkFontMgr::RefDefault()->makeFromStream(
                GetResourceAsStream(kColrCpalTestFontPath), paletteArguments);
    }

    SkString onShortName() override {
        SkString gm_name = SkStringPrintf("font_palette_%s", testTypeToString(fTestType).c_str());
        return gm_name;
    }

    SkISize onISize() override { return SkISize::Make(600, 400); }

    DrawResult onDraw(SkCanvas* canvas, SkString* errorMsg) override {
        canvas->drawColor(SK_ColorWHITE);
        SkPaint paint;

        canvas->translate(200, 20);

        if (!fTypefaceCloned || !fTypefaceFromStream) {
            *errorMsg = "Did not recognize COLR v1 test font format.";
            return DrawResult::kSkip;
        }

        SkFontMetrics metrics;
        SkScalar y = 0;
        SkScalar textSize = 200;
        for (auto& typeface : { fTypefaceFromStream, fTypefaceCloned} ) {
            SkFont defaultFont(fTypefaceDefault);
            SkFont paletteFont(typeface);
            defaultFont.setSize(textSize);
            paletteFont.setSize(textSize);

            defaultFont.getMetrics(&metrics);
            y += -metrics.fAscent;
            // Set a recognizable foreground color which is not to be overriden.
            paint.setColor(SK_ColorGRAY);
            // Draw the default palette on the left, for COLRv0 and COLRv1.
            canvas->drawSimpleText(fGlyphs.data(),
                                   fGlyphs.size() * sizeof(uint16_t),
                                   SkTextEncoding::kGlyphID,
                                   10,
                                   y,
                                   defaultFont,
                                   paint);
            // Draw the overriden palette on the right.
            canvas->drawSimpleText(fGlyphs.data(),
                                   fGlyphs.size() * sizeof(uint16_t),
                                   SkTextEncoding::kGlyphID,
                                   440,
                                   y,
                                   paletteFont,
                                   paint);
            y += metrics.fDescent + metrics.fLeading;
        }
        return DrawResult::kOk;
    }

private:
    using INHERITED = GM;
    PaletteTestType fTestType;
};

DEF_GM(return new FontPaletteGM(FontPaletteGM::kPaletteDefault);)
DEF_GM(return new FontPaletteGM(FontPaletteGM::kPaletteSwitchLight);)
DEF_GM(return new FontPaletteGM(FontPaletteGM::kPaletteSwitchDark);)
DEF_GM(return new FontPaletteGM(FontPaletteGM::kPaletteOverrideOne);)
DEF_GM(return new FontPaletteGM(FontPaletteGM::kPaletteOverrideAll);)

}  // namespace skiagm
