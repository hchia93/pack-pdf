#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace packpdf
{
    // Coarse classification used at intake (drag-drop, CLI parse). The
    // TimelineRow variant collapses PDF vs image at storage time; for images
    // the JPEG/PNG distinction is preserved in ImageOptions::format.
    enum class FileExtension : uint8_t { PDF, JPEG, PNG };

    // Decode hint stored in ImageOptions. Only ever JPEG or PNG, never PDF.
    enum class ImageFormat : uint8_t { JPEG, PNG };

    inline ImageFormat ImageFormatFromExtension(FileExtension e)
    {
        return (e == FileExtension::PNG) ? ImageFormat::PNG : ImageFormat::JPEG;
    }

    // Nullopt for extensions PackPDF cannot process — callers drop silently.
    inline std::optional<FileExtension> FileExtensionFromPath(const std::string& path)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) 
        { 
            return static_cast<char>(std::tolower(c)); 
        });

        if (ext == ".pdf")                    
        {
            return FileExtension::PDF;
        }
        if (ext == ".jpg" || ext == ".jpeg")  
        {
            return FileExtension::JPEG;
        }
        if (ext == ".png")                    
        {
            return FileExtension::PNG;
        }
        return std::nullopt;
    }
}
