// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "linuxdesktop.h"

#include "logging.h"

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

LOGGING_CATEGORY(desktop, "desktop")

namespace {
  constexpr auto kwinScreenshotService = "org.kde.KWin.ScreenShot2";
  constexpr auto kwinScreenshotPath = "/org/kde/KWin/ScreenShot2";
  constexpr auto kwinScreenshotInterface = "org.kde.KWin.ScreenShot2";

  // -----------------------------------------------------------------------------------------------
  QPixmap grabScreenKWin(QScreen* screen)
  {
    int pipeDescriptors[2] = {-1, -1};
    if (::pipe2(pipeDescriptors, O_CLOEXEC) != 0) {
      logError(desktop) << LinuxDesktop::tr("Could not create a pipe for the KWin screenshot.");
      return {};
    }

    QFile readPipe;
    if (!readPipe.open(pipeDescriptors[0], QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
      ::close(pipeDescriptors[0]);
      ::close(pipeDescriptors[1]);
      logError(desktop) << LinuxDesktop::tr("Could not open the KWin screenshot pipe.");
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
      auto message = LinuxDesktop::tr("Screenshot via KWin ScreenShot2 failed: %1")
                       .arg(reply.error().message());
      if (reply.error().name() == QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized")) {
        message += LinuxDesktop::tr(
          " Install Projecteur so KWin can associate the executable with its desktop metadata.");
      }
      logError(desktop) << message;
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
      logError(desktop) << LinuxDesktop::tr("KWin returned invalid screenshot dimensions.");
      return {};
    }

    const QByteArray pixels = readPipe.readAll();
    if (quint64(pixels.size()) < expectedBytes) {
      logError(desktop) << LinuxDesktop::tr("KWin returned an incomplete screenshot.");
      return {};
    }

    const auto format = static_cast<QImage::Format>(formatValue);
    const QImage image(reinterpret_cast<const uchar*>(pixels.constData()),
                       int(width), int(height), int(stride), format);
    if (image.isNull()) {
      logError(desktop) << LinuxDesktop::tr("KWin returned an unsupported screenshot format.");
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
}

QPixmap LinuxDesktop::grabScreen(QScreen* screen) const
{
  if (!screen) {
    return {};
  }
  if (!isWayland()) {
    logWarning(desktop) << tr("Screen capture is only supported on Wayland.");
    return {};
  }
  if (type() != LinuxDesktop::Type::KDE) {
    logWarning(desktop) << tr("Screen capture is only supported on KDE Plasma.");
    return {};
  }
  return grabScreenKWin(screen);
}
