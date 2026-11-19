// cortex_snapshot: renders every factory scene from a fixed synthetic beat and writes PNGs, plus a
// contact sheet and GPU timings. Deterministic: the same build always produces the same pictures.
//
//   cortex_snapshot [--out <folder>] [--size <w>x<h>] [--frames <n>]
//
// Seeds the golden-image tests of North Star Q5 and lets a reviewer look at the modules without
// opening a DAW.

#include "TestSignals.h"

#include <analysis/AnalysisEngine.h>
#include <engine/Renderer.h>
#include <engine/ModuleInfo.h>
#include <presets/CoverArtPalette.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <cstdio>
#include <map>

namespace
{

using namespace juce::gl;

struct Options
{
    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("snapshots");
    int width = 1920, height = 1080;
    int timingFrames = 240;
    juce::File cover;   // --cover <image>: also render scenes with the palette taken from it
    juce::File thumbs;  // --thumbs <folder>: only render one 480 x 270 thumbnail per module
};

Options parse (const juce::StringArray& args)
{
    Options o;
    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--out" && i + 1 < args.size())
            o.outDir = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i].unquoted());
        else if (args[i] == "--size" && i + 1 < args.size())
        {
            const auto parts = juce::StringArray::fromTokens (args[++i], "x", "");
            if (parts.size() == 2) { o.width = parts[0].getIntValue(); o.height = parts[1].getIntValue(); }
        }
        else if (args[i] == "--frames" && i + 1 < args.size())
            o.timingFrames = args[++i].getIntValue();
        else if (args[i] == "--thumbs" && i + 1 < args.size())
            o.thumbs = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i].unquoted());
        else if (args[i] == "--cover" && i + 1 < args.size())
            o.cover = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i].unquoted());
    }
    return o;
}

/** The analysis of a synthetic trap pattern, at 140 BPM with the host transport running. */
struct Track
{
    testsignals::Pattern pattern;
    std::vector<cortex::AnalysisFrame> frames;

    Track()
    {
        pattern = testsignals::trapHats (48000.0);

        struct Sink : cortex::FrameSink
        {
            std::vector<cortex::AnalysisFrame>* out = nullptr;
            void onFrame (const cortex::AnalysisFrame& f) noexcept override { out->push_back (f); }
        } sink;
        sink.out = &frames;

        cortex::AnalysisEngine engine;
        engine.prepare (48000.0);
        constexpr int block = 512;
        for (size_t pos = 0; pos + block <= pattern.left.size(); pos += block)
        {
            cortex::HostTime host;
            host.playing = true;
            host.bpm = 140.0;
            host.ppq = (double) pos / 48000.0 * 140.0 / 60.0;
            host.barStartPpq = std::floor (host.ppq / 4.0) * 4.0;
            const float* channels[2] = { pattern.left.data() + pos, pattern.right.data() + pos };
            engine.process (channels, 2, block, host, {}, sink);
        }
    }

    double seconds() const   { return (double) pattern.left.size() / 48000.0; }
};

class SnapshotRunner final : public juce::Component, private juce::OpenGLRenderer
{
public:
    explicit SnapshotRunner (Options o) : options (std::move (o))
    {
        setSize (320, 180);
        context.setOpenGLVersionRequired (juce::OpenGLContext::openGL4_1);
        context.setRenderer (this);
        context.setComponentPaintingEnabled (false);
        context.setContinuousRepainting (true);
        context.attachTo (*this);
    }

    ~SnapshotRunner() override   { context.detach(); }

    int exitCode = 0;

private:
    void newOpenGLContextCreated() override {}
    void openGLContextClosing() override {}

    void renderOpenGL() override
    {
        if (finished)
            return;
        finished = true;

        exitCode = run() ? 0 : 1;
        context.setContinuousRepainting (false);
        juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
    }

