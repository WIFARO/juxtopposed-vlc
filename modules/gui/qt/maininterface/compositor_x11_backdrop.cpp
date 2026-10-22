// SPDX-License-Identifier: GPL-2.0-or-later
#include "compositor_x11_backdrop.hpp"
#include "compositor_x11_utils.hpp"
#include <QImage>
#include <QPainter>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>

Q_LOGGING_CATEGORY(lcX11Backdrop, "vlc.gui.video.backdrop.x11", QtWarningMsg)

namespace vlc {
namespace {

constexpr int fixedOne = 1 << 16;
xcb_render_fixed_t fixed(qreal value) { return std::lround(value * fixedOne); }
const xcb_render_transform_t identity{fixedOne, 0, 0, 0, fixedOne, 0, 0, 0, fixedOne};

bool succeeded(xcb_connection_t *conn, xcb_void_cookie_t cookie)
{
    auto *error = xcb_request_check(conn, cookie);
    if (!error) return true;
    qCWarning(lcX11Backdrop) << "XRender backdrop request failed:" << error->error_code;
    free(error);
    return false;
}

struct Surface
{
    PixmapPtr pixmap;
    PicturePtr picture;
    QSize size;
    explicit Surface(xcb_connection_t *conn) : pixmap(conn), picture(conn) {}
    bool create(xcb_connection_t *conn, xcb_drawable_t drawable, const QSize &next,
                xcb_render_pictformat_t format, uint8_t depth)
    {
        if (picture && size == next) return true;
        picture.reset(); pixmap.reset(); size = {};
        const auto nextPixmap = xcb_generate_id(conn);
        if (!succeeded(conn, xcb_create_pixmap_checked(conn, depth, nextPixmap, drawable,
                                                      next.width(), next.height()))) return false;
        pixmap = nextPixmap;
        const auto nextPicture = xcb_generate_id(conn);
        const uint32_t repeat = XCB_RENDER_REPEAT_PAD;
        if (!succeeded(conn, xcb_render_create_picture_checked(conn, nextPicture, pixmap.get(),
                           format, XCB_RENDER_CP_REPEAT, &repeat)))
        { pixmap.reset(); return false; }
        picture = nextPicture;
        size = next;
        return true;
    }
};

std::vector<xcb_render_fixed_t> kernel(qreal sigma, bool horizontal)
{
    const int radius = std::ceil(3 * sigma);
    const int count = radius * 2 + 1;
    std::vector<qreal> weights(count);
    qreal total = 0;
    for (int n = 0; n < count; ++n)
        total += weights[n] = std::exp(-qreal((n - radius) * (n - radius)) / (2 * sigma * sigma));
    std::vector<xcb_render_fixed_t> result(count + 2);
    result[0] = (horizontal ? count : 1) * fixedOne;
    result[1] = (horizontal ? 1 : count) * fixedOne;
    int sum = 0;
    for (int n = 0; n < count; ++n)
        sum += result[n + 2] = fixed(weights[n] / total);
    result[radius + 2] += fixedOne - sum;
    return result;
}

int reductionFactor(qreal radius)
{
    int factor = 4;
    while (radius / factor > 8 && factor < 64) factor *= 2;
    return factor;
}

struct Region
{
    VideoBackdropRegion definition;
    QRect visibleRect, captureRect;
    std::vector<std::unique_ptr<Surface>> reductions;
    Surface opaque, horizontal, blurred, mask;
    qreal sigma = 0;
    int factor = 0;
    explicit Region(xcb_connection_t *conn)
        : opaque(conn), horizontal(conn), blurred(conn), mask(conn) {}
};

}

struct X11VideoBackdrop::Private
{
    xcb_connection_t *conn;
    xcb_drawable_t drawable;
    xcb_render_pictformat_t argb = 0, rgb = 0, alpha = 0;
    bool supported = false;
    quint64 revision = 0;
    std::map<quintptr, std::unique_ptr<Region>> regions;

