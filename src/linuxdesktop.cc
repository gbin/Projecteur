// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "linuxdesktop.h"

#include "kwinscreencast.h"
#include "projecteur_desktop_debug.h"

#include <KLocalizedString>

#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QProcessEnvironment>
#include <QScreen>

#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace {
  constexpr auto kwinScreenshotService = "org.kde.KWin.ScreenShot2";
  constexpr auto kwinScreenshotPath = "/org/kde/KWin/ScreenShot2";
  constexpr auto kwinScreenshotInterface = "org.kde.KWin.ScreenShot2";
  constexpr auto kwinService = "org.kde.KWin";
  constexpr auto kwinEffectsPath = "/Effects";
  constexpr auto kwinEffectsInterface = "org.kde.kwin.Effects";
  constexpr auto shakeCursorEffect = "shakecursor";

  // -----------------------------------------------------------------------------------------------
  QPixmap grabScreenKWin(QScreen* screen)
  {
    int pipeDescriptors[2] = {-1, -1};
    if (::pipe2(pipeDescriptors, O_CLOEXEC) != 0) {
      qCCritical(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Could not create a pipe for the KWin screenshot.");
      return {};
    }

    QFile readPipe;
    if (!readPipe.open(pipeDescriptors[0], QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
      ::close(pipeDescriptors[0]);
      ::close(pipeDescriptors[1]);
      qCCritical(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Could not open the KWin screenshot pipe.");
      return {};
    }

    QDBusReply<QVariantMap> reply;
    {
      QDBusUnixFileDescriptor writePipe;
      writePipe.giveFileDescriptor(pipeDescriptors[1]);
      QDBusInterface interface(kwinScreenshotService, kwinScreenshotPath,
                               kwinScreenshotInterface);

      QVariantMap options;
      options.insert(QStringLiteral("hide-caller-windows"), true);
      options.insert(QStringLiteral("native-resolution"), true);

      reply = interface.call(QStringLiteral("CaptureScreen"), screen->name(), options,
                             QVariant::fromValue(writePipe));
    }

    if (!reply.isValid()) {
      auto message = i18n("Screenshot via KWin ScreenShot2 failed: %1",
                          reply.error().message());
      if (reply.error().name() == QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized")) {
        message += i18n(
          " Install Projecteur so KWin can associate the executable with its desktop metadata.");
      }
      qCCritical(PROJECTEUR_DESKTOP_LOG).noquote() << message;
      return {};
    }

    const QVariantMap attributes = reply.value();
    const quint32 width = attributes.value(QStringLiteral("width")).toUInt();
    const quint32 height = attributes.value(QStringLiteral("height")).toUInt();
    const quint32 stride = attributes.value(QStringLiteral("stride")).toUInt();
    const quint32 formatValue = attributes.value(QStringLiteral("format")).toUInt();
    const qreal scale = attributes.value(QStringLiteral("scale"), 1.0).toDouble();

    const quint64 expectedBytes = quint64(stride) * height;
    if (width == 0 || height == 0 || stride == 0
        || expectedBytes > quint64(std::numeric_limits<qsizetype>::max())) {
      qCCritical(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("KWin returned invalid screenshot dimensions.");
      return {};
    }

    const QByteArray pixels = readPipe.readAll();
    if (quint64(pixels.size()) < expectedBytes) {
      qCCritical(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("KWin returned an incomplete screenshot.");
      return {};
    }

    const auto format = static_cast<QImage::Format>(formatValue);
    const QImage image(reinterpret_cast<const uchar*>(pixels.constData()),
                       int(width), int(height), int(stride), format);
    if (image.isNull()) {
      qCCritical(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("KWin returned an unsupported screenshot format.");
      return {};
    }

    QPixmap pixmap = QPixmap::fromImage(image.copy());
    pixmap.setDevicePixelRatio(scale > 0 ? scale : 1.0);
    return pixmap;
  }
} // end anonymous namespace

LinuxDesktop::LinuxDesktop(QObject* parent)
  : QObject(parent)
{
  const auto env = QProcessEnvironment::systemEnvironment();
  const auto kdeFullSession = env.value(QStringLiteral("KDE_FULL_SESSION"));
  const auto desktopSession = env.value(QStringLiteral("DESKTOP_SESSION"));
  const auto xdgCurrentDesktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));

  if (!kdeFullSession.isEmpty()
      || desktopSession.contains(QStringLiteral("plasma"), Qt::CaseInsensitive)
      || xdgCurrentDesktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive)) {
    m_type = LinuxDesktop::Type::KDE;
  }

  m_wayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"),
                                                         Qt::CaseInsensitive);
  if (m_wayland && m_type == LinuxDesktop::Type::KDE) {
    m_screencast = new KWinScreencast(this);
  }
}

LinuxDesktop::~LinuxDesktop()
{
  setShakeCursorEffectSuppressed(false);
}

QPixmap LinuxDesktop::grabScreen(QScreen* screen) const
{
  if (!screen) {
    return {};
  }
  if (!isWayland()) {
    qCWarning(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Screen capture is only supported on Wayland.");
    return {};
  }
  if (type() != LinuxDesktop::Type::KDE) {
    qCWarning(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Screen capture is only supported on KDE Plasma.");
    return {};
  }
  return grabScreenKWin(screen);
}

QObject* LinuxDesktop::streamScreen(QScreen* screen, QObject* parent)
{
  return m_screencast ? m_screencast->streamScreen(screen, parent) : nullptr;
}

void LinuxDesktop::setShakeCursorEffectSuppressed(bool suppressed)
{
  if (!isWayland() || type() != LinuxDesktop::Type::KDE
      || suppressed == m_shakeCursorEffectSuppressed) {
    return;
  }

  QDBusInterface interface(kwinService, kwinEffectsPath, kwinEffectsInterface);
  if (!interface.isValid()) {
    qCWarning(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Could not access KWin's desktop effects interface.");
    return;
  }

  if (suppressed)
  {
    const QDBusReply<bool> loadedReply =
      interface.call(QStringLiteral("isEffectLoaded"), QString::fromLatin1(shakeCursorEffect));
    if (!loadedReply.isValid()) {
      qCWarning(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Could not query KWin's Shake Cursor effect: %1").arg(loadedReply.error().message());
      return;
    }
    if (!loadedReply.value()) {
      return;
    }

    const QDBusReply<void> unloadReply =
      interface.call(QStringLiteral("unloadEffect"), QString::fromLatin1(shakeCursorEffect));
    if (!unloadReply.isValid()) {
      qCWarning(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Could not suppress KWin's Shake Cursor effect: %1").arg(unloadReply.error().message());
      return;
    }

    m_shakeCursorEffectSuppressed = true;
    return;
  }

  const QDBusReply<bool> loadReply =
    interface.call(QStringLiteral("loadEffect"), QString::fromLatin1(shakeCursorEffect));
  if (!loadReply.isValid() || !loadReply.value()) {
    const auto error = loadReply.isValid()
                         ? i18n("KWin refused to load the effect.")
                         : loadReply.error().message();
    qCWarning(PROJECTEUR_DESKTOP_LOG).noquote() << QStringLiteral("Could not restore KWin's Shake Cursor effect: %1").arg(error);
    return;
  }

  m_shakeCursorEffectSuppressed = false;
}
