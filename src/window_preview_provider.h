#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QHash>
#include <QStringList>
#include <QMutex>

class WindowPreviewProvider : public QQuickImageProvider {
public:
    WindowPreviewProvider();
    ~WindowPreviewProvider() override = default;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    void storeCached(const QString &uuid, const QImage &img);
    QImage getCached(const QString &uuid) const;

private:
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_cache;
    QStringList m_lruOrder;
    static constexpr int MaxCacheEntries = 20;
};
