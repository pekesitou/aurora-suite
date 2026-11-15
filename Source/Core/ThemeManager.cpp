#include "ThemeManager.h"

// The AuroraAssets target (juce_add_binary_data) generates BinaryData.h in the
// build directory. If this translation unit is compiled without that target
// (e.g. a standalone DSP test target), the class still compiles and falls back
// to the compiled Dark Matter theme. Integrators must add the generated include
// directory and link AuroraAssets to get the JSON documents at runtime.
#if defined (__has_include)
    #if __has_include (<BinaryData.h>)
        #include <BinaryData.h>
        #define AURORA_HAS_EMBEDDED_THEME_ASSETS 1
    #endif
#endif

#ifndef AURORA_HAS_EMBEDDED_THEME_ASSETS
    #define AURORA_HAS_EMBEDDED_THEME_ASSETS 0
#endif

#include <cmath>
#include <iterator>
#include <utility>

namespace aurora
{
namespace
{
constexpr bool kHasEmbeddedAssets = AURORA_HAS_EMBEDDED_THEME_ASSETS != 0;

// juce_add_binary_data exposes resources by their sanitised C identifier, so
// "dark-matter.json" becomes "dark_matter_json".
constexpr const char* kDarkMatterResource = "dark_matter_json";
constexpr const char* kIndustrialSteelResource = "industrial_steel_json";

void assertMessageThread() noexcept
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
}

juce::Colour fromHex (juce::uint32 rgb) noexcept
{
    return juce::Colour::fromRGB (static_cast<juce::uint8> ((rgb >> 16) & 0xffu),
                                  static_cast<juce::uint8> ((rgb >> 8) & 0xffu),
                                  static_cast<juce::uint8> (rgb & 0xffu));
}

bool parseHexColour (const juce::String& text, juce::Colour& result) noexcept
{
    if (text.length() != 7 || text[0] != '#')
        return false;

    for (int i = 1; i < 7; ++i)
    {
        const auto c = text[i];
        const bool isHex = (c >= '0' && c <= '9')
                        || (c >= 'a' && c <= 'f')
                        || (c >= 'A' && c <= 'F');

        if (! isHex)
            return false;
    }

    const auto r = static_cast<juce::uint8> (text.substring (1, 3).getHexValue32());
    const auto g = static_cast<juce::uint8> (text.substring (3, 5).getHexValue32());
    const auto b = static_cast<juce::uint8> (text.substring (5, 7).getHexValue32());
    result = juce::Colour::fromRGB (r, g, b);
    return true;
}

bool isValidThemeId (const juce::String& id) noexcept
{
    constexpr int minLength = 3;
    constexpr int maxLength = 48;

    if (id.length() < minLength || id.length() > maxLength)
        return false;

    if (id[0] < 'a' || id[0] > 'z')
        return false;

    for (int i = 1; i < id.length(); ++i)
    {
        const auto c = id[i];
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (! valid)
            return false;
    }

    return id.getLastCharacter() != '-' && ! id.contains ("--");
}

Theme makeCompiledDarkMatter()
{
    Theme t;
    t.id = ThemeManager::darkMatterId;
    t.name = "Dark Matter - Flat";
    t.version = ThemeManager::themeFormatVersion;

    t.background      = fromHex (0x0A0B0D);
    t.surface         = fromHex (0x14161B);
    t.surfaceAlt      = fromHex (0x1A1D24);
    t.border          = fromHex (0x22262E);
    t.borderStrong    = fromHex (0x333A45);
    t.textPrimary     = fromHex (0xE6EAF2);
    t.textSecondary   = fromHex (0x98A2B3);
    t.textDisabled    = fromHex (0x5A6373);
    t.accent          = fromHex (0x00E5FF);
    t.accentSecondary = fromHex (0x8B5CF6);
    t.warning         = fromHex (0xFFB74D);
    t.danger          = fromHex (0xFF5370);
    t.success         = fromHex (0x2ED47A);
    t.meterLow        = fromHex (0x2ED47A);
    t.meterMid        = fromHex (0xFFB74D);
    t.meterHigh       = fromHex (0xFF5370);
    t.meterBackground = fromHex (0x0F1115);
    t.grid            = fromHex (0x1E232C);
    t.knobBody        = fromHex (0x1A1E25);
    t.knobRing        = fromHex (0x00E5FF);
    t.knobPrimary     = fromHex (0x00E5FF);
    t.knobSecondary   = fromHex (0x8B5CF6);
    t.selection       = fromHex (0x123A45);

    t.borderWidth   = 1;
    t.cornerRadius  = 2;
    t.slotHeight    = 96;
    t.headerHeight  = 52;
    t.knobSize      = 44;
    t.meterSegments = 24;

    t.uiFamily       = "Inter";
    t.monoFamily     = "JetBrains Mono";
    t.fontSizeSmall  = 10.0f;
    t.fontSizeNormal = 12.0f;
    t.fontSizeLarge  = 15.0f;
    t.fontSizeMono   = 11.0f;
    return t;
}

struct ColourToken
{
    const char* key;
    juce::Colour Theme::* member;
};

constexpr ColourToken kColourTokens[] =
{
    { "background",      &Theme::background },
    { "surface",         &Theme::surface },
    { "surfaceAlt",      &Theme::surfaceAlt },
    { "border",          &Theme::border },
    { "borderStrong",    &Theme::borderStrong },
    { "textPrimary",     &Theme::textPrimary },
    { "textSecondary",   &Theme::textSecondary },
    { "textDisabled",    &Theme::textDisabled },
    { "accent",          &Theme::accent },
    { "accentSecondary", &Theme::accentSecondary },
    { "warning",         &Theme::warning },
    { "danger",          &Theme::danger },
    { "success",         &Theme::success },
    { "meterLow",        &Theme::meterLow },
    { "meterMid",        &Theme::meterMid },
    { "meterHigh",       &Theme::meterHigh },
    { "meterBackground", &Theme::meterBackground },
    { "grid",            &Theme::grid },
    { "knobBody",        &Theme::knobBody },
    { "knobRing",        &Theme::knobRing },
    { "knobPrimary",     &Theme::knobPrimary },
    { "knobSecondary",   &Theme::knobSecondary },
    { "selection",       &Theme::selection }
};

static_assert (std::size (kColourTokens) == 23, "Theme::Colour token table must cover every schema token");

struct IntToken
{
    const char* key;
    int Theme::* member;
    int minValue;
    int maxValue;
};

constexpr IntToken kGeometryTokens[] =
{
    { "borderWidth",   &Theme::borderWidth,   0,  4 },
    { "cornerRadius",  &Theme::cornerRadius,  0, 12 },
    { "slotHeight",    &Theme::slotHeight,   64, 160 },
    { "headerHeight",  &Theme::headerHeight, 32, 96 },
    { "knobSize",      &Theme::knobSize,     24, 96 },
    { "meterSegments", &Theme::meterSegments, 8, 64 }
};

struct FloatToken
{
    const char* key;
    float Theme::* member;
    float minValue;
    float maxValue;
};

constexpr FloatToken kFontSizeTokens[] =
{
    { "small",  &Theme::fontSizeSmall,   8.0f, 24.0f },
    { "normal", &Theme::fontSizeNormal,  8.0f, 28.0f },
    { "large",  &Theme::fontSizeLarge,   8.0f, 40.0f },
    { "mono",   &Theme::fontSizeMono,    8.0f, 24.0f }
};

juce::var getObjectProperty (const juce::var& object, const char* key)
{
    const auto value = object.getProperty (key, juce::var());
    return value.isObject() ? value : juce::var();
}

bool readNumber (const juce::var& value, double& result) noexcept
{
    if (! (value.isInt() || value.isDouble()))
        return false;

    result = static_cast<double> (value);
    return std::isfinite (result);
}

void readColours (const juce::var& root, Theme& theme, const juce::String& source,
                  juce::StringArray& errors)
{
    const auto colors = getObjectProperty (root, "colors");
    if (! colors.isObject())
    {
        errors.add (source + ": missing or invalid 'colors' object");
        return;
    }

    for (const auto& token : kColourTokens)
    {
        const auto value = colors.getProperty (token.key, juce::var());
        if (! value.isString())
        {
            errors.add (source + ": colors." + juce::String (token.key) + " must be a #RRGGBB string");
            continue;
        }

        juce::Colour parsed;
        if (! parseHexColour (value.toString().trim(), parsed))
        {
            errors.add (source + ": colors." + juce::String (token.key) + " is not a valid #RRGGBB colour");
            continue;
        }

        theme.*(token.member) = parsed;
    }
}

void readGeometry (const juce::var& root, Theme& theme, const juce::String& source,
                   juce::StringArray& errors)
{
    const auto geometry = getObjectProperty (root, "geometry");
    if (! geometry.isObject())
    {
        errors.add (source + ": missing or invalid 'geometry' object");
        return;
    }

    for (const auto& token : kGeometryTokens)
    {
        const auto value = geometry.getProperty (token.key, juce::var());
        if (! value.isInt())
        {
            errors.add (source + ": geometry." + juce::String (token.key) + " must be an integer");
            continue;
        }

        const auto number = static_cast<int> (value);
        if (number < token.minValue || number > token.maxValue)
        {
            errors.add (source + ": geometry." + juce::String (token.key) + " out of range ["
                        + juce::String (token.minValue) + ", " + juce::String (token.maxValue) + "]");
            continue;
        }

        theme.*(token.member) = number;
    }
}

void readTypography (const juce::var& root, Theme& theme, const juce::String& source,
                     juce::StringArray& errors)
{
    const auto typography = getObjectProperty (root, "typography");
    if (! typography.isObject())
    {
        errors.add (source + ": missing or invalid 'typography' object");
        return;
    }

    for (const auto* key : { "uiFamily", "monoFamily" })
    {
        const auto value = typography.getProperty (key, juce::var());
        const auto family = value.isString() ? value.toString().trim() : juce::String();

        if (family.isEmpty())
        {
            errors.add (source + ": typography." + juce::String (key) + " must be a non-empty string");
            continue;
        }

        if (juce::String (key) == "uiFamily")
            theme.uiFamily = family;
        else
            theme.monoFamily = family;
    }

    const auto sizes = getObjectProperty (typography, "sizes");
    if (! sizes.isObject())
    {
        errors.add (source + ": missing or invalid 'typography.sizes' object");
        return;
    }

    for (const auto& token : kFontSizeTokens)
    {
        const auto value = sizes.getProperty (token.key, juce::var());
        double number = 0.0;

        if (! readNumber (value, number))
        {
            errors.add (source + ": typography.sizes." + juce::String (token.key) + " must be a number");
            continue;
        }

        if (number < token.minValue || number > token.maxValue)
        {
            errors.add (source + ": typography.sizes." + juce::String (token.key) + " out of range ["
                        + juce::String (token.minValue, 1) + ", " + juce::String (token.maxValue, 1) + "]");
            continue;
        }

        theme.*(token.member) = static_cast<float> (number);
    }

    // Keep visual hierarchy usable even if the document inverts the scale.
    if (theme.fontSizeSmall > theme.fontSizeNormal)
    {
        errors.add (source + ": typography.sizes.small must be <= normal");
        theme.fontSizeSmall = theme.fontSizeNormal;
    }

    if (theme.fontSizeNormal > theme.fontSizeLarge)
    {
        errors.add (source + ": typography.sizes.normal must be <= large");
        theme.fontSizeNormal = theme.fontSizeLarge;
    }
}

bool parseTheme (const juce::String& jsonText, const juce::String& source,
                 Theme& parsedTheme, juce::StringArray& errors)
{
    juce::var parseDiagnostic;
    auto root = juce::JSON::parse (jsonText, parseDiagnostic);

    // juce::JSON::parse returns the parsed value and fills the out-parameter
    // with the error text on failure. Accept the out-parameter too, so the
    // parser keeps working if that contract changes.
    if (root.isVoid() && parseDiagnostic.isObject())
        root = parseDiagnostic;

    if (! root.isObject())
    {
        const auto detail = parseDiagnostic.isString() ? parseDiagnostic.toString()
                                                      : juce::String ("root value is not an object");
        errors.add (source + ": invalid JSON (" + detail + ")");
        return false;
    }

    const auto idValue = root.getProperty ("id", juce::var());
    const auto id = idValue.isString() ? idValue.toString().trim() : juce::String();

    if (! isValidThemeId (id))
    {
        errors.add (source + ": missing or invalid 'id' (lowercase a-z, 0-9 and single dashes)");
        return false;
    }

    auto theme = makeCompiledDarkMatter();
    theme.id = id;

    const auto nameValue = root.getProperty ("name", juce::var());
    const auto name = nameValue.isString() ? nameValue.toString().trim() : juce::String();
    if (name.isEmpty())
    {
        errors.add (source + ": missing 'name'; using id as display name");
        theme.name = id;
    }
    else
    {
        theme.name = name;
    }

    const auto versionValue = root.getProperty ("version", juce::var());
    if (! versionValue.isInt() || static_cast<int> (versionValue) < 1
        || static_cast<int> (versionValue) > 1000)
    {
        errors.add (source + ": 'version' must be an integer in [1, 1000]; using 1");
        theme.version = 1;
    }
    else
    {
        theme.version = static_cast<int> (versionValue);
    }

    readColours (root, theme, source, errors);
    readGeometry (root, theme, source, errors);
    readTypography (root, theme, source, errors);

    parsedTheme = std::move (theme);
    return true;
}

struct EmbeddedAsset
{
    const void* data = nullptr;
    int size = 0;
};

EmbeddedAsset findEmbeddedAsset (const char* resourceName) noexcept
{
#if AURORA_HAS_EMBEDDED_THEME_ASSETS
    int size = 0;
    if (const auto* data = AuroraAssets::getNamedResource (resourceName, size))
        return { data, size };
#else
    juce::ignoreUnused (resourceName);
#endif
    return {};
}
} // namespace

