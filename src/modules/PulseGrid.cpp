// Pulse Grid (module 1): a field of glyphs, drawn from an atlas rendered once with JUCE.

#include "Module.h"

#include <juce_graphics/juce_graphics.h>

namespace cortex
{

using namespace juce::gl;

namespace
{
    class PulseGrid final : public Module
    {
    public:
        ~PulseGrid() override
        {
            if (atlas != 0)
                glDeleteTextures (1, &atlas);
        }

        const char* id() const noexcept override   { return "pulse_grid"; }

        bool init (Renderer& r, std::string& log) override
        {
            shader = r.program ("fullscreen.vert.glsl", "pulse_grid.frag.glsl", log);
            if (shader == nullptr)
                return false;

            buildAtlas();
            return true;
        }

        void render (Renderer& r, gfx::RenderTarget& target, const WidgetSettings& s) override
        {
            target.bind();
            gfx::Device::setBlend (gfx::Device::Blend::none);
            shader->use();
            r.bindCommon (*shader, s, target.width(), target.height());
            shader->setTexture ("uGlyphs", 4, atlas);
            shader->set ("uGlyphCount", (float) glyphCount);
            shader->set ("uGlyphGrid", (float) columns, (float) rows);
            r.device().drawFullscreen();
        }

    private:
        static constexpr int cell = 64, columns = 16, rows = 4;

        void buildAtlas()
        {
            // Original character set: digits, a few capitals, and technical symbols.
            const juce::String glyphs ("0123456789ABCDEFHKMNRSTVXYZ<>/\\|+-=*#%&@$?!:;~^[]{}()");
            glyphCount = juce::jmin (glyphs.length(), columns * rows);

            juce::Image image (juce::Image::SingleChannel, columns * cell, rows * cell, true, juce::SoftwareImageType());
            {
                juce::Graphics g (image);
                g.setColour (juce::Colours::white);
                g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), (float) cell * 0.74f, juce::Font::bold)));
                for (int i = 0; i < glyphCount; ++i)
                    g.drawText (juce::String::charToString (glyphs[i]),
                                juce::Rectangle<int> ((i % columns) * cell, (i / columns) * cell, cell, cell),
                                juce::Justification::centred, false);
            }

            const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::readOnly);
            std::vector<uint8_t> pixels ((size_t) (image.getWidth() * image.getHeight()));
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    pixels[(size_t) (y * image.getWidth() + x)] = *bitmap.getPixelPointer (x, y);

            glGenTextures (1, &atlas);
            glBindTexture (GL_TEXTURE_2D, atlas);
            glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D (GL_TEXTURE_2D, 0, GL_R8, image.getWidth(), image.getHeight(), 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
            glGenerateMipmap (GL_TEXTURE_2D);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        gfx::Shader* shader = nullptr;
        GLuint atlas = 0;
        int glyphCount = 0;
    };
}

std::unique_ptr<Module> makePulseGrid()   { return std::make_unique<PulseGrid>(); }

} // namespace cortex
