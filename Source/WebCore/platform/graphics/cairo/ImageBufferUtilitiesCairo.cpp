/*
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2007 Holger Hans Peter Freyther <zecke@selfish.org>
 * Copyright (C) 2008, 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 * Copyright (C) 2010 Igalia S.L.
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ImageBufferUtilitiesCairo.h"

#if USE(CAIRO)

#include "CairoUtilities.h"
#include "MIMETypeRegistry.h"
#include "RefPtrCairo.h"
#include <cairo.h>
#include <wtf/text/Base64.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

#if PLATFORM(GTK)
#include "GRefPtrGtk.h"
#include "GdkCairoUtilities.h"
#include <gtk/gtk.h>
#include <wtf/glib/GUniquePtr.h>
#endif

namespace WebCore {

#if !PLATFORM(GTK)
static cairo_status_t writeFunction(void* output, const unsigned char* data, unsigned length)
{
    if (!reinterpret_cast<Vector<uint8_t>*>(output)->tryAppend(std::span { data, length }))
        return CAIRO_STATUS_WRITE_ERROR;
    return CAIRO_STATUS_SUCCESS;
}

static bool encodeImage(cairo_surface_t* image, const String& mimeType, Vector<uint8_t>* output)
{
    ASSERT_UNUSED(mimeType, mimeType == "image/png"_s); // Only PNG output is supported for now.

    return cairo_surface_write_to_png_stream(image, writeFunction, output) == CAIRO_STATUS_SUCCESS;
}

Vector<uint8_t> encodeData(cairo_surface_t* image, const String& mimeType, std::optional<double>)
{
    Vector<uint8_t> encodedImage;
    if (!image || !encodeImage(image, mimeType, &encodedImage))
        return { };
    return encodedImage;
}
#else
static bool encodeImage(cairo_surface_t* surface, const String& mimeType, std::optional<double> quality, GUniqueOutPtr<gchar>& buffer, gsize& bufferSize)
{
    // List of supported image encoding types comes from the GdkPixbuf documentation.
    // http://developer.gnome.org/gdk-pixbuf/stable/gdk-pixbuf-File-saving.html#gdk-pixbuf-save-to-bufferv

    String type = mimeType.substring(sizeof "image");
    if (type != "jpeg"_s && type != "png"_s && type != "tiff"_s && type != "ico"_s && type != "bmp"_s)
        return false;

    auto pixbuf = cairoSurfaceToGdkPixbuf(surface);
    if (!pixbuf)
        return false;

    GUniqueOutPtr<GError> error;
    if (type == "jpeg"_s && quality && *quality >= 0.0 && *quality <= 1.0) {
        String qualityString = String::number(static_cast<int>(*quality * 100.0 + 0.5));
        gdk_pixbuf_save_to_buffer(pixbuf.get(), &buffer.outPtr(), &bufferSize, type.utf8().data(), &error.outPtr(), "quality", qualityString.utf8().data(), NULL);
    } else
        gdk_pixbuf_save_to_buffer(pixbuf.get(), &buffer.outPtr(), &bufferSize, type.utf8().data(), &error.outPtr(), NULL);

    return !error;
}

Vector<uint8_t> encodeData(cairo_surface_t* image, const String& mimeType, std::optional<double> quality)
{
    GUniqueOutPtr<gchar> buffer;
    gsize bufferSize;
    if (!encodeImage(image, mimeType, quality, buffer, bufferSize))
        return { };

    return std::span { reinterpret_cast<const uint8_t*>(buffer.get()), bufferSize };
}
#endif // !PLATFORM(GTK)

} // namespace WebCore

#endif // USE(CAIRO)