ThemeManager::ThemeManager()
{
    assertMessageThread();

    themes.reserve (3);

    const auto darkAsset = findEmbeddedAsset (kDarkMatterResource);
    addThemeFromJson (darkAsset.data, darkAsset.size, kDarkMatterResource);

    const auto steelAsset = findEmbeddedAsset (kIndustrialSteelResource);
    addThemeFromJson (steelAsset.data, steelAsset.size, kIndustrialSteelResource);

    // Last-resort default: the compiled Dark Matter palette.
    if (! hasTheme (darkMatterId))
    {
        themes.push_back (makeCompiledDarkMatter());
        logError ("Compiled fallback theme 'dark-matter-flat' installed");
    }

    if (! hasTheme (industrialSteelId))
        logError ("Theme 'industrial-steel-flat' unavailable; only 'dark-matter-flat' is selectable");

    activeIndex = indexOf (darkMatterId);
    if (activeIndex < 0)
        activeIndex = 0;
}

const Theme& ThemeManager::getActiveTheme() const noexcept
{
    jassert (activeIndex >= 0 && activeIndex < static_cast<int> (themes.size()));
    return themes[static_cast<size_t> (activeIndex)];
}

juce::String ThemeManager::getActiveThemeId() const
{
    return getActiveTheme().id;
}

