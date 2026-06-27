#pragma once
#include <JuceHeader.h>
#include <array>

struct ColorPalette
{
    juce::String name;
    juce::Colour bg;
    juce::Colour grid;
    juce::Colour text;
    juce::Colour anaFill;
    juce::Colour anaStroke;
    juce::Colour lowcut;
    juce::Colour highcut;
    juce::Colour bell1;
    juce::Colour bell2;
    juce::Colour bell3;
    juce::Colour bell4;
};

inline const std::array<ColorPalette, 10>& getPalettes()
{
    static const std::array<ColorPalette, 10> palettes = {{
        // 1. Studio Neon (Current Default)
        {
            "Studio Neon",
            juce::Colour(0xff0a0a0f), juce::Colour(0x1a7080af), juce::Colour(0xff8c9ba5),
            juce::Colour(0x2236cfc9), juce::Colour(0x7036cfc9),
            juce::Colour(0xffff7a45), juce::Colour(0xff40a9ff),
            juce::Colour(0xff36cfc9), juce::Colour(0xff73d13d), juce::Colour(0xffffc53d), juce::Colour(0xfff759ab)
        },
        // 2. Chic Monochrome
        {
            "Chic Mono",
            juce::Colour(0xff0a0a0a), juce::Colour(0x20888888), juce::Colour(0xffcccccc),
            juce::Colour(0x15ffffff), juce::Colour(0x60ffffff),
            juce::Colour(0xffffffff), juce::Colour(0xffe6e6e6),
            juce::Colour(0xffcccccc), juce::Colour(0xffb3b3b3), juce::Colour(0xff999999), juce::Colour(0xff808080)
        },
        // 3. Vivid Future
        {
            "Vivid Future",
            juce::Colour(0xff050515), juce::Colour(0x22ff00ff), juce::Colour(0xff00ffff),
            juce::Colour(0x2000ffff), juce::Colour(0xff00ffff),
            juce::Colour(0xffff0055), juce::Colour(0xff00bfff),
            juce::Colour(0xff00ffcc), juce::Colour(0xff99ff00), juce::Colour(0xffffcc00), juce::Colour(0xffff00ff)
        },
        // 4. Warm Retro
        {
            "Warm Retro",
            juce::Colour(0xff1c120c), juce::Colour(0x20e6a23c), juce::Colour(0xffebd9c8),
            juce::Colour(0x20e6a23c), juce::Colour(0xffe6a23c),
            juce::Colour(0xfff56c6c), juce::Colour(0xffe28d5c),
            juce::Colour(0xffe6a23c), juce::Colour(0xffd9a05b), juce::Colour(0xffc09060), juce::Colour(0xffa88058)
        },
        // 5. Pastel Dream
        {
            "Pastel Dream",
            juce::Colour(0xff1e1a24), juce::Colour(0x18ffb7c5), juce::Colour(0xffe8d3e3),
            juce::Colour(0x20ffb7c5), juce::Colour(0x90ffb7c5),
            juce::Colour(0xffffb7b2), juce::Colour(0xffb3c2f2),
            juce::Colour(0xffffdac1), juce::Colour(0xffe2f0cb), juce::Colour(0xffb5ead7), juce::Colour(0xffc7ceea)
        },
        // 6. Cyberpunk
        {
            "Cyberpunk",
            juce::Colour(0xff120015), juce::Colour(0x20fcee0a), juce::Colour(0xff00f0ff),
            juce::Colour(0x20fcee0a), juce::Colour(0xfffcee0a),
            juce::Colour(0xff00ffff), juce::Colour(0xff00ff66),
            juce::Colour(0xfffcee0a), juce::Colour(0xffff0055), juce::Colour(0xffb500ff), juce::Colour(0xffff9900)
        },
        // 7. Ocean Abyss
        {
            "Ocean Abyss",
            juce::Colour(0xff000c14), juce::Colour(0x200088cc), juce::Colour(0xff8cd5ff),
            juce::Colour(0x2000ffaa), juce::Colour(0xff00ffaa),
            juce::Colour(0xff0088ff), juce::Colour(0xff33a3ff),
            juce::Colour(0xff00ccff), juce::Colour(0xff00ffaa), juce::Colour(0xff00e676), juce::Colour(0xff81c784)
        },
        // 8. Forest Zenith
        {
            "Forest Zenith",
            juce::Colour(0xff0b140d), juce::Colour(0x1a78a57d), juce::Colour(0xffc2d7c5),
            juce::Colour(0x1a81c784), juce::Colour(0xff81c784),
            juce::Colour(0xffa1887f), juce::Colour(0xff388e3c),
            juce::Colour(0xff4caf50), juce::Colour(0xff8bc34a), juce::Colour(0xffcddc39), juce::Colour(0xffd7ccc8)
        },
        // 9. Sunset Glow
        {
            "Sunset Glow",
            juce::Colour(0xff14090b), juce::Colour(0x1aff5722), juce::Colour(0xfffbe9e7),
            juce::Colour(0x20ff9800), juce::Colour(0xffff9800),
            juce::Colour(0xffd32f2f), juce::Colour(0xffe91e63),
            juce::Colour(0xfff44336), juce::Colour(0xffff5722), juce::Colour(0xffff9800), juce::Colour(0xffffc107)
        },
        // 10. Midnight Gold
        {
            "Midnight Gold",
            juce::Colour(0xff050505), juce::Colour(0x20ffd700), juce::Colour(0xffebd185),
            juce::Colour(0x20ebd185), juce::Colour(0xffebd185),
            juce::Colour(0xffffffff), juce::Colour(0xffe5c158),
            juce::Colour(0xffd4af37), juce::Colour(0xffaa7c11), juce::Colour(0xffebd185), juce::Colour(0xff8c7853)
        }
    }};
    return palettes;
}
