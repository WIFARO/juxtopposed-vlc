// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QImage>
#include <cmath>
#include <cstdlib>
#include "maininterface/compositor_x11_backdrop.hpp"
#include "maininterface/compositor_x11_utils.hpp"

class X11VideoBackdropTest : public QObject
{
    Q_OBJECT
    xcb_connection_t *conn = nullptr;
    xcb_window_t root = 0;
    xcb_render_pictformat_t argb = 0;
    struct Image
    {
        vlc::PixmapPtr pixmap;
        vlc::PicturePtr picture;
        QSize size;
        Image(xcb_connection_t *c) : pixmap(c), picture(c) {}
    };
    void create(Image &image, const QSize &size)
    {
        image.size = size;
        image.pixmap.generateId();
        QVERIFY(check(xcb_create_pixmap_checked(conn, 32, image.pixmap.get(), root, size.width(), size.height())));
        image.picture.generateId();
        QVERIFY(check(xcb_render_create_picture_checked(conn, image.picture.get(), image.pixmap.get(), argb, 0, nullptr)));
    }
    bool check(xcb_void_cookie_t request)
    {
        auto *error = xcb_request_check(conn, request);
        if (!error) return true;
        qWarning() << "X11 request failed" << error->error_code << error->major_code << error->minor_code;
        free(error); return false;
    }
    void upload(Image &target, const QImage &image)
    {
        const auto gc = xcb_generate_id(conn);
        QVERIFY(check(xcb_create_gc_checked(conn, gc, target.pixmap.get(), 0, nullptr)));
        for (int y = 0; y < image.height(); y += 16)
        {
            const int height = std::min(16, image.height() - y);
            QVERIFY(check(xcb_put_image_checked(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, target.pixmap.get(), gc,
                image.width(), height, 0, y, 0, 32, height * image.bytesPerLine(), image.constScanLine(y))));
        }
        xcb_free_gc(conn, gc);
    }
    QImage read(const Image &image)
    {
        auto *reply = xcb_get_image_reply(conn, xcb_get_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP,
                    image.pixmap.get(), 0, 0, image.size.width(), image.size.height(), ~0u), nullptr);
        if (!reply) return {};
        QImage result(xcb_get_image_data(reply), image.size.width(), image.size.height(), QImage::Format_ARGB32);
        result = result.copy(); free(reply); return result;
    }
    void draw(const Image &video, const QRect &rect, const Image &output)
    {
        const xcb_render_color_t background{0x2000, 0x4000, 0x6000, 0xffff};
        const xcb_rectangle_t whole{0, 0, uint16_t(output.size.width()), uint16_t(output.size.height())};
        xcb_render_fill_rectangles(conn, XCB_RENDER_PICT_OP_SRC, output.picture.get(), background, 1, &whole);
        xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, video.picture.get(), 0, output.picture.get(),
                             0, 0, 0, 0, rect.x(), rect.y(), rect.width(), rect.height());
    }
    QImage checker(const QSize &size, int period = 8)
    {
        QImage result(size, QImage::Format_ARGB32);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
                result.setPixel(x, y, (x / period + y / period) % 2 ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
        return result;
    }
    void noErrors()
    {
        // A round trip makes all asynchronous Render errors observable.
        auto *reply = xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), nullptr); free(reply);
        while (auto *event = xcb_poll_for_event(conn))
        {
            const bool error = event->response_type == 0;
            free(event); QVERIFY2(!error, "Unexpected asynchronous XRender error");
        }
    }

