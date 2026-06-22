#include "ArcDial.h"

ArcDialLookAndFeel::ArcDialLookAndFeel() {}

void ArcDialLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float)juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = (float)x + (float)width * 0.5f;
    auto centreY = (float)y + (float)height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto arcThickness = 6.0f; // アークの太さ

    // 1. 背景のトラック（Ableton風のダークグレー）
    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawEllipse(rx, ry, rw, rw, arcThickness);

    // 2. 塗りつぶされるアーク（セクション色を基にしたグラデーション）
    juce::Path p;
    p.addArc(rx, ry, rw, rw, rotaryStartAngle, angle, true);

    // セクションから渡された色を取得
    auto baseColour = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // その色を薄い色と濃い色にする
    auto lightColour = baseColour.brighter(1.2f);  // 薄い色（明るくする）
    auto darkColour = baseColour.darker(0.8f);     // 濃い色（暗くする）

    // グラデーション作成（薄い→濃い）
    juce::ColourGradient gradient(
        darkColour,      // ← 左側：濃い色
        rx, centreY,
        lightColour,     // ← 右側：薄い色
        rx + rw, centreY,
        false
    );

    g.setGradientFill(gradient);
    g.strokePath(p, juce::PathStrokeType(arcThickness, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    // 3. ポインター（直線のインジケーター）
    juce::Path p2;
    auto pointerLength = radius * 0.4f;
    p2.addRectangle(-1.5f, -radius, 3.0f, pointerLength);
    p2.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colours::white);
    g.fillPath(p2);
}