#pragma once

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <string>
#include <vector>

namespace cortex::gfx
{

using namespace juce::gl;

/** Thin layer over OpenGL 4.1 core (section 2). Everything here must be called on the GL thread.
    Kept small on purpose so a Metal or Vulkan backend can replace it later. */

// -------------------------------------------------------------------------------------------------

/** A compiled program with a small allocation-free uniform-location cache. Pass string literals as
    names: the cache is keyed on the pointer. */
class Shader
{
public:
    Shader() = default;
    ~Shader()   { release(); }
    Shader (const Shader&) = delete;
    Shader& operator= (const Shader&) = delete;

    /** Returns false and fills `log` if compiling or linking fails; the old program is kept. */
    bool build (const std::string& vertexSource, const std::string& fragmentSource, std::string& log);
    void release();

    bool valid() const noexcept   { return program != 0; }
    void use() const noexcept     { glUseProgram (program); }

    GLint location (const char* name) noexcept;

    void set (const char* name, float v) noexcept                      { glUniform1f (location (name), v); }
    void set (const char* name, int v) noexcept                        { glUniform1i (location (name), v); }
    void set (const char* name, float x, float y) noexcept             { glUniform2f (location (name), x, y); }
    void set (const char* name, float x, float y, float z) noexcept    { glUniform3f (location (name), x, y, z); }
    void set (const char* name, float x, float y, float z, float w) noexcept { glUniform4f (location (name), x, y, z, w); }
    void setFloats (const char* name, const float* v, int count) noexcept    { glUniform1fv (location (name), count, v); }
    void setVec3s (const char* name, const float* v, int count) noexcept     { glUniform3fv (location (name), count, v); }

    /** Binds `texture` to `unit` and points the sampler at it. */
    void setTexture (const char* name, int unit, GLuint texture) noexcept;

private:
    struct Entry { const char* name = nullptr; GLint location = -1; };

    GLuint program = 0;
    std::array<Entry, 64> cache {};
    int cacheSize = 0;
};

// -------------------------------------------------------------------------------------------------

/** A colour texture with a framebuffer. Default format is RGBA16F linear light (North Star Q1). */
class RenderTarget
{
public:
    RenderTarget() = default;
    ~RenderTarget()   { release(); }
    RenderTarget (const RenderTarget&) = delete;
    RenderTarget& operator= (const RenderTarget&) = delete;

    /** (Re)creates the target only if the size or format changed. */
    void ensure (int width, int height, GLenum internalFormat = GL_RGBA16F, bool linearFilter = true, bool mipmaps = false);
    void release();

    void bind() const noexcept;          // binds the framebuffer and sets the viewport
    void clear (float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f) const noexcept;
    void generateMipmaps() const noexcept;

    GLuint texture() const noexcept   { return tex; }
    int width() const noexcept        { return w; }
    int height() const noexcept       { return h; }
    bool valid() const noexcept       { return fbo != 0; }

private:
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    GLenum format = 0;
    bool linear = true, mips = false;
};

/** A single-channel float texture updated from the CPU each frame (spectrum, scope, histories). */
class DataTexture
{
public:
    DataTexture() = default;
    ~DataTexture()   { release(); }
    DataTexture (const DataTexture&) = delete;
    DataTexture& operator= (const DataTexture&) = delete;

    void ensure (int width, int height, GLenum internalFormat = GL_R32F, GLenum dataFormat = GL_RED, bool linearFilter = true);
    void upload (const float* data) const noexcept;
    void uploadRows (const float* data, int firstRow, int rowCount) const noexcept;
    void release();

    GLuint texture() const noexcept   { return tex; }
    int width() const noexcept        { return w; }
    int height() const noexcept       { return h; }

private:
    GLuint tex = 0;
    int w = 0, h = 0;
    GLenum dataFormat = GL_RED;
};

// -------------------------------------------------------------------------------------------------

/** Owns the one vertex array every draw uses. All geometry is generated in the vertex shader from
    gl_VertexID / gl_InstanceID, so there are no vertex buffers anywhere. */
class Device
{
public:
    void init();
    void shutdown();

    void drawFullscreen() const noexcept;                              // one triangle covering the target
    void drawInstancedQuads (int instances) const noexcept;            // 4-vertex strips
    void drawPoints (int count) const noexcept;

    static void bindBackbuffer (GLuint framebuffer, int x, int y, int width, int height) noexcept;

    enum class Blend { none, alpha, additive, maximum, screen, multiply };
    static void setBlend (Blend) noexcept;   // colours are premultiplied throughout

private:
    GLuint vao = 0;
};

/** Loads GLSL by file name from the embedded shader set, or (Debug builds) straight from the
    shaders/ folder so edits show up live. */
class ShaderSource
{
public:
    static std::string load (const char* fileName);

    /** Debug hot reload: newest modification time across shaders/, or 0 when reading from the binary. */
    static long long latestModification();
};

} // namespace cortex::gfx
