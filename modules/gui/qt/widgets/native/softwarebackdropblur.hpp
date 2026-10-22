// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_SOFTWARE_BACKDROP_BLUR_HPP
#define VLC_QT_SOFTWARE_BACKDROP_BLUR_HPP

#include <QQuickItem>
#include <memory>

struct SoftwareBackdropState;

// Blur the Qt foreground already painted below this item. Native video remains
// in its separate surface; its transparent foreground pixels stay transparent.
class SoftwareBackdropBlur : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged FINAL)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged FINAL)
public:
    explicit SoftwareBackdropBlur(QQuickItem *parent = nullptr);
    qreal radius() const { return m_radius; }
    qreal cornerRadius() const { return m_cornerRadius; }
    void setRadius(qreal value);
    void setCornerRadius(qreal value);
signals:
    void radiusChanged();
    void cornerRadiusChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
private:
    qreal m_radius = 25;
    qreal m_cornerRadius = 10;
    std::shared_ptr<SoftwareBackdropState> m_state;
    QMetaObject::Connection m_syncConnection;
};
#endif
