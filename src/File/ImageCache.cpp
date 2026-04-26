#include "File/ImageCache.h"

#include "File/Composer.h"  // Utf8ToPath

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

#include <GLFW/glfw3.h>  // pulls in <GL/gl.h> with the right Windows prelude

// Default Windows <GL/gl.h> stops at OpenGL 1.1 and lacks GL 1.2+ enums.
// We use these constants directly without going through a loader.
#ifndef GL_CLAMP_TO_EDGE
  #define GL_CLAMP_TO_EDGE 0x812F
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include <stb_image.h>

#include <cstdint>
#include <fstream>
#include <vector>

namespace packpdf
{
    ImageCache::~ImageCache()
    {
        for (auto& kv : m_Entries)
        {
            if (kv.second.tex)
            {
                GLuint id = kv.second.tex;
                glDeleteTextures(1, &id);
            }
        }
    }

    const ImageCache::Entry* ImageCache::Get(const std::string& path)
    {
        auto it = m_Entries.find(path);
        if (it != m_Entries.end())
        {
            return it->second.failed ? nullptr : &it->second;
        }

        Entry e;
        if (!Load(path, e))
        {
            e.failed = true;
            m_Entries.emplace(path, e);
            return nullptr;
        }
        auto ins = m_Entries.emplace(path, e);
        return &ins.first->second;
    }

    bool ImageCache::Load(const std::string& pathUtf8, Entry& e)
    {
        // Read the file via std::filesystem so non-ASCII Windows paths survive.
        std::filesystem::path p = Utf8ToPath(pathUtf8);
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f)
        {
            return false;
        }
        std::streamsize sz = f.tellg();
        if (sz <= 0)
        {
            return false;
        }
        f.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
        if (!f.read(reinterpret_cast<char*>(buf.data()), sz))
        {
            return false;
        }

        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load_from_memory(buf.data(), static_cast<int>(buf.size()), &w, &h, &ch, 4);
        if (!px)
        {
            return false;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        stbi_image_free(px);

        e.tex = tex;
        e.w   = w;
        e.h   = h;
        return true;
    }
}