juce::StringArray ThemeManager::getThemeIds() const
{
    assertMessageThread();

    juce::StringArray ids;
    ids.ensureStorageAllocated (static_cast<int> (themes.size()));

    for (const auto& theme : themes)
        ids.add (theme.id);

    return ids;
}

const Theme* ThemeManager::getTheme (const juce::String& themeId) const noexcept
{
    for (const auto& theme : themes)
        if (theme.id == themeId)
            return &theme;

    return nullptr;
}

bool ThemeManager::hasTheme (const juce::String& themeId) const noexcept
{
    return indexOf (themeId) >= 0;
}

bool ThemeManager::setTheme (const juce::String& themeId)
{
    assertMessageThread();

    const auto index = indexOf (themeId);
    if (index < 0)
    {
        logError ("setTheme: unknown theme id '" + themeId + "'");
        return false;
    }

    if (index != activeIndex)
    {
        activeIndex = index;
        sendChangeMessage();
    }

    return true;
}

bool ThemeManager::isUsingEmbeddedAssets() const noexcept
{
    return kHasEmbeddedAssets;
}

const juce::StringArray& ThemeManager::getLoadErrors() const noexcept
{
    return loadErrors;
}

juce::String ThemeManager::getLoadErrorsAsText() const
{
    return loadErrors.joinIntoString ("\n");
}

