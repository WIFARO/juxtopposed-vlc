// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QUrl>
#include "util/covergenerator.hpp"

class TestCoverGenerator : public QObject
{
    Q_OBJECT
private slots:
    void transparentFallbackHasNoWhiteOrAlbumPlaceholder() {
        CoverGenerator generator;
        generator.setSize({91,53});
        generator.setDefaultThumbnail({});
        for (const auto split : {CoverGenerator::Divide, CoverGenerator::Duplicate}) {
            generator.setSplit(split);
            for (const QStringList& files : {QStringList{},QStringList{QStringLiteral("/missing/cover.png")}}) {
                const auto image = generator.execute(files);
                QCOMPARE(image.size(), QSize(91,53));
                QVERIFY(image.hasAlphaChannel());
                for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
                    QCOMPARE(image.pixelColor(x,y).alpha(),0);
            }
        }
    }
    void optionalEmptyMarkerDoesNotChangeRealImageDimensions() {
        QTemporaryDir directory;
        QImage pixel(1,1,QImage::Format_ARGB32_Premultiplied); pixel.fill(Qt::red);
        const auto path=directory.filePath("pixel.png"); QVERIFY(pixel.save(path));
        CoverGenerator generator; generator.setSize({91,53});
        generator.setDefaultThumbnail({}); generator.setEmptyFallbackSize({1,1});
        for (const QStringList& files : {QStringList{},QStringList{QStringLiteral("/missing/cover.png")}}) {
            const auto image=generator.execute(files);
            QCOMPARE(image.size(),QSize(1,1));
            QCOMPARE(image.pixelColor(0,0).alpha(),0);
        }
        const auto real=generator.execute({path});
        QCOMPARE(real.size(),QSize(91,53));
        QCOMPARE(real.pixelColor(45,26),QColor(Qt::red));
    }
    void missingAndInvalidImagesDoNotLeaveGapsBesideRealArtwork() {
        QTemporaryDir directory;
        QImage source(30,30,QImage::Format_RGB32); source.fill(QColor("#428b69"));
        const auto cover = directory.filePath("épisode & cover.png");
        QVERIFY(source.save(cover));
        QFile broken(directory.filePath("broken.png"));
        QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("invalid image"); broken.close();
        CoverGenerator generator;
        generator.setSize({91,53}); generator.setDefaultThumbnail({});
        const auto image=generator.execute({"/missing/cover.png",broken.fileName(),QUrl::fromLocalFile(cover).toString()});
        for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
            QCOMPARE(image.pixelColor(x,y),QColor("#428b69"));
    }
    void singleDefaultUsesOneImageAndIgnoresUnreadableCovers() {
        QTemporaryDir directory;
        QImage pattern(100,60,QImage::Format_RGB32);
        pattern.fill(Qt::red);
        QPainter painter(&pattern); painter.fillRect(50,0,50,60,Qt::blue); painter.end();
        const auto path=directory.filePath("fallback.png"); QVERIFY(pattern.save(path));
        CoverGenerator generator; generator.setSize({200,100});
        generator.setCountX(4); generator.setCountY(2); generator.setSplit(CoverGenerator::Duplicate);
        generator.setDefaultThumbnail(path);
        QCOMPARE(generator.execute({}).pixelColor(37,30),QColor(Qt::blue));
        generator.setSingleDefault(true);
        for (const auto& files : {QStringList{},QStringList{QStringLiteral("/missing/cover.png")}}) {
            const auto image=generator.execute(files);
            QCOMPARE(image.pixelColor(37,30),QColor(Qt::red));
            QCOMPARE(image.pixelColor(150,30),QColor(Qt::blue));
        }
    }
    void singleDefaultPreservesTransparencyInRealArtwork() {
        QTemporaryDir directory;
        QImage art(20,20,QImage::Format_ARGB32_Premultiplied); art.fill(Qt::transparent);
        QPainter painter(&art); painter.fillRect(0,0,10,20,Qt::red); painter.end();
        const auto path=directory.filePath("real.png"); QVERIFY(art.save(path));
        QImage fallback(20,20,QImage::Format_RGB32); fallback.fill(Qt::blue);
        const auto fallbackPath=directory.filePath("fallback.png"); QVERIFY(fallback.save(fallbackPath));
        CoverGenerator generator; generator.setSize({100,60});
        generator.setDefaultThumbnail(fallbackPath); generator.setSingleDefault(true);
        const auto image=generator.execute({path});
        QCOMPARE(image.pixelColor(20,30),QColor(Qt::red));
        QCOMPARE(image.pixelColor(80,30).alpha(),0);
    }
    void explicitDefaultAndCollageRemainAvailable() {
        QTemporaryDir directory;
        QImage source(20,20,QImage::Format_RGB32);
        const auto red=directory.filePath("red.png"), blue=directory.filePath("blue.png");
        source.fill(Qt::red); QVERIFY(source.save(red));
        source.fill(Qt::blue); QVERIFY(source.save(blue));
        CoverGenerator generator; generator.setSize({100,60}); generator.setDefaultThumbnail(blue);
        QCOMPARE(generator.execute({}).pixelColor(50,30),QColor(Qt::blue));
        generator.setDefaultThumbnail({});
        const auto collage=generator.execute({red,blue});
        QCOMPARE(collage.pixelColor(10,30),QColor(Qt::red));
        QCOMPARE(collage.pixelColor(90,30),QColor(Qt::blue));
    }
};
QTEST_GUILESS_MAIN(TestCoverGenerator)
#include "test_cover_generator.moc"
