// SPDX-License-Identifier: GPL-2.0-or-later
#include "softwarebackdropblur.hpp"
#include "util/fast_gaussian_blur_template.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGRenderNode>
#include <cstdlib>
#include <cstring>

struct SoftwareBackdropState
{
    // Accessed only by the scene graph thread, including beforeSynchronizing.
    QSGNode *node = nullptr;
};

class SoftwareBackdropNode : public QSGRenderNode
{
public:
    SoftwareBackdropNode(QQuickWindow *window, std::shared_ptr<SoftwareBackdropState> state)
        : m_window(window), m_state(std::move(state)) { m_state->node = this; }
    ~SoftwareBackdropNode() override { m_state->node = nullptr; }

    void synchronize(QSizeF size, qreal radius, qreal cornerRadius)
    {
        m_size = size;
        m_radius = radius;
        m_cornerRadius = cornerRadius;
        markDirty(DirtyGeometry);
    }
    QRectF rect() const override
    {
        const qreal padding = std::ceil(3 * m_radius);
        // Repaint the dependency area below us on each requested frame. Without
        // this padding, partial updates could feed our previous blurred pixels
        // back into the next frame.
        return QRectF(QPointF(), m_size).adjusted(-padding, -padding, padding, padding);
    }
    RenderingFlags flags() const override { return BoundedRectRendering | DepthAwareRendering; }
    void render(const RenderState *state) override
    {
        auto *painter = static_cast<QPainter *>(m_window->rendererInterface()->getResource(
            m_window, QSGRendererInterface::PainterResource));
        if (!painter || painter->device()->devType() != QInternal::Image || m_size.isEmpty())
            return;
        auto *device = static_cast<QImage *>(painter->device());
        painter->save();
        if (const auto *clip = state->clipRegion(); clip && !clip->isEmpty())
            painter->setClipRegion(*clip, Qt::ReplaceClip);
        painter->setTransform(matrix()->toTransform());
        const QTransform physical = painter->deviceTransform();
        const qreal scale = std::hypot(physical.m11(), physical.m12());
        const qreal sigma = m_radius * scale;
        const QRect capture = physical.mapRect(rect()).toAlignedRect().intersected(device->rect());
        if (capture.isEmpty() || !physical.isInvertible() || sigma <= 0) {
            painter->restore();
            return;
        }
        QImage image = device->copy(capture).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const auto bytes = size_t(image.sizeInBytes());
        auto *input = static_cast<unsigned char *>(std::malloc(bytes));
        auto *output = static_cast<unsigned char *>(std::malloc(bytes));
        if (!input || !output) {
            std::free(input); std::free(output); painter->restore(); return;
        }
        std::memcpy(input, image.constBits(), bytes);
        fast_gaussian_blur(input, output, image.width(), image.height(), 4,
                           float(sigma), 3, Border::kExtend);
        std::free(input);
        image = QImage(output, image.width(), image.height(), image.width() * 4,
                       QImage::Format_ARGB32_Premultiplied, std::free, output);
        // Paint in device pixels so capture alignment also holds at fractional
        // scale. Source composition replaces the sharp foreground. QPainter's
        // antialiased path coverage preserves the original at rounded edges.
        painter->resetTransform();
        painter->setTransform(painter->deviceTransform().inverted());
        painter->setCompositionMode(QPainter::CompositionMode_Source);
        painter->setOpacity(inheritedOpacity());
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const qreal corner = qBound(qreal(0), m_cornerRadius, qMin(m_size.width(), m_size.height()) / 2);
        path.addRoundedRect(QRectF(QPointF(), m_size), corner, corner);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(image));
        painter->setBrushOrigin(capture.topLeft());
        painter->drawPath(physical.map(path));
        painter->restore();
    }
private:
    QQuickWindow *m_window;
    std::shared_ptr<SoftwareBackdropState> m_state;
    QSizeF m_size;
    qreal m_radius = 25;
    qreal m_cornerRadius = 10;
};

SoftwareBackdropBlur::SoftwareBackdropBlur(QQuickItem *parent)
    : QQuickItem(parent), m_state(std::make_shared<SoftwareBackdropState>())
{
    setFlag(ItemHasContents);
    const auto attachWindow = [this](QQuickWindow *window) {
        disconnect(m_syncConnection);
        m_state = std::make_shared<SoftwareBackdropState>();
        if (!window) return;
        m_syncConnection = connect(window, &QQuickWindow::beforeSynchronizing, this,
            [state = m_state] {
                // This observes frames already requested by other scene changes.
                // It must never request a new frame or keep an idle window busy.
                if (state->node) state->node->markDirty(QSGNode::DirtyForceUpdate);
            }, Qt::DirectConnection);
    };
    connect(this, &QQuickItem::windowChanged, this, attachWindow);
    attachWindow(window());
}
void SoftwareBackdropBlur::setRadius(qreal value)
{
    if (!std::isfinite(value)) return;
    value = qMax(qreal(0), value);
    if (m_radius == value) return;
    m_radius = value; emit radiusChanged(); update();
}
void SoftwareBackdropBlur::setCornerRadius(qreal value)
{
    if (!std::isfinite(value)) return;
    value = qMax(qreal(0), value);
    if (m_cornerRadius == value) return;
    m_cornerRadius = value; emit cornerRadiusChanged(); update();
}
QSGNode *SoftwareBackdropBlur::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (!window() || window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software
        || m_radius <= 0 || size().isEmpty()) {
        delete old; return nullptr;
    }
    auto *node = static_cast<SoftwareBackdropNode *>(old);
    if (!node) node = new SoftwareBackdropNode(window(), m_state);
    node->synchronize(size(), m_radius, m_cornerRadius);
    return node;
}