void ThemeManager::addThemeFromJson (const void* data, int size, const juce::String& sourceName)
{
    if (data == nullptr || size <= 0)
    {
        logError (sourceName + ": embedded resource not available");
        return;
    }

    const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), size);
    addThemeFromJsonText (text, sourceName);
}

void ThemeManager::addThemeFromJsonText (const juce::String& text, const juce::String& sourceName)
{
    Theme parsed;
    juce::StringArray fieldErrors;

    if (! parseTheme (text, sourceName, parsed, fieldErrors))
    {
        for (const auto& error : fieldErrors)
            logError (error);

        return;
    }

    for (const auto& error : fieldErrors)
        logError (error);

    if (hasTheme (parsed.id))
    {
        logError (sourceName + ": duplicate theme id '" + parsed.id + "' ignored");
        return;
    }

    themes.push_back (std::move (parsed));
}

void ThemeManager::logError (const juce::String& message)
{
    if (loadErrors.size() < maxLoggedErrors)
        loadErrors.add (message);
}

int ThemeManager::indexOf (const juce::String& themeId) const noexcept
{
    for (int i = 0; i < static_cast<int> (themes.size()); ++i)
        if (themes[static_cast<size_t> (i)].id == themeId)
            return i;

    return -1;
}
} // namespace aurora
