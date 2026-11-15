#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace aurora
{
/**
    Immutable description of a flat UI theme.

    Every field maps 1:1 to a key in assets/themes/theme.schema.json. The JSON
    parser falls back field by field to the compiled Dark Matter theme, so a
    partially valid document still produces a usable Theme. Colours are opaque
    RGB (alpha is always 1.0); gradients, shadows and textures are not modelled.

    ThemeManager returns references to Theme objects owned by itself. Those
    references stay valid for the lifetime of the ThemeManager because the theme
    list is built once in the constructor and never mutated afterwards.
*/
struct Theme
{
    juce::String id;
    juce::String name;
    int version = 1;

    juce::Colour background;
    juce::Colour surface;
    juce::Colour surfaceAlt;
    juce::Colour border;
    juce::Colour borderStrong;
    juce::Colour textPrimary;
    juce::Colour textSecondary;
    juce::Colour textDisabled;
    juce::Colour accent;
    juce::Colour accentSecondary;
    juce::Colour warning;
    juce::Colour danger;
    juce::Colour success;
    juce::Colour meterLow;
    juce::Colour meterMid;
    juce::Colour meterHigh;
    juce::Colour meterBackground;
    juce::Colour grid;
    juce::Colour knobBody;
    juce::Colour knobRing;
    juce::Colour knobPrimary;
    juce::Colour knobSecondary;
    juce::Colour selection;

    int borderWidth = 1;
    int cornerRadius = 2;
    int slotHeight = 96;
    int headerHeight = 52;
    int knobSize = 44;
    int meterSegments = 24;

    juce::String uiFamily = "Inter";
    juce::String monoFamily = "JetBrains Mono";
    float fontSizeSmall = 10.0f;
    float fontSizeNormal = 12.0f;
    float fontSizeLarge = 15.0f;
    float fontSizeMono = 11.0f;
};

/**
    Loads the factory themes from the embedded AuroraAssets target and owns the
    active theme.

    Threading: message thread only. The class contains no locks and is never
    touched by the audio thread, RackEngine, metering workers or ARIA workers.
    Construct it in PluginProcessor's constructor (message thread) and let the
    editor subscribe as a juce::ChangeListener.

    Loading strategy:
      1. Parse "dark-matter.json" and "industrial-steel.json" from BinaryData.
      2. Validate every field; invalid fields fall back to compiled Dark Matter
         values and are reported through getLoadErrors().
      3. A theme whose "id" is missing or malformed is rejected as a whole.
      4. If a theme id is absent after loading, the compiled theme is installed.

    If BinaryData.h is not available at compile time (for example a unit-test
    target that does not link AuroraAssets) both themes come from the compiled
    fallback and isUsingEmbeddedAssets() returns false. This keeps the class
    compilable in isolation without inventing BinaryData symbols.
*/
class ThemeManager final : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* darkMatterId = "dark-matter-flat";
    static constexpr const char* industrialSteelId = "industrial-steel-flat";

    static constexpr int themeFormatVersion = 1;

    ThemeManager();
    ~ThemeManager() override = default;

    ThemeManager (const ThemeManager&) = delete;
    ThemeManager& operator= (const ThemeManager&) = delete;

    /** Returns a reference to the active theme. Never null. */
    [[nodiscard]] const Theme& getActiveTheme() const noexcept;

    [[nodiscard]] juce::String getActiveThemeId() const;

    /** Ids in load order (factory themes first). Allocates; UI thread only. */
    [[nodiscard]] juce::StringArray getThemeIds() const;

    /** Returns nullptr when the id is unknown. Pointer stays valid for the
        lifetime of this ThemeManager. */
    [[nodiscard]] const Theme* getTheme (const juce::String& themeId) const noexcept;

    [[nodiscard]] bool hasTheme (const juce::String& themeId) const noexcept;

    /** Selects a theme and broadcasts a change if the selection actually
        changed. Returns false for an unknown id; in that case the active theme
        is left untouched. Message thread only. */
    bool setTheme (const juce::String& themeId);

    /** False when the library was compiled without <BinaryData.h> or when the
        AuroraAssets target does not expose the expected resources. */
    [[nodiscard]] bool isUsingEmbeddedAssets() const noexcept;

    /** Non-fatal diagnostics plus rejected-theme reasons, capped at 64 entries.
        Empty when both factory documents loaded without any field fallback. */
    [[nodiscard]] const juce::StringArray& getLoadErrors() const noexcept;

    [[nodiscard]] juce::String getLoadErrorsAsText() const;

private:
    static constexpr int maxLoggedErrors = 64;

    void addThemeFromJson (const void* data, int size, const juce::String& sourceName);
    void addThemeFromJsonText (const juce::String& text, const juce::String& sourceName);
    void logError (const juce::String& message);
    [[nodiscard]] int indexOf (const juce::String& themeId) const noexcept;

    std::vector<Theme> themes;
    int activeIndex = 0;
    juce::StringArray loadErrors;
};
} // namespace aurora
