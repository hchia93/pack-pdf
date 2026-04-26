// Define before any include: PDFium headers pull in windows.h indirectly,
// and that drag in the min/max macros which clash with std::min/std::max.
#define NOMINMAX

#include "Composer.h"

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdf_transformpage.h>

// STB_IMAGE_IMPLEMENTATION is defined in ImageCache.cpp; this is a pure include.
#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

namespace packpdf
{
    std::filesystem::path Utf8ToPath(const std::string& utf8)
    {
    #ifdef _WIN32
        if (utf8.empty())
        {
            return {};
        }
        int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                         static_cast<int>(utf8.size()),
                                         nullptr, 0);
        if (wlen <= 0)
        {
            return std::filesystem::path(utf8);
        }
        std::wstring w(static_cast<size_t>(wlen), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                              static_cast<int>(utf8.size()),
                              w.data(), wlen);
        return std::filesystem::path(w);
    #else
        return std::filesystem::path(utf8);
    #endif
    }

    namespace
    {
        // PDF points (1 pt = 1/72 inch). A4 = 210 × 297 mm.
        constexpr double kA4PortraitW = 595.275591;  // 210 mm
        constexpr double kA4PortraitH = 841.889764;  // 297 mm

        // Cap (w, h) in place to fit within an A4 sheet matching the page's
        // own orientation (landscape page → landscape A4). Never upscales.
        // Returns the scale factor applied (1.0 = unchanged).
        double CapToA4(double& w, double& h)
        {
            const bool   landscape = w > h;
            const double maxW = landscape ? kA4PortraitH : kA4PortraitW;
            const double maxH = landscape ? kA4PortraitW : kA4PortraitH;
            if (w <= maxW && h <= maxH)
            {
                return 1.0;
            }
            const double s = std::min(maxW / w, maxH / h);
            w *= s;
            h *= s;
            return s;
        }

        // Scales a PDF page in place so its MediaBox fits within A4.
        // No-op when the page is already small enough.
        void CapPageToA4(FPDF_PAGE page)
        {
            float left = 0, bottom = 0, right = 0, top = 0;
            if (!FPDFPage_GetMediaBox(page, &left, &bottom, &right, &top))
            {
                return;
            }
            double w = static_cast<double>(right - left);
            double h = static_cast<double>(top - bottom);
            if (w <= 0.0 || h <= 0.0)
            {
                return;
            }

            double newW = w, newH = h;
            const double s = CapToA4(newW, newH);
            if (s >= 1.0)
            {
                return;
            }

            // Apply scale + translate to all page contents so the original
            // (left, bottom) origin maps to (0, 0) of the new MediaBox.
            FS_MATRIX m{ static_cast<float>(s), 0.0f, 0.0f, static_cast<float>(s),
                         static_cast<float>(-left * s),
                         static_cast<float>(-bottom * s) };
            FS_RECTF clip{ 0.0f, 0.0f,
                           static_cast<float>(newW), static_cast<float>(newH) };
            FPDFPage_TransFormWithClip(page, &m, &clip);

            FPDFPage_SetMediaBox(page, 0.0f, 0.0f,
                                 static_cast<float>(newW), static_cast<float>(newH));
            FPDFPage_GenerateContent(page);
        }

        void EnsurePdfiumInit()
        {
            static bool s_initialized = false;
            if (s_initialized)
            {
                return;
            }
            FPDF_LIBRARY_CONFIG cfg{};
            cfg.version = 2;
            FPDF_InitLibraryWithConfig(&cfg);
            s_initialized = true;
        }

        std::vector<uint8_t> ReadAll(const std::filesystem::path& p, std::string& err)
        {
            std::ifstream f(p, std::ios::binary | std::ios::ate);
            if (!f)
            {
                err = "cannot open input";
                return {};
            }
            std::streamsize size = f.tellg();
            if (size < 0)
            {
                err = "cannot tell input size";
                return {};
            }
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> buf(static_cast<size_t>(size));
            if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size))
            {
                err = "read error";
                return {};
            }
            return buf;
        }

        // Build the 1-indexed page-list spec FPDF_ImportPages expects.
        //   nullopt        → no pages to import; caller should skip this segment.
        //   present empty  → all pages (FPDF_ImportPages accepts NULL spec).
        //   present non-empty → spec like "1-3,5".
        std::optional<std::string>
        BuildImportSpec(PageSelection sel, int first, int last, int totalPages)
        {
            if (sel == PageSelection::All)
            {
                return std::string{};
            }

            auto fmt = [](int a, int b) {
                return a == b ? std::to_string(a)
                              : std::to_string(a) + '-' + std::to_string(b);
            };

            const int a = std::max(1, first);
            const int b = std::min(totalPages, last);

            if (sel == PageSelection::Range)
            {
                if (a > b)
                {
                    return std::nullopt;
                }
                return fmt(a, b);
            }

            // Exclude: keep [1, a-1] ∪ [b+1, totalPages].
            const std::string left  = (a > 1)          ? fmt(1, a - 1)         : "";
            const std::string right = (b < totalPages) ? fmt(b + 1, totalPages) : "";
            if (left.empty() && right.empty())
            {
                return std::nullopt;
            }
            if (left.empty())
            {
                return right;
            }
            if (right.empty())
            {
                return left;
            }
            return left + ',' + right;
        }

        // Composition (not inheritance) so layout matches the C "extending
        // a struct by trailing fields" idiom: `base` sits at offset 0 and a
        // pointer to the whole context is reinterpret-castable to FPDF_FILEWRITE*.
        struct OfstreamWriterCtx
        {
            FPDF_FILEWRITE base{};
            std::ofstream* out = nullptr;
            bool           ok  = true;
        };

        int OfstreamWriteBlock(FPDF_FILEWRITE* pThis, const void* data, unsigned long size)
        {
            auto* ctx = reinterpret_cast<OfstreamWriterCtx*>(pThis);
            ctx->out->write(static_cast<const char*>(data),
                            static_cast<std::streamsize>(size));
            if (!ctx->out->good())
            {
                ctx->ok = false;
                return 0;
            }
            return 1;
        }

        struct JpegMemContext
        {
            const uint8_t* data;
            unsigned long  size;
        };

        int JpegMemRead(void* param, unsigned long pos, unsigned char* pBuf, unsigned long sz)
        {
            auto* c = static_cast<JpegMemContext*>(param);
            if (pos + sz > c->size)
            {
                return 0;
            }
            std::memcpy(pBuf, c->data + pos, sz);
            return 1;
        }

        // White margin around image content when addPadding=true.
        constexpr double kPaddingPts = 36.0;  // 0.5 inch

        struct ImgMatrix { double a, b, c, d, e, f; };

        // Build the matrix that places the image's unit square (rotated by
        // `rot` degrees CCW) into the rect at (x, y) sized (w, h). w/h are
        // the post-rotation displayed dimensions.
        ImgMatrix MakeImageMatrix(double x, double y, double w, double h, int rot)
        {
            switch (((rot % 360) + 360) % 360)
            {
                default:
                case 0:   return {  w,  0.0,  0.0,  h, x,         y         };
                case 90:  return { 0.0,  h,  -w,   0.0, x + w,    y         };
                case 180: return { -w,  0.0,  0.0, -h, x + w,    y + h     };
                case 270: return { 0.0, -h,   w,   0.0, x,        y + h     };
            }
        }

        bool LoadJpegImageObj(FPDF_DOCUMENT dest,
                              const std::vector<uint8_t>& jpeg,
                              FPDF_PAGEOBJECT* outImg,
                              int* outPxW, int* outPxH,
                              std::string& err)
        {
            // FILEACCESS only needs to live for the LoadJpegFileInline call:
            // "Inline" decodes the data immediately into the new image object.
            JpegMemContext ctx{ jpeg.data(), static_cast<unsigned long>(jpeg.size()) };
            FPDF_FILEACCESS access{};
            access.m_FileLen  = ctx.size;
            access.m_GetBlock = &JpegMemRead;
            access.m_Param    = &ctx;

            FPDF_PAGEOBJECT img = FPDFPageObj_NewImageObj(dest);
            if (!img) { err = "FPDFPageObj_NewImageObj failed"; return false; }

            if (!FPDFImageObj_LoadJpegFileInline(nullptr, 0, img, &access))
            {
                FPDFPageObj_Destroy(img);
                err = "FPDFImageObj_LoadJpegFileInline failed";
                return false;
            }

            unsigned int pxW = 0, pxH = 0;
            if (!FPDFImageObj_GetImagePixelSize(img, &pxW, &pxH) || pxW == 0 || pxH == 0)
            {
                FPDFPageObj_Destroy(img);
                err = "FPDFImageObj_GetImagePixelSize failed";
                return false;
            }
            *outImg = img;
            *outPxW = static_cast<int>(pxW);
            *outPxH = static_cast<int>(pxH);
            return true;
        }

        // Decodes a PNG via stb_image, swizzles RGBA → BGRA, then hands the
        // pixels to PDFium via FPDFImageObj_SetBitmap. SetBitmap deep-copies
        // the data into the image object, so we release the source buffer
        // before returning.
        bool LoadPngImageObj(FPDF_DOCUMENT dest,
                             const std::vector<uint8_t>& png,
                             FPDF_PAGEOBJECT* outImg,
                             int* outPxW, int* outPxH,
                             std::string& err)
        {
            int w = 0, h = 0, comp = 0;
            unsigned char* rgba = stbi_load_from_memory(
                png.data(), static_cast<int>(png.size()),
                &w, &h, &comp, 4);
            if (!rgba || w <= 0 || h <= 0)
            {
                if (rgba)
                {
                    stbi_image_free(rgba);
                }
                err = "stbi_load_from_memory failed";
                return false;
            }

            // stb returns RGBA; PDFium's BGRA format wants the red and blue
            // channels swapped per pixel.
            const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
            for (size_t p = 0; p < pixelCount; ++p)
            {
                std::swap(rgba[p * 4 + 0], rgba[p * 4 + 2]);
            }

            FPDF_BITMAP bm = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA,
                                                 rgba, w * 4);
            if (!bm)
            {
                stbi_image_free(rgba);
                err = "FPDFBitmap_CreateEx failed";
                return false;
            }

            FPDF_PAGEOBJECT img = FPDFPageObj_NewImageObj(dest);
            if (!img)
            {
                FPDFBitmap_Destroy(bm);
                stbi_image_free(rgba);
                err = "FPDFPageObj_NewImageObj failed";
                return false;
            }

            if (!FPDFImageObj_SetBitmap(nullptr, 0, img, bm))
            {
                FPDFPageObj_Destroy(img);
                FPDFBitmap_Destroy(bm);
                stbi_image_free(rgba);
                err = "FPDFImageObj_SetBitmap failed";
                return false;
            }

            FPDFBitmap_Destroy(bm);
            stbi_image_free(rgba);

            *outImg = img;
            *outPxW = w;
            *outPxH = h;
            return true;
        }

        bool LoadImageObj(FPDF_DOCUMENT dest, FileExtension fileType,
                          const std::vector<uint8_t>& bytes,
                          FPDF_PAGEOBJECT* outImg, int* outPxW, int* outPxH,
                          std::string& err)
        {
            switch (fileType)
            {
                case FileExtension::JPEG:
                    return LoadJpegImageObj(dest, bytes, outImg, outPxW, outPxH, err);
                case FileExtension::PNG:
                    return LoadPngImageObj(dest, bytes, outImg, outPxW, outPxH, err);
                default:
                    err = "not an image file type";
                    return false;
            }
        }

        inline bool IsImageFileType(FileExtension k)
        {
            return k == FileExtension::JPEG || k == FileExtension::PNG;
        }

        // Position an image inside an arbitrary rect (used by merged-page slots).
        struct ImageInRect
        {
            double imgX, imgY, imgW, imgH;
            int    rotationCcw;
        };

        ImageInRect ComputeImageInRect(int pxW, int pxH, const Segment& seg,
                                       double rectX, double rectY,
                                       double rectW, double rectH)
        {
            ImageInRect R{};
            R.rotationCcw = RotationCcwFor(pxW, pxH, seg.orientation, seg.reverse180);

            const double dpi = 100.0;
            double imgNatW, imgNatH;
            if (R.rotationCcw == 90 || R.rotationCcw == 270)
            {
                imgNatW = pxH * 72.0 / dpi;
                imgNatH = pxW * 72.0 / dpi;
            }
            else
            {
                imgNatW = pxW * 72.0 / dpi;
                imgNatH = pxH * 72.0 / dpi;
            }

            const double availW = rectW - (seg.addPadding ? 2.0 * kPaddingPts : 0.0);
            const double availH = rectH - (seg.addPadding ? 2.0 * kPaddingPts : 0.0);
            const double fit = std::min(availW / imgNatW, availH / imgNatH);
            const double s   = (seg.scaleMode == ImageScaleMode::FitPage)
                             ? fit                   // small images upscale to fill the rect
                             : std::min(1.0, fit);   // small images keep natural size
            R.imgW = imgNatW * s;
            R.imgH = imgNatH * s;
            R.imgX = rectX + (rectW - R.imgW) * 0.5;
            R.imgY = rectY + (rectH - R.imgH) * 0.5;
            return R;
        }

        bool AppendImagePage(FPDF_DOCUMENT dest, int& insertIndex,
                             const std::vector<uint8_t>& bytes, const Segment& seg,
                             std::string& err)
        {
            FPDF_PAGEOBJECT img = nullptr;
            int pxW = 0, pxH = 0;
            if (!LoadImageObj(dest, seg.fileType, bytes, &img, &pxW, &pxH, err))
            {
                return false;
            }

            ImagePageLayout L = ComputeImagePageLayout(pxW, pxH, seg);

            FPDF_PAGE page = FPDFPage_New(dest, insertIndex, L.pageW, L.pageH);
            if (!page)
            {
                FPDFPageObj_Destroy(img);
                err = "FPDFPage_New failed";
                return false;
            }

            const ImgMatrix m = MakeImageMatrix(L.imgX, L.imgY, L.imgW, L.imgH, L.rotationCcw);
            FPDFImageObj_SetMatrix(img, m.a, m.b, m.c, m.d, m.e, m.f);
            FPDFPage_InsertObject(page, img); // ownership transfers to page
            FPDFPage_GenerateContent(page);
            FPDF_ClosePage(page);

            ++insertIndex;
            return true;
        }

        // AutoMerge stack: one A4-portrait page, top half holds `top`, bottom
        // half holds `bot` (or stays blank if bot is null).
        bool AppendMergedImagePage(FPDF_DOCUMENT dest, int& insertIndex,
                                   const std::vector<uint8_t>& topBytes, const Segment& topSeg,
                                   const std::vector<uint8_t>* botBytes, const Segment* botSeg,
                                   std::string& err)
        {
            const double pageW = kA4PortraitW;
            const double pageH = kA4PortraitH;
            const double half  = pageH * 0.5;

            FPDF_PAGE page = FPDFPage_New(dest, insertIndex, pageW, pageH);
            if (!page) { err = "FPDFPage_New failed"; return false; }

            auto place = [&](const std::vector<uint8_t>& bytes, const Segment& seg,
                             double rectY) -> bool {
                FPDF_PAGEOBJECT img = nullptr;
                int pxW = 0, pxH = 0;
                if (!LoadImageObj(dest, seg.fileType, bytes, &img, &pxW, &pxH, err))
                {
                    return false;
                }
                ImageInRect R = ComputeImageInRect(pxW, pxH, seg, 0.0, rectY, pageW, half);
                const ImgMatrix m = MakeImageMatrix(R.imgX, R.imgY, R.imgW, R.imgH,
                                                    R.rotationCcw);
                FPDFImageObj_SetMatrix(img, m.a, m.b, m.c, m.d, m.e, m.f);
                FPDFPage_InsertObject(page, img);
                return true;
            };

            // Top slot (upper half of page).
            if (!place(topBytes, topSeg, half))
            {
                FPDFPage_GenerateContent(page);
                FPDF_ClosePage(page);
                return false;
            }

            // Bottom slot (only if a partner image was supplied).
            if (botBytes && botSeg)
            {
                if (!place(*botBytes, *botSeg, 0.0))
                {
                    FPDFPage_GenerateContent(page);
                    FPDF_ClosePage(page);
                    return false;
                }
            }

            FPDFPage_GenerateContent(page);
            FPDF_ClosePage(page);
            ++insertIndex;
            return true;
        }

    }

    ImagePageLayout ComputeImagePageLayout(int pxW, int pxH, const Segment& seg)
    {
        ImagePageLayout L{};
        L.rotationCcw = RotationCcwFor(pxW, pxH, seg.orientation, seg.reverse180);

        // Image's natural display dimensions in PDF points (post-rotation).
        // 100 DPI matches the legacy Python prototype's PIL default.
        const double dpi = 100.0;
        double imgNatW, imgNatH;
        if (L.rotationCcw == 90 || L.rotationCcw == 270)
        {
            imgNatW = pxH * 72.0 / dpi;
            imgNatH = pxW * 72.0 / dpi;
        }
        else
        {
            imgNatW = pxW * 72.0 / dpi;
            imgNatH = pxH * 72.0 / dpi;
        }

        // Page is always A4 portrait. Image is scaled DOWN to fit (with
        // optional 0.5" white margin) and centered. ScaleMode controls
        // small images: Original keeps native size and the surrounding
        // whitespace IS the automatic A4-shaped padding; FitPage upscales
        // the image to fill the page minus padding.
        L.pageW = kA4PortraitW;
        L.pageH = kA4PortraitH;
        const double availW = L.pageW - (seg.addPadding ? 2.0 * kPaddingPts : 0.0);
        const double availH = L.pageH - (seg.addPadding ? 2.0 * kPaddingPts : 0.0);
        const double fit = std::min(availW / imgNatW, availH / imgNatH);
        const double s   = (seg.scaleMode == ImageScaleMode::FitPage)
                         ? fit
                         : std::min(1.0, fit);
        L.imgW = imgNatW * s;
        L.imgH = imgNatH * s;
        L.imgX = (L.pageW - L.imgW) * 0.5;
        L.imgY = (L.pageH - L.imgH) * 0.5;
        return L;
    }

    ComposeResult ComposeToFile(const SegmentList& segments,
                                const std::filesystem::path& outputPath)
    {
        ComposeResult r;
        if (segments.empty())
        {
            r.errorMessage = "timeline is empty";
            return r;
        }

        EnsurePdfiumInit();

        FPDF_DOCUMENT dest = FPDF_CreateNewDocument();
        if (!dest)
        {
            r.errorMessage = "FPDF_CreateNewDocument failed";
            return r;
        }

        // FPDF_LoadMemDocument keeps a reference to the input buffer; src docs
        // and their backing buffers must stay alive until SaveAsCopy returns.
        std::vector<std::vector<uint8_t>> srcBuffers;
        std::vector<FPDF_DOCUMENT>        srcDocs;
        srcBuffers.reserve(segments.size());
        srcDocs.reserve(segments.size());

        auto cleanup = [&]() {
            for (auto d : srcDocs)
            {
                FPDF_CloseDocument(d);
            }
            FPDF_CloseDocument(dest);
        };

        int insertIndex = 0;

        for (size_t i = 0; i < segments.size(); /* advanced inside */)
        {
            const Segment& seg = segments[i];

            std::filesystem::path srcPath = Utf8ToPath(seg.path);
            std::string ferr;
            std::vector<uint8_t> bytes = ReadAll(srcPath, ferr);
            if (bytes.empty())
            {
                r.errorMessage = ferr + ": " + seg.path;
                cleanup();
                return r;
            }

            switch (seg.fileType)
            {
                case FileExtension::PDF:
                {
                    FPDF_DOCUMENT src = FPDF_LoadMemDocument(bytes.data(),
                                                             static_cast<int>(bytes.size()),
                                                             nullptr);
                    if (!src)
                    {
                        r.errorMessage = "not a valid PDF: " + seg.path;
                        cleanup();
                        return r;
                    }

                    int pageCount = FPDF_GetPageCount(src);
                    auto specOpt = BuildImportSpec(seg.pageSelection,
                                                   seg.rangeFirst,
                                                   seg.rangeLast,
                                                   pageCount);
                    if (!specOpt.has_value())
                    {
                        // Selection resolved to zero pages; keep src alive
                        // (writers may reference it) but import nothing.
                        srcBuffers.push_back(std::move(bytes));
                        srcDocs.push_back(src);
                        break;
                    }

                    const char* specCStr = specOpt->empty() ? nullptr : specOpt->c_str();
                    int before = FPDF_GetPageCount(dest);
                    if (!FPDF_ImportPages(dest, src, specCStr, insertIndex))
                    {
                        FPDF_CloseDocument(src);
                        r.errorMessage = "FPDF_ImportPages failed: " + seg.path;
                        cleanup();
                        return r;
                    }
                    int added = FPDF_GetPageCount(dest) - before;

                    // Cap each newly-imported page at A4. Pages already <= A4
                    // are skipped inside CapPageToA4.
                    for (int p = 0; p < added; ++p)
                    {
                        FPDF_PAGE page = FPDF_LoadPage(dest, before + p);
                        if (page)
                        {
                            CapPageToA4(page);
                            FPDF_ClosePage(page);
                        }
                    }

                    insertIndex   += added;
                    r.pagesWritten += added;

                    srcBuffers.push_back(std::move(bytes));
                    srcDocs.push_back(src);
                    ++i;
                    break;
                }

                case FileExtension::JPEG:
                case FileExtension::PNG:
                {
                    // AutoMerge: peek the next segment. If it is also a
                    // landscape+autoMerge image (JPEG or PNG), stack both
                    // onto one A4 page.
                    bool consumedPair = false;
                    if (IsLandscapeMode(seg.orientation) && seg.autoMerge
                        && i + 1 < segments.size())
                    {
                        const Segment& nxt = segments[i + 1];
                        if (IsImageFileType(nxt.fileType)
                            && IsLandscapeMode(nxt.orientation))
                        {
                            std::filesystem::path nxtPath = Utf8ToPath(nxt.path);
                            std::string ferr2;
                            std::vector<uint8_t> bytes2 = ReadAll(nxtPath, ferr2);
                            if (bytes2.empty())
                            {
                                r.errorMessage = ferr2 + ": " + nxt.path;
                                cleanup();
                                return r;
                            }
                            std::string ierr;
                            if (!AppendMergedImagePage(dest, insertIndex,
                                                       bytes, seg, &bytes2, &nxt, ierr))
                            {
                                r.errorMessage = ierr + ": " + seg.path;
                                cleanup();
                                return r;
                            }
                            ++r.pagesWritten;
                            i += 2;
                            consumedPair = true;
                        }
                    }
                    if (consumedPair)
                    {
                        break;
                    }

                    if (IsLandscapeMode(seg.orientation) && seg.autoMerge)
                    {
                        // Lonely landscape with autoMerge: render alone in the
                        // top half so the user-visible "0.5 page" rule holds.
                        std::string ierr;
                        if (!AppendMergedImagePage(dest, insertIndex,
                                                   bytes, seg, nullptr, nullptr, ierr))
                        {
                            r.errorMessage = ierr + ": " + seg.path;
                            cleanup();
                            return r;
                        }
                        ++r.pagesWritten;
                        ++i;
                        break;
                    }

                    std::string ierr;
                    if (!AppendImagePage(dest, insertIndex, bytes, seg, ierr))
                    {
                        r.errorMessage = ierr + ": " + seg.path;
                        cleanup();
                        return r;
                    }
                    ++r.pagesWritten;
                    ++i;
                    break;
                }
            }
        }

        // Open the output file ourselves so non-ASCII output paths work.
        std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            r.errorMessage = "cannot open output for write";
            cleanup();
            return r;
        }

        OfstreamWriterCtx writer;
        writer.base.version    = 1;
        writer.base.WriteBlock = &OfstreamWriteBlock;
        writer.out             = &out;

        FPDF_BOOL saved = FPDF_SaveAsCopy(dest, &writer.base, FPDF_NO_INCREMENTAL);
        out.flush();
        out.close();

        cleanup();

        if (!saved || !writer.ok)
        {
            r.errorMessage = "FPDF_SaveAsCopy failed";
            return r;
        }

        r.ok = true;
        return r;
    }

    int GetPdfPageCount(const std::filesystem::path& pdfPath)
    {
        std::ifstream f(pdfPath, std::ios::binary | std::ios::ate);
        if (!f)
        {
            return -1;
        }
        std::streamsize sz = f.tellg();
        if (sz <= 0)
        {
            return -1;
        }
        f.seekg(0);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
        if (!f.read(reinterpret_cast<char*>(buf.data()), sz))
        {
            return -1;
        }

        EnsurePdfiumInit();
        FPDF_DOCUMENT doc = FPDF_LoadMemDocument(buf.data(),
                                                 static_cast<int>(buf.size()),
                                                 nullptr);
        if (!doc)
        {
            return -1;
        }
        const int n = FPDF_GetPageCount(doc);
        FPDF_CloseDocument(doc);
        return n;
    }
}