private slots:
    void initTestCase()
    {
        int screen = 0;
        conn = xcb_connect(nullptr, &screen);
        if (xcb_connection_has_error(conn)) QSKIP("An X11 server is required for native Render pixels");
        auto screens = xcb_setup_roots_iterator(xcb_get_setup(conn));
        for (int i = 0; i < screen; ++i) xcb_screen_next(&screens);
        root = screens.data->root;
        auto *formats = xcb_render_query_pict_formats_reply(conn, xcb_render_query_pict_formats(conn), nullptr);
        QVERIFY(formats);
        auto it = xcb_render_query_pict_formats_formats_iterator(formats);
        for (; it.rem; xcb_render_pictforminfo_next(&it))
            if (it.data->depth == 32 && it.data->direct.alpha_mask == 255 && it.data->direct.alpha_shift == 24
                && it.data->direct.red_shift == 16 && it.data->direct.green_shift == 8 && it.data->direct.blue_shift == 0)
                argb = it.data->id;
        free(formats); QVERIFY(argb);
        vlc::X11VideoBackdrop effect(conn, root);
        if (!effect.available()) QSKIP("The X11 server does not provide convolution filtering");
    }
    void cleanupTestCase() { if (conn) xcb_disconnect(conn); conn = nullptr; }

    void gaussianRadius_data()
    {
        QTest::addColumn<qreal>("radius");
        QTest::newRow("DPR1") << 25.;
        QTest::newRow("DPR1.5") << 37.5;
        QTest::newRow("DPR2") << 50.;
    }
    void gaussianRadius()
    {
        QFETCH(qreal, radius);
        const QSize size(512, 384);
        QImage step(size, QImage::Format_ARGB32); step.fill(Qt::black);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 256; x < size.width(); ++x) step.setPixel(x, y, qRgb(255, 255, 255));
        Image video(conn), output(conn); create(video, size); create(output, size); upload(video, step);
        draw(video, QRect(QPoint(), size), output);
        vlc::X11VideoBackdrop effect(conn, root);
        effect.setRegions({1, {{1, QRectF(32, 16, 448, 352), radius, 14}}});
        effect.render(video.picture.get(), QRect(QPoint(), size), output.picture.get(), size);
        const auto image = read(output); QVERIFY(!image.isNull());
        double mass = 0, mean = 0, second = 0;
        for (int x = 64; x < 448; ++x)
        {
            const int weight = qRed(image.pixel(x, 192)) - qRed(image.pixel(x - 1, 192));
            QVERIFY(weight >= -1);
            mass += weight; mean += x * weight; second += x * x * weight;
        }
        QVERIFY(mass > 250);
        mean /= mass;
        const double sigma = std::sqrt(second / mass - mean * mean);
        qInfo() << "measured Gaussian sigma" << sigma << "requested" << radius;
        QVERIFY(std::abs(sigma - radius) < radius * .12);
        QVERIFY(std::abs(mean - 256) < 2);
        // Restoring the source picture's transform/filter must preserve the next frame.
        draw(video, QRect(QPoint(), size), output);
        QCOMPARE(read(output), step);
        noErrors();
    }

    void roundedAndClippedRegions_data()
    {
        QTest::addColumn<QRectF>("region"); QTest::addColumn<QPoint>("position");
        QTest::newRow("normal") << QRectF(64, 32, 384, 320) << QPoint();
        QTest::newRow("fractional") << QRectF(64.25, 32.5, 383.5, 319.25) << QPoint();
        QTest::newRow("video-offset") << QRectF(64, 32, 384, 320) << QPoint(37, 29);
        QTest::newRow("offscreen") << QRectF(-20, -10, 240, 180) << QPoint();
        QTest::newRow("tiny") << QRectF(120.5, 120.25, 2, 3) << QPoint();
    }
    void roundedAndClippedRegions()
    {
        QFETCH(QRectF, region); QFETCH(QPoint, position);
        const QSize size(512, 384), window(600, 450);
        Image video(conn), output(conn); create(video, size); create(output, window); upload(video, checker(size));
        const QRect videoRect(position, size);
        draw(video, videoRect, output); const auto original = read(output);
        vlc::X11VideoBackdrop effect(conn, root);
        effect.setRegions({1, {{1, region, 25, 14}}});
        effect.render(video.picture.get(), videoRect, output.picture.get(), window);
        const auto image = read(output);
        int changed = 0;
        for (int y = 0; y < window.height(); ++y)
            for (int x = 0; x < window.width(); ++x)
            {
                const auto point = QPointF(x + .5, y + .5);
                // Fractional boundaries can cover part of an adjacent pixel.
                if (!region.adjusted(-1, -1, 1, 1).contains(point) || !videoRect.contains(QPoint(x, y)))
                    QCOMPARE(image.pixel(x, y), original.pixel(x, y));
                changed += image.pixel(x, y) != original.pixel(x, y);
            }
        QVERIFY(changed > 0);
        const auto corner = region.topLeft().toPoint() + QPoint(1, 1);
        if (region.width() > 28 && region.height() > 28 && videoRect.contains(corner))
            QCOMPARE(image.pixel(corner), original.pixel(corner));
        noErrors();
    }

    void overlapMovementAndRemoval()
    {
        const QSize size(512, 384); const QRect videoRect(QPoint(), size);
        Image video(conn), output(conn); create(video, size); create(output, size); upload(video, checker(size, 1));
        vlc::X11VideoBackdrop effect(conn, root);
        const VideoBackdropRegion a{1, QRectF(64, 32, 320, 300), 25, 14};
        effect.setRegions({1, {a}}); draw(video, videoRect, output);
        effect.render(video.picture.get(), videoRect, output.picture.get(), size); const auto first = read(output);
        QVERIFY(qRed(first.pixel(200, 200)) >= 126 && qRed(first.pixel(200, 200)) <= 129);
        const VideoBackdropRegion b{2, QRectF(128, 96, 320, 240), 25, 14};
        effect.setRegions({2, {a, b}}); draw(video, videoRect, output);
        effect.render(video.picture.get(), videoRect, output.picture.get(), size);
        QCOMPARE(read(output).pixel(200, 200), first.pixel(200, 200));
        auto moved = a; moved.rect.translate(20, 15); moved.cornerRadius = 30; moved.blurRadius = 50;
        effect.setRegions({3, {moved}}); draw(video, videoRect, output);
        effect.render(video.picture.get(), videoRect, output.picture.get(), size);
        QCOMPARE(read(output).pixel(65, 40), checker(size, 1).pixel(65, 40));
        effect.setRegions({4, {}});
        effect.setRegions({3, {a}}); // A delayed old snapshot must not restore removed regions.
        draw(video, videoRect, output);
        effect.render(video.picture.get(), videoRect, output.picture.get(), size);
        QCOMPARE(read(output), checker(size, 1));
        noErrors();
    }

    void opaqueNativeVideo_data()
    {
        QTest::addColumn<int>("paddingByte");
        QTest::newRow("unused-zero") << 0;
        QTest::newRow("unused-nonzero") << 17;
        QTest::newRow("opaque") << 255;
    }
    void opaqueNativeVideo()
    {
        QFETCH(int, paddingByte);
        const QSize size(512, 384);
        auto pixels = checker(size, 1);
        // An opaque X11 video window can use XRGB storage in an ARGB visual.
        // The video output need not initialize the unused high byte.
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
                pixels.setPixel(x, y, (pixels.pixel(x, y) & 0xffffff) | (uint32_t(paddingByte) << 24));
        Image video(conn), output(conn); create(video, size); create(output, size); upload(video, pixels);
        draw(video, QRect(QPoint(), size), output);
        vlc::X11VideoBackdrop effect(conn, root);
        effect.setRegions({1, {{1, QRectF(64, 32, 384, 320), 25, 14}}});
        effect.render(video.picture.get(), QRect(QPoint(), size), output.picture.get(), size);
        const auto color = read(output).pixel(200, 200);
        QVERIFY(qRed(color) >= 126 && qRed(color) <= 129);
        QCOMPARE(qAlpha(color), 255);
        noErrors();
    }
};

QTEST_GUILESS_MAIN(X11VideoBackdropTest)
#include "test_x11_video_backdrop.moc"
