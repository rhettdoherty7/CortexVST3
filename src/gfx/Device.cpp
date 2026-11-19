#include "Device.h"

#include <CortexShaders.h>

namespace cortex::gfx
{

// --- Shader --------------------------------------------------------------------------------------

namespace
{
    GLuint compileStage (GLenum type, const std::string& source, std::string& log)
    {
        const GLuint shader = glCreateShader (type);
        const char* text = source.c_str();
        glShaderSource (shader, 1, &text, nullptr);
        glCompileShader (shader);

        GLint ok = GL_FALSE;
        glGetShaderiv (shader, GL_COMPILE_STATUS, &ok);
        if (ok == GL_TRUE)
            return shader;

        GLint length = 0;
        glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &length);
        std::string info ((size_t) std::max (length, 1), '\0');
        glGetShaderInfoLog (shader, length, nullptr, info.data());
        log += (type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + info + "\n";
        glDeleteShader (shader);
        return 0;
    }
}

bool Shader::build (const std::string& vertexSource, const std::string& fragmentSource, std::string& log)
{
    const GLuint vs = compileStage (GL_VERTEX_SHADER, vertexSource, log);
    const GLuint fs = compileStage (GL_FRAGMENT_SHADER, fragmentSource, log);

    if (vs == 0 || fs == 0)
    {
        if (vs != 0) glDeleteShader (vs);
        if (fs != 0) glDeleteShader (fs);
        return false;
    }

    const GLuint linked = glCreateProgram();
    glAttachShader (linked, vs);
    glAttachShader (linked, fs);
    glLinkProgram (linked);
    glDeleteShader (vs);
    glDeleteShader (fs);

    GLint ok = GL_FALSE;
    glGetProgramiv (linked, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        GLint length = 0;
        glGetProgramiv (linked, GL_INFO_LOG_LENGTH, &length);
        std::string info ((size_t) std::max (length, 1), '\0');
        glGetProgramInfoLog (linked, length, nullptr, info.data());
        log += "link: " + info + "\n";
        glDeleteProgram (linked);
        return false;
    }

    release();
    program = linked;
    return true;
}

void Shader::release()
{
    if (program != 0)
        glDeleteProgram (program);
    program = 0;
    cacheSize = 0;
}

GLint Shader::location (const char* name) noexcept
{
    for (int i = 0; i < cacheSize; ++i)
        if (cache[(size_t) i].name == name)
            return cache[(size_t) i].location;

    const GLint found = glGetUniformLocation (program, name);
    if (cacheSize < (int) cache.size())
        cache[(size_t) cacheSize++] = { name, found };
    return found;
}

void Shader::setTexture (const char* name, int unit, GLuint texture) noexcept
{
    glActiveTexture ((GLenum) (GL_TEXTURE0 + unit));
    glBindTexture (GL_TEXTURE_2D, texture);
    glUniform1i (location (name), unit);
}

// --- RenderTarget --------------------------------------------------------------------------------

void RenderTarget::ensure (int width, int height, GLenum internalFormat, bool linearFilter, bool mipmaps)
{
    width = std::max (width, 1);
    height = std::max (height, 1);
    if (fbo != 0 && width == w && height == h && internalFormat == format && linearFilter == linear && mipmaps == mips)
        return;

    release();
    w = width;
    h = height;
    format = internalFormat;
    linear = linearFilter;
    mips = mipmaps;

    glGenTextures (1, &tex);
    glBindTexture (GL_TEXTURE_2D, tex);
    glTexImage2D (GL_TEXTURE_2D, 0, (GLint) format, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : (linear ? GL_LINEAR : GL_NEAREST));
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (mips)
        glGenerateMipmap (GL_TEXTURE_2D);

    glGenFramebuffers (1, &fbo);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    jassert (glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
    glClear (GL_COLOR_BUFFER_BIT);
}

void RenderTarget::release()
{
    if (fbo != 0) glDeleteFramebuffers (1, &fbo);
    if (tex != 0) glDeleteTextures (1, &tex);
    fbo = tex = 0;
    w = h = 0;
}

void RenderTarget::bind() const noexcept
{
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glViewport (0, 0, w, h);
}

void RenderTarget::clear (float r, float g, float b, float a) const noexcept
{
    bind();
    glClearColor (r, g, b, a);
    glClear (GL_COLOR_BUFFER_BIT);
}

void RenderTarget::generateMipmaps() const noexcept
{
    glBindTexture (GL_TEXTURE_2D, tex);
    glGenerateMipmap (GL_TEXTURE_2D);
}

// --- DataTexture ---------------------------------------------------------------------------------

void DataTexture::ensure (int width, int height, GLenum internalFormat, GLenum newDataFormat, bool linearFilter)
{
    if (tex != 0 && width == w && height == h)
        return;

    release();
    w = width;
    h = height;
    dataFormat = newDataFormat;

    glGenTextures (1, &tex);
    glBindTexture (GL_TEXTURE_2D, tex);
    glTexImage2D (GL_TEXTURE_2D, 0, (GLint) internalFormat, w, h, 0, dataFormat, GL_FLOAT, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void DataTexture::upload (const float* data) const noexcept
{
    uploadRows (data, 0, h);
}

void DataTexture::uploadRows (const float* data, int firstRow, int rowCount) const noexcept
{
    glBindTexture (GL_TEXTURE_2D, tex);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D (GL_TEXTURE_2D, 0, 0, firstRow, w, rowCount, dataFormat, GL_FLOAT, data);
}

void DataTexture::release()
{
    if (tex != 0) glDeleteTextures (1, &tex);
    tex = 0;
    w = h = 0;
}

// --- Device --------------------------------------------------------------------------------------

void Device::init()
{
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glDisable (GL_SCISSOR_TEST);
    glEnable (GL_PROGRAM_POINT_SIZE);
}

void Device::shutdown()
{
    if (vao != 0) glDeleteVertexArrays (1, &vao);
    vao = 0;
}

void Device::drawFullscreen() const noexcept
{
    glBindVertexArray (vao);
    glDrawArrays (GL_TRIANGLES, 0, 3);
}

void Device::drawInstancedQuads (int instances) const noexcept
{
    glBindVertexArray (vao);
    glDrawArraysInstanced (GL_TRIANGLE_STRIP, 0, 4, instances);
}

void Device::drawPoints (int count) const noexcept
{
    glBindVertexArray (vao);
    glDrawArrays (GL_POINTS, 0, count);
}

void Device::bindBackbuffer (GLuint framebuffer, int x, int y, int width, int height) noexcept
{
    glBindFramebuffer (GL_FRAMEBUFFER, framebuffer);
    glViewport (x, y, width, height);
}

void Device::setBlend (Blend mode) noexcept
{
    if (mode == Blend::none)
    {
        glDisable (GL_BLEND);
        return;
    }

    glEnable (GL_BLEND);
    glBlendEquation (mode == Blend::maximum ? GL_MAX : GL_FUNC_ADD);

    switch (mode)
    {
        case Blend::alpha:    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
        case Blend::screen:   glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
        case Blend::multiply: glBlendFunc (GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
        case Blend::additive:
        case Blend::maximum:
        case Blend::none:     glBlendFunc (GL_ONE, GL_ONE); break;
    }
}

// --- ShaderSource --------------------------------------------------------------------------------

std::string ShaderSource::load (const char* fileName)
{
   #if defined (CORTEX_SHADER_DIR) && JUCE_DEBUG
    const juce::File onDisk = juce::File (CORTEX_SHADER_DIR).getChildFile (fileName);
    if (onDisk.existsAsFile())
        return onDisk.loadFileAsString().toStdString();
   #endif

    // BinaryData names replace every non-identifier character with an underscore.
    const auto resource = juce::String (fileName).replaceCharacters (".-", "__");
    int size = 0;
    if (const char* data = CortexShaders::getNamedResource (resource.toRawUTF8(), size))
        return std::string (data, (size_t) size);

    jassertfalse;   // shader missing from shaders/CMake list
    return {};
}

long long ShaderSource::latestModification()
{
   #if defined (CORTEX_SHADER_DIR) && JUCE_DEBUG
    long long newest = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (juce::File (CORTEX_SHADER_DIR), false, "*.glsl"))
        newest = std::max (newest, (long long) entry.getFile().getLastModificationTime().toMilliseconds());
    return newest;
   #else
    return 0;
   #endif
}

} // namespace cortex::gfx
