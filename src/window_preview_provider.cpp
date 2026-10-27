#include "window_preview_provider.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QUrl>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

WindowPreviewProvider::WindowPreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage WindowPreviewProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(requestedSize);
    QString cleanUuid = QUrl::fromPercentEncoding(id.toUtf8()).section(QLatin1Char('?'), 0, 0).trimmed();

    if (cleanUuid.isEmpty()) {
        return QImage();
    }

    int pipeFds[2] = {-1, -1};
    if (::pipe2(pipeFds, O_CLOEXEC) != 0) {
        return getCached(cleanUuid);
    }

    int readFd = pipeFds[0];
    int writeFd = pipeFds[1];

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin.ScreenShot2"),
        QStringLiteral("/org/kde/KWin/ScreenShot2"),
        QStringLiteral("org.kde.KWin.ScreenShot2"),
        QStringLiteral("CaptureWindow")
    );

    QVariantMap options;
    options.insert(QStringLiteral("include-decoration"), true);
    options.insert(QStringLiteral("native-resolution"), false);

    msg << cleanUuid
        << options
        << QVariant::fromValue(QDBusUnixFileDescriptor(writeFd));

    // Synchronous call on this background image thread with a 600ms timeout
    QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 600);
    ::close(writeFd); // Close write end so readFd encounters EOF when KWin finishes writing

    if (!reply.isValid()) {
        qWarning() << "[WindowPreview] CaptureWindow DBus error for" << cleanUuid << ":" << reply.error().name() << reply.error().message();
        ::close(readFd);
        return getCached(cleanUuid);
    }

    const QVariantMap results = reply.value();
    const int width = results.value(QStringLiteral("width")).toInt();
    const int height = results.value(QStringLiteral("height")).toInt();
    const int stride = results.value(QStringLiteral("stride")).toInt();
    const int format = results.value(QStringLiteral("format")).toInt();

    if (width <= 0 || height <= 0 || stride < width ||
        format <= QImage::Format_Invalid || format >= QImage::NImageFormats)
    {
        ::close(readFd);
        return getCached(cleanUuid);
    }

    const qint64 expectedBytes = static_cast<qint64>(height) * stride;
    QByteArray rawBytes;
    rawBytes.reserve(expectedBytes);

    char chunk[65536];
    while (rawBytes.size() < expectedBytes) {
        ssize_t n = ::read(readFd, chunk, sizeof(chunk));
        if (n > 0) {
            rawBytes.append(chunk, static_cast<qsizetype>(n));
        } else if (n == 0) {
            break; // EOF
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    ::close(readFd);

    if (rawBytes.size() < expectedBytes) {
        qWarning() << "[WindowPreview] Incomplete read:" << rawBytes.size() << "expected:" << expectedBytes;
        return getCached(cleanUuid);
    }

    QImage rawImg(
        reinterpret_cast<const uchar*>(rawBytes.constData()),
        width,
        height,
        stride,
        static_cast<QImage::Format>(format)
    );

    // Downscale to a crisp Retina thumbnail (~720x450) and free the large raw buffer immediately
    QImage thumbnail = rawImg.scaled(QSize(720, 450), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (thumbnail.isNull()) {
        return getCached(cleanUuid);
    }

    storeCached(cleanUuid, thumbnail);

    if (size) {
        *size = thumbnail.size();
    }
    qInfo() << "[WindowPreview] Successfully captured window preview for" << cleanUuid << "size:" << thumbnail.size();
    return thumbnail;
}

void WindowPreviewProvider::storeCached(const QString &uuid, const QImage &img) {
    if (uuid.isEmpty() || img.isNull()) return;
    QMutexLocker locker(&m_mutex);
    m_lruOrder.removeAll(uuid);
    m_lruOrder.append(uuid);
    m_cache.insert(uuid, img);
    while (m_lruOrder.size() > MaxCacheEntries) {
        const QString oldest = m_lruOrder.takeFirst();
        m_cache.remove(oldest);
    }
}

QImage WindowPreviewProvider::getCached(const QString &uuid) const {
    QMutexLocker locker(&m_mutex);
    return m_cache.value(uuid);
}

void WindowPreviewProvider::pruneTo(const QSet<QString> &validUuids) {
    QMutexLocker locker(&m_mutex);
    for (int i = m_lruOrder.size() - 1; i >= 0; --i) {
        const QString &uuid = m_lruOrder.at(i);
        if (!validUuids.contains(uuid)) {
            m_cache.remove(uuid);
            m_lruOrder.removeAt(i);
        }
    }
}

void WindowPreviewProvider::clear() {
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_lruOrder.clear();
}