    bool run()
    {
        std::printf ("GPU: %s\nOpenGL: %s\n", (const char*) glGetString (GL_RENDERER), (const char*) glGetString (GL_VERSION));

        cortex::Renderer renderer;
        std::string log;
        if (! renderer.init (log))
        {
            std::printf ("renderer init failed:\n%s\n", log.c_str());
            renderer.shutdown();
            return false;
        }

        options.outDir.createDirectory();
        const Track track;

        if (options.thumbs != juce::File())
        {
            // One module per thumbnail, on the palette its factory scene uses.
            const std::map<std::string, int> palettes { { "bloom_orb", 0 }, { "spectrum_bars", 1 }, { "scope", 11 },
                                                        { "waveform_stack", 3 }, { "flowfield", 7 }, { "pulse_grid", 8 } };
            options.thumbs.createDirectory();
            output.ensure (options.width, options.height, GL_RGBA8, false);
            const double moment = (double) track.pattern.kicks[4] / 48000.0 + 0.035;

            for (const auto& info : cortex::moduleInfos())
            {
                const auto found = palettes.find (info.id);
                const int palette = found != palettes.end() ? found->second : 0;
                cortex::SceneSpec scene;
                scene.name = info.name;
                scene.backgroundPalette = palette;
                scene.bloom = 0.12f;
                scene.widgets.push_back (cortex::defaultWidget (info.id, palette));

                log.clear();
                if (! renderer.loadScene (scene, log))
                {
                    std::printf ("%s failed:\n%s\n", info.id, log.c_str());
                    continue;
                }
                const auto image = captureAt (renderer, track, moment).rescaled (480, 270, juce::Graphics::highResamplingQuality);
                auto file = options.thumbs.getChildFile (juce::String (info.id) + ".png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
                std::printf ("thumbnail %s\n", file.getFullPathName().toRawUTF8());
            }

            renderer.shutdown();
            output.release();
            return true;
        }

        // Moments to capture: just after a kick, and just after the clap.
        const double kickTime  = (double) track.pattern.kicks[4] / 48000.0 + 0.035;
        const double snareTime = (double) track.pattern.snares[1] / 48000.0 + 0.040;

        output.ensure (options.width, options.height, GL_RGBA8, false);

        juce::Array<juce::Image> sheet;
        bool ok = true;

        for (int s = 0; s < cortex::numFactoryScenes(); ++s)
        {
            const auto& spec = cortex::factoryScene (s);
            log.clear();
            if (! renderer.loadScene (spec, log))
            {
                std::printf ("scene %s failed:\n%s\n", spec.name.c_str(), log.c_str());
                ok = false;
                continue;
            }

            cortex::FrameInterpolator interpolator;
            size_t nextFrame = 0;
            const double fps = 60.0;
            juce::Image atKick, atSnare;

            const int totalFrames = (int) std::ceil (std::max (kickTime, snareTime) * fps) + 1;
            for (int frame = 0; frame <= totalFrames; ++frame)
            {
                const double t = frame / fps;
                while (nextFrame < track.frames.size() && (double) track.frames[nextFrame].samplePos / 48000.0 <= t)
                    interpolator.push (track.frames[nextFrame++]);

                const auto& inputs = interpolator.update ((float) (1.0 / fps));
                renderFrame (renderer, inputs);

                if (atKick.isNull() && t >= kickTime)   atKick = readOutput();
                if (atSnare.isNull() && t >= snareTime) atSnare = readOutput();
            }

            const auto base = juce::String (s + 1).paddedLeft ('0', 2) + "_" + juce::String (spec.name).toLowerCase();
            write (atKick, base + "_kick.png");
            write (atSnare, base + "_snare.png");
            sheet.add (atKick);

            // GPU time: keep playing the track and average the timer queries.
            float sum = 0.0f;
            int counted = 0;
            for (int frame = 0; frame < options.timingFrames; ++frame)
            {
                const double t = std::fmod ((totalFrames + frame) / fps, track.seconds());
                if (t < 1.0 / fps) { nextFrame = 0; }
                while (nextFrame < track.frames.size() && (double) track.frames[nextFrame].samplePos / 48000.0 <= t)
                    interpolator.push (track.frames[nextFrame++]);

                renderFrame (renderer, interpolator.update ((float) (1.0 / fps)));
                if (frame >= 30)
                {
                    sum += renderer.stats().gpuMs;
                    ++counted;
                }
            }
            glFinish();
            std::printf ("%-10s %d widget(s)  GPU %.2f ms at %dx%d\n", spec.name.c_str(), (int) spec.widgets.size(),
                         counted > 0 ? sum / (float) counted : 0.0f, options.width, options.height);
        }

        writeSheet (sheet, "contact_sheet.png");

        // Every Look on the three-widget scene.
        juce::Array<juce::Image> lookSheet;
        const int lookScene = cortex::numFactoryScenes() - 1;
        for (int look = 0; look < cortex::numFactoryLooks(); ++look)
        {
            log.clear();
            renderer.loadScene (cortex::factoryScene (lookScene), log);
            frameSettings = {};
            frameSettings.look = look;
            const auto image = captureAt (renderer, track, kickTime);
            write (image, "look_" + juce::String (look).paddedLeft ('0', 2) + "_"
                          + juce::String (cortex::factoryLook (look).name).toLowerCase().replaceCharacter (' ', '_') + ".png");
            lookSheet.add (image);

            // Keep rendering the same moment to time this Look on the three-widget scene.
            cortex::FrameInterpolator hold;
            float sum = 0.0f;
            int counted = 0;
            for (int frame = 0; frame < options.timingFrames; ++frame)
            {
                renderFrame (renderer, hold.update (1.0f / 60.0f));
                if (frame >= 30) { sum += renderer.stats().gpuMs; ++counted; }
            }
            std::printf ("look %-13s %d layer(s)  GPU %.2f ms at %dx%d\n", cortex::factoryLook (look).name,
                         (int) cortex::factoryLook (look).layers.size(), counted > 0 ? sum / (float) counted : 0.0f,
                         options.width, options.height);
        }
        writeSheet (lookSheet, "looks_sheet.png");

        // The macros, one at a time at full, on the same scene.
        juce::Array<juce::Image> macroSheet;
        for (int m = 0; m < 8; ++m)
        {
            log.clear();
            renderer.loadScene (cortex::factoryScene (lookScene), log);
            frameSettings = {};
            frameSettings.macros[(size_t) m] = 1.0f;
            const auto image = captureAt (renderer, track, kickTime);
            write (image, "macro_" + juce::String (m + 1) + "_" + juce::String (cortex::macroLabel (m)).toLowerCase() + ".png");
            macroSheet.add (image);
        }
        writeSheet (macroSheet, "macros_sheet.png");
        frameSettings = {};

        // Cover art: the same extraction the editor uses, applied to every scene.
        if (options.cover.existsAsFile())
        {
            const auto image = juce::ImageFileFormat::loadFrom (options.cover);
            const auto small = image.rescaled (96, 96, juce::Graphics::mediumResamplingQuality);
            std::vector<cortex::Rgb> pixels;
            for (int y = 0; y < small.getHeight(); ++y)
                for (int x = 0; x < small.getWidth(); ++x)
                {
                    const auto c = small.getPixelAt (x, y);
                    pixels.push_back ({ c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() });
                }
            const auto palette = cortex::paletteFromPixels (pixels);

            std::printf ("cover palette:");
            for (auto hex : palette.srgbHex)
                std::printf (" #%06X", (unsigned) hex);
            std::printf ("\n");

            juce::Array<juce::Image> coverSheet;
            coverSheet.add (image.rescaled (options.width, options.height));
            for (int sc = 0; sc < cortex::numFactoryScenes(); ++sc)
            {
                log.clear();
                renderer.loadScene (cortex::factoryScene (sc), log);
                frameSettings = {};
                frameSettings.paletteOverride = &palette;
                coverSheet.add (captureAt (renderer, track, kickTime));
            }
            writeSheet (coverSheet, "cover_sheet.png");
            frameSettings = {};
        }

        renderer.shutdown();
        output.release();
        std::printf ("wrote %s\n", options.outDir.getFullPathName().toRawUTF8());
        return ok;
    }

    /** Renders one frame into our own 8-bit target rather than the tiny window. */
    void renderFrame (cortex::Renderer& renderer, const cortex::RenderInputs& inputs)
    {
        cortex::FrameSettings settings = frameSettings;
        settings.exact = true;

        GLint fbo = 0;
        output.bind();
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &fbo);
        renderer.render (inputs, options.width, options.height, (GLuint) fbo, 0, 0, options.width, options.height, settings);
    }

