#pragma once

#include <string>
#include <unordered_map>

namespace packpdf
{
    // Lazy GL-texture cache for hover-preview tooltips. Decodes JPG/PNG via
    // stb_image, uploads RGBA8, owns textures until destruction.
    class ImageCache
    {
    public:
        struct Entry
        {
            unsigned int tex    = 0;   // GL texture id (0 means "not loaded")
            int          w      = 0;
            int          h      = 0;
            bool         failed = false;
        };

        ~ImageCache();

        // nullptr on decode/upload failure (cached so we don't retry every frame).
        const Entry* Get(const std::string& pathUtf8);

    private:
        bool Load(const std::string& pathUtf8, Entry& outEntry);

        std::unordered_map<std::string, Entry> m_Entries;
    };
}
