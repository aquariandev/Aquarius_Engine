#include "ImGuiFont.hpp"

#include <imgui.h>

ImFont* LoadBundledFont(float sizePixels) {
    static const ImWchar polishGlyphRanges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0,
    };
#ifdef AQUARIUS_BUNDLED_FONT_PATH
    ImFontConfig fontConfig;
    fontConfig.SizePixels = sizePixels;
    fontConfig.GlyphRanges = polishGlyphRanges;
    if (ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
            AQUARIUS_BUNDLED_FONT_PATH, sizePixels, &fontConfig,
            polishGlyphRanges)) {
        return font;
    }
#endif
    ImFontConfig fallbackConfig;
    fallbackConfig.SizePixels = sizePixels;
    fallbackConfig.GlyphRanges = polishGlyphRanges;
    return ImGui::GetIO().Fonts->AddFontDefault(&fallbackConfig);
}