    juce::Image readOutput()
    {
        std::vector<uint8_t> pixels ((size_t) (options.width * options.height * 4));
        output.bind();
        glPixelStorei (GL_PACK_ALIGNMENT, 1);
        glReadPixels (0, 0, options.width, options.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        juce::Image image (juce::Image::RGB, options.width, options.height, false, juce::SoftwareImageType());
        juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < options.height; ++y)
            for (int x = 0; x < options.width; ++x)
            {
                const auto* p = pixels.data() + ((size_t) (options.height - 1 - y) * (size_t) options.width + (size_t) x) * 4;
                bitmap.setPixelColour (x, y, juce::Colour (p[0], p[1], p[2]));
            }
        return image;
    }

    void write (const juce::Image& image, const juce::String& name)
    {
        if (image.isNull())
            return;
        auto file = options.outDir.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    /** Plays the track from the start and returns the frame just after `kickTime`. */
    juce::Image captureAt (cortex::Renderer& renderer, const Track& track, double kickTime)
    {
        cortex::FrameInterpolator interpolator;
        size_t nextFrame = 0;
        const double fps = 60.0;
        const int totalFrames = (int) std::ceil (kickTime * fps) + 1;

        for (int frame = 0; frame <= totalFrames; ++frame)
        {
            const double t = frame / fps;
            while (nextFrame < track.frames.size() && (double) track.frames[nextFrame].samplePos / 48000.0 <= t)
                interpolator.push (track.frames[nextFrame++]);
            renderFrame (renderer, interpolator.update ((float) (1.0 / fps)));
        }
        return readOutput();
    }

    void writeSheet (const juce::Array<juce::Image>& images, const juce::String& name)
    {
        if (images.isEmpty())
            return;

        constexpr int columns = 3, cellW = 640;
        const int cellH = cellW * options.height / options.width;
        const int rowsNeeded = (images.size() + columns - 1) / columns;
        juce::Image sheet (juce::Image::RGB, columns * cellW, rowsNeeded * cellH, true, juce::SoftwareImageType());
        juce::Graphics g (sheet);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (int i = 0; i < images.size(); ++i)
            g.drawImage (images[i], juce::Rectangle<float> ((float) ((i % columns) * cellW), (float) ((i / columns) * cellH), (float) cellW, (float) cellH));
        write (sheet, name);
    }

    Options options;
    cortex::FrameSettings frameSettings;
    juce::OpenGLContext context;
    cortex::gfx::RenderTarget output;
    bool finished = false;
};

class SnapshotApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "cortex_snapshot"; }
    const juce::String getApplicationVersion() override    { return "1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        runner = std::make_unique<SnapshotRunner> (parse (getCommandLineParameterArray()));
        window = std::make_unique<juce::DocumentWindow> ("cortex_snapshot", juce::Colours::black, 0);
        window->setUsingNativeTitleBar (true);
        window->setContentNonOwned (runner.get(), true);
        window->setTopLeftPosition (40, 40);
        window->setVisible (true);
        window->toBack();   // stay behind whatever the user is working in
    }

    void shutdown() override
    {
        const int code = runner != nullptr ? runner->exitCode : 1;
        window = nullptr;
        runner = nullptr;
        setApplicationReturnValue (code);
    }

    void systemRequestedQuit() override   { quit(); }

private:
    std::unique_ptr<SnapshotRunner> runner;
    std::unique_ptr<juce::DocumentWindow> window;
};

} // namespace

START_JUCE_APPLICATION (SnapshotApp)