    bool prepare(Region &region, const QRect &visible, const QRect &capture, int factor)
    {
        if (!region.opaque.create(conn, drawable, capture.size(), rgb, 24)) return false;
        int stages = 0;
        for (int scale = factor; scale > 1; scale /= 2) ++stages;
        while (int(region.reductions.size()) < stages)
            region.reductions.push_back(std::make_unique<Surface>(conn));
        region.reductions.resize(stages);
        for (int n = 0; n < stages; ++n)
            if (!region.reductions[n]->create(conn, drawable, capture.size() / (1 << (n + 1)), argb, 32)) return false;
        const QSize reduced = capture.size() / factor;
        if (!region.horizontal.create(conn, drawable, reduced, argb, 32)
            || !region.blurred.create(conn, drawable, reduced, argb, 32)) return false;
        const qreal sigma = region.definition.blurRadius / factor;
        if (region.sigma != sigma || region.factor != factor || region.captureRect.size() != capture.size())
        {
            const xcb_render_transform_t down{2 * fixedOne, 0, 0, 0, 2 * fixedOne, 0, 0, 0, fixedOne};
            xcb_render_set_picture_transform(conn, region.opaque.picture.get(), down);
            xcb_render_set_picture_filter(conn, region.opaque.picture.get(), 8, "bilinear", 0, nullptr);
            for (int n = 0; n + 1 < stages; ++n)
            {
                const auto picture = region.reductions[n]->picture.get();
                xcb_render_set_picture_transform(conn, picture, down);
                xcb_render_set_picture_filter(conn, picture, 8, "bilinear", 0, nullptr);
            }
            for (bool horizontal : {true, false})
            {
                const auto values = kernel(sigma, horizontal);
                const auto picture = horizontal ? region.reductions.back()->picture.get() : region.horizontal.picture.get();
                xcb_render_set_picture_transform(conn, picture, identity);
                if (!succeeded(conn, xcb_render_set_picture_filter_checked(conn, picture, 11, "convolution",
                                            values.size(), values.data()))) return false;
            }
            xcb_render_set_picture_filter(conn, region.blurred.picture.get(), 8, "bilinear", 0, nullptr);
            region.sigma = sigma;
            region.factor = factor;
        }
        if (region.visibleRect != visible || !region.mask.picture)
        {
            if (!region.mask.create(conn, drawable, visible.size(), alpha, 8)) return false;
            QImage image(visible.size(), QImage::Format_ARGB32_Premultiplied);
            if (image.isNull()) return false;
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen); painter.setBrush(Qt::white);
            const qreal radius = std::max(qreal(0), std::min({region.definition.cornerRadius,
                region.definition.rect.width() / 2, region.definition.rect.height() / 2}));
            painter.drawRoundedRect(region.definition.rect.translated(-visible.topLeft()), radius, radius);
            painter.end();
            const int stride = (visible.width() + 3) & ~3;
            // Chunk the upload to fit even a server without BIG-REQUESTS.
            const int rows = std::max(1, int(std::min<uint32_t>(65536,
                                        xcb_get_maximum_request_length(conn) * 4 - 64)) / stride);
            const auto gc = xcb_generate_id(conn);
            if (!succeeded(conn, xcb_create_gc_checked(conn, gc, region.mask.pixmap.get(), 0, nullptr))) return false;
            QByteArray pixels(stride * std::min(rows, visible.height()), 0);
            for (int y = 0; y < visible.height(); y += rows)
            {
                const int height = std::min(rows, visible.height() - y);
                for (int n = 0; n < height; ++n)
                {
                    const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y + n));
                    for (int x = 0; x < visible.width(); ++x) pixels[n * stride + x] = qAlpha(line[x]);
                }
                xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, region.mask.pixmap.get(), gc,
                              visible.width(), height, 0, y, 0, 8, stride * height,
                              reinterpret_cast<const uint8_t *>(pixels.constData()));
            }
            xcb_free_gc(conn, gc);
            region.visibleRect = visible;
        }
        region.captureRect = capture;
        return true;
    }
};

X11VideoBackdrop::X11VideoBackdrop(xcb_connection_t *conn, xcb_drawable_t drawable)
    : d(std::make_unique<Private>())
{
    d->conn = conn; d->drawable = drawable;
    auto *formats = xcb_render_query_pict_formats_reply(conn, xcb_render_query_pict_formats(conn), nullptr);
    if (!formats) return;
    auto it = xcb_render_query_pict_formats_formats_iterator(formats);
    for (; it.rem; xcb_render_pictforminfo_next(&it))
    {
        const auto &f = *it.data;
        if (f.type != XCB_RENDER_PICT_TYPE_DIRECT) continue;
        if (f.depth == 8 && f.direct.alpha_mask == 255) d->alpha = f.id;
        if (f.depth == 24 && f.direct.alpha_mask == 0
            && f.direct.red_mask == 255 && f.direct.red_shift == 16
            && f.direct.green_mask == 255 && f.direct.green_shift == 8
            && f.direct.blue_mask == 255 && f.direct.blue_shift == 0) d->rgb = f.id;
        if (f.depth == 32 && f.direct.alpha_mask == 255 && f.direct.alpha_shift == 24
            && f.direct.red_mask == 255 && f.direct.red_shift == 16
            && f.direct.green_mask == 255 && f.direct.green_shift == 8
            && f.direct.blue_mask == 255 && f.direct.blue_shift == 0) d->argb = f.id;
    }
    free(formats);
    auto *filters = xcb_render_query_filters_reply(conn, xcb_render_query_filters(conn, drawable), nullptr);
    if (!filters) return;
    auto filter = xcb_render_query_filters_filters_iterator(filters);
    for (; filter.rem; xcb_str_next(&filter))
        if (QByteArray(xcb_str_name(filter.data), xcb_str_name_length(filter.data)) == "convolution")
            d->supported = d->alpha && d->argb && d->rgb;
    free(filters);
}

