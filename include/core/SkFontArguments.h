/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFontArguments_DEFINED
#define SkFontArguments_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkScalar.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"

/** Represents a set of actual arguments for a font. */
struct SkFontArguments {
    struct VariationPosition {
        struct Coordinate {
            SkFourByteTag axis;
            float value;
        };
        const Coordinate* coordinates;
        int coordinateCount;
    };

    /** Combined parameters allowing selection of a palettes (using basePalette)
     * and an optional set of overrides.
     *
     * The colorOverrides can be a sparse set of color indices + color values
     * overriding existing palette entries. Not all palette entries have to be
     * specified. Specifying more overrides than what the font has in its
     * palettes or specifying color indices outside the number of entries in a
     * palette will not have any effect. Later override entries override earlier
     * ones. */
    struct PaletteOverride {
        struct ColorOverride {
            uint16_t colorIndex;
            SkColor color;
        };
        uint16_t basePalette;
        const ColorOverride* colorOverrides;
        int colorOverrideCount;
    };

    SkFontArguments()
            : fCollectionIndex(0)
            , fVariationDesignPosition{nullptr, 0}
            , fPaletteOverride{0, nullptr, 0} {}

    /** Specify the index of the desired font.
     *
     *  Font formats like ttc, dfont, cff, cid, pfr, t42, t1, and fon may actually be indexed
     *  collections of fonts.
     */
    SkFontArguments& setCollectionIndex(int collectionIndex) {
        fCollectionIndex = collectionIndex;
        return *this;
    }

    /** Specify a position in the variation design space.
     *
     *  Any axis not specified will use the default value.
     *  Any specified axis not actually present in the font will be ignored.
     *
     *  @param position not copied. The value must remain valid for life of SkFontArguments.
     */
    SkFontArguments& setVariationDesignPosition(VariationPosition position) {
        fVariationDesignPosition.coordinates = position.coordinates;
        fVariationDesignPosition.coordinateCount = position.coordinateCount;
        return *this;
    }

    int getCollectionIndex() const {
        return fCollectionIndex;
    }

    VariationPosition getVariationDesignPosition() const {
        return fVariationDesignPosition;
    }

    SkFontArguments& setPaletteOverride(PaletteOverride paletteOverride) {
        fPaletteOverride.basePalette = paletteOverride.basePalette;
        fPaletteOverride.colorOverrides = paletteOverride.colorOverrides;
        fPaletteOverride.colorOverrideCount = paletteOverride.colorOverrideCount;
        return *this;
    }

    PaletteOverride getPaletteOverride() const { return fPaletteOverride; }

private:
    int fCollectionIndex;
    VariationPosition fVariationDesignPosition;
    PaletteOverride fPaletteOverride;
};

#endif