X11VideoBackdrop::~X11VideoBackdrop() = default;
bool X11VideoBackdrop::available() const { return d->supported; }

void X11VideoBackdrop::setRegions(const VideoBackdropState &state)
{
    if (state.revision < d->revision) return;
    d->revision = state.revision;
    std::map<quintptr, std::unique_ptr<Region>> next;
    for (const auto &definition : state.regions)
    {
        if (!definition.rect.isValid() || !std::isfinite(definition.blurRadius)
            || definition.blurRadius <= 0 || definition.blurRadius > 512
            || !std::isfinite(definition.cornerRadius)) continue;
        auto &region = d->regions[definition.id];
        if (!region) region = std::make_unique<Region>(d->conn);
        if (region->definition.rect != definition.rect || region->definition.cornerRadius != definition.cornerRadius)
            region->visibleRect = {};
        region->definition = definition;
        next[definition.id] = std::move(region);
    }
    d->regions = std::move(next);
}

void X11VideoBackdrop::render(xcb_render_picture_t video, const QRect &videoRect,
                             xcb_render_picture_t destination, const QSize &windowSize)
{
    if (!d->supported || !video || !destination || videoRect.isEmpty() || windowSize.isEmpty()
        || d->regions.empty()) return;
    const auto conn = d->conn;
    const uint32_t pad = XCB_RENDER_REPEAT_PAD;
    xcb_render_change_picture(conn, video, XCB_RENDER_CP_REPEAT, &pad);
    xcb_render_set_picture_filter(conn, video, 7, "nearest", 0, nullptr);
    for (auto &entry : d->regions)
    {
        auto &r = *entry.second;
        const auto clipped = r.definition.rect.intersected(QRectF(videoRect)).intersected(QRectF(QPoint(), windowSize));
        if (clipped.isEmpty()) continue;
        const auto visible = clipped.toAlignedRect();
        const int factor = reductionFactor(r.definition.blurRadius);
        const int padding = std::ceil(3 * r.definition.blurRadius + 2 * factor);
        const auto local = visible.translated(-videoRect.topLeft());
        // Align all regions to the same sampling grid, including overlapping menus.
        const int left = int(std::floor(qreal(local.left() - padding) / factor)) * factor;
        const int top = int(std::floor(qreal(local.top() - padding) / factor)) * factor;
        const int right = int(std::ceil(qreal(local.x() + local.width() + padding) / factor)) * factor;
        const int bottom = int(std::ceil(qreal(local.y() + local.height() + padding) / factor)) * factor;
        if (std::abs(left) > 32767 || std::abs(top) > 32767 || visible.x() > 32767 || visible.y() > 32767
            || right - left > 65532 || bottom - top > 65532) continue;
        if (!d->prepare(r, visible, QRect(left, top, right - left, bottom - top), factor))
        { d->supported = false; break; }
        // X11 video windows are opaque. Software outputs can leave the XRGB
        // padding byte unset even when the window has an ARGB visual. Discard
        // that byte before filtering, so it cannot turn the blur transparent.
        xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, video, 0, r.opaque.picture.get(),
                             left, top, 0, 0, 0, 0, r.captureRect.width(), r.captureRect.height());
        // Repeated 2:1 reductions retain small details and bound the kernel size at high DPR.
        auto source = r.opaque.picture.get();
        for (const auto &target : r.reductions)
        {
            xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, source, 0, target->picture.get(),
                                 0, 0, 0, 0, 0, 0, target->size.width(), target->size.height());
            source = target->picture.get();
        }
        xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, source, 0, r.horizontal.picture.get(),
                             0, 0, 0, 0, 0, 0, r.blurred.size.width(), r.blurred.size.height());
        xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, r.horizontal.picture.get(), 0, r.blurred.picture.get(),
                             0, 0, 0, 0, 0, 0, r.blurred.size.width(), r.blurred.size.height());
        const auto origin = r.captureRect.topLeft() + videoRect.topLeft();
        const qreal scale = 1. / factor;
        const xcb_render_transform_t up{fixed(scale), 0, fixed(-origin.x() * scale), 0, fixed(scale),
                                        fixed(-origin.y() * scale), 0, 0, fixedOne};
        xcb_render_set_picture_transform(conn, r.blurred.picture.get(), up);
        xcb_render_composite(conn, XCB_RENDER_PICT_OP_OVER, r.blurred.picture.get(), r.mask.picture.get(), destination,
                             visible.x(), visible.y(), 0, 0, visible.x(), visible.y(), visible.width(), visible.height());
    }
    if (!d->supported) d->regions.clear();
    // The caller also uses this picture for the unmodified video on the next frame.
    const uint32_t none = XCB_RENDER_REPEAT_NONE;
    xcb_render_set_picture_transform(conn, video, identity);
    xcb_render_set_picture_filter(conn, video, 7, "nearest", 0, nullptr);
    xcb_render_change_picture(conn, video, XCB_RENDER_CP_REPEAT, &none);
}

}
