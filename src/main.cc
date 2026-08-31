// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapp.h"
#include "projecteur-GitVersion.h"

#include "settings.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedString>

#include <QCommandLineParser>

#ifndef NDEBUG
#include <QQmlDebuggingEnabler>
#endif

#include <csignal>
#include <iomanip>
#include <iostream>

#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(x) #x

namespace {
  // -----------------------------------------------------------------------------------------------
  constexpr int PROJECTEUR_ERROR_NO_INSTANCE_FOUND = 43;
  constexpr int PROJECTEUR_ERROR_EMPTY_COMMAND_PROPS = 44;

  // -----------------------------------------------------------------------------------------------
  KAboutData projecteurAboutData()
  {
    KAboutData aboutData(
      QStringLiteral("Projecteur"),
      QStringLiteral("Projecteur"),
      QString::fromUtf8(projecteur::version_string()),
      i18n("A KDE Plasma spotlight for Logitech presenter devices."),
      KAboutLicense::MIT,
      i18n("Copyright 2018–2021 Jahn Fuchs\n"
               "Current development copyright 2026 Guillaume Binet"),
      {},
      QStringLiteral("https://github.com/gbin/Projecteur"),
      QStringLiteral("https://github.com/gbin/Projecteur/issues"));

    aboutData.setOrganizationDomain("projecteur.org");
    aboutData.setDesktopFileName(QStringLiteral("org.projecteur.Projecteur"));
    aboutData.setOtherText(
      i18n("Official KDE Plasma/Wayland edition of Projecteur.\n\n"
               "Build information:\n"
               "Git branch: %1\n"
               "Git hash: %2\n"
               "Build type: %3",
             QString::fromUtf8(projecteur::version_branch()),
             QString::fromUtf8(projecteur::version_shorthash()),
             QString::fromUtf8(projecteur::version_buildtype())));

    aboutData.addAuthor(
      QStringLiteral("Guillaume Binet"), i18n("Projecteur maintainer"), {},
      QStringLiteral("https://github.com/gbin"));
    aboutData.addAuthor(
      QStringLiteral("Jahn Fuchs"), i18n("Original Projecteur author"), {},
      QStringLiteral("https://github.com/jahnf"));

    const struct {
      const char* name;
      const char* githubName;
    } contributors[] = {
      {"Ricardo Jesus", "rj-jesus"},
      {"Mayank Suman", "mayanksuman"},
      {"Tiziano Müller", "dev-zero"},
      {"Torsten Maehne", "maehne"},
      {"TBK", "TBK"},
      {"Louie Lu", "mlouielu"},
      {"fmuelle4711", "fmuelle4711"},
      {"Deniz Bahadir", "Bagira80"},
      {"Tomáš Chvátal", "scarabeusiv"},
      {"Brandon Johnson", "dbrandonjohnson"},
      {"Stuart Prescott", "llimeht"},
      {"Crista Renouard", "Lumnicence"},
      {"freddii", "freddii"},
      {"Matthias Blümel", "Blaimi"},
      {"Grzegorz Szymaszek", "gszy"},
      {"TheAssassin", "TheAssassin"},
    };
    for (const auto& contributor : contributors) {
      aboutData.addCredit(
        QString::fromUtf8(contributor.name), i18n("Contributor"), {},
        QStringLiteral("https://github.com/%1").arg(QString::fromUtf8(contributor.githubName)));
    }
    return aboutData;
  }

  std::ostream& operator<<(std::ostream& os, const QString& s) {
    os << s.toStdString();
    return os;
  }

  struct print {
    template<typename T>
    auto& operator<<(const T& a) const { return std::cout << a; }
    ~print() { std::cout << std::endl; }
  };

  struct error {
    template<typename T>
    auto& operator<<(const T& a) const { return std::cerr << a; }
    ~error() { std::cerr << std::endl; }
  };

  void ctrl_c_signal_handler(int sig)
  {
    if (sig == SIGINT) {
      print() << "...";
      if (qApp) { QCoreApplication::quit(); }
    }
  }

  // -----------------------------------------------------------------------------------------------
  // Helper function to get the range of valid values for a string property
  QString getValuesDescription(const Settings::StringProperty& sp)
  {
    if (sp.type == Settings::StringProperty::Type::Integer
        || sp.type == Settings::StringProperty::Type::Double) {
      return QString("(%1 ... %2)").arg(sp.range[0].toString(), sp.range[1].toString());
    }

    if (sp.type == Settings::StringProperty::Type::Bool) {
      return "(false, true)";
    }

    if (sp.type == Settings::StringProperty::Type::Color) {
      return "(HTML-color; #RRGGBB)";
    }

    if (sp.type == Settings::StringProperty::Type::StringEnum) {
      QStringList values;
      for (const auto& v : sp.range) {
        values.push_back(v.toString());
      }
      return QString("(%1)").arg(values.join(", "));
    }
    return QString();
  }

  // -----------------------------------------------------------------------------------------------
  void printVersionInfo(const ProjecteurApplication::Options& options, bool fullVersionOption)
  {
    print() << QCoreApplication::applicationName().toStdString() << " "
            << projecteur::version_string();

    if (fullVersionOption ||
        (std::string(projecteur::version_branch()) != "master" &&
          std::string(projecteur::version_branch()) != "not-within-git-repo"))
    { // Not a build from master branch, print out additional information:
      print() << "  - git-branch: " << projecteur::version_branch();
      print() << "  - git-hash: " << projecteur::version_fullhash();
    }

    // Show if we have a build from modified sources
    if (projecteur::version_isdirty()) {
      print() << "  - dirty-flag: " << projecteur::version_isdirty();
    }

    // Additional useful information
    if (fullVersionOption)
    {
      print() << "  - compiler: " << XSTRINGIFY(CXX_COMPILER_ID) << " "
                                  << XSTRINGIFY(CXX_COMPILER_VERSION);
      print() << "  - build-type: " << projecteur::version_buildtype();
      print() << "  - qt-version: (build: " << QT_VERSION_STR << ", runtime: " << qVersion() << ")";

      const auto result = DeviceScan::getDevices(options.additionalDevices);
      print() << "  - device-scan: "
              << QString("(errors: %1, devices: %2 [readable: %3, writable: %4])")
                  .arg(result.errorMessages.size()).arg(result.devices.size())
                  .arg(result.numDevicesReadable).arg(result.numDevicesWritable);
    }
  }

  // -----------------------------------------------------------------------------------------------
  void printDeviceInfo(const ProjecteurApplication::Options& options)
  {
    const auto result = DeviceScan::getDevices(options.additionalDevices);
    print() << QCoreApplication::applicationName() << " "
            << projecteur::version_string() << "; " << i18n("device scan") << std::endl;

    for (const auto& errmsg : result.errorMessages) {
      print() << "** " << i18n("Error: ") << errmsg;
    }

    print() << (!result.errorMessages.empty() ? "\n" : "")
            << i18np(" * Found one supported device. (%2 readable, %3 writable)",
                     " * Found %1 supported devices. (%2 readable, %3 writable)",
                     result.devices.size(), result.numDevicesReadable, result.numDevicesWritable);

    for (const auto& device : result.devices)
    {
      print() << "\n"
              << " +++ " << "name:     '" << device.name << "'";
      if (!device.userName.isEmpty()) {
        print() << "     " << "userName: '" << device.userName << "'";
      }

      const QStringList subDeviceList = [&device](){
        QStringList subDeviceList;
        for (const auto& sd: device.subDevices) {
          if (sd.deviceFile.size()) { subDeviceList.push_back(sd.deviceFile); }
        }
        return subDeviceList;
      }();

      const bool allReadable = std::all_of(device.subDevices.cbegin(), device.subDevices.cend(),
      [](const auto& subDevice){
        return subDevice.deviceReadable;
      });

      const bool allWriteable = std::all_of(device.subDevices.cbegin(), device.subDevices.cend(),
      [](const auto& subDevice){
        return subDevice.deviceWritable;
      });

      print() << "     " << "vendorId:  " << formatHexId(device.id.vendorId);
      print() << "     " << "productId: " << formatHexId(device.id.productId);
      print() << "     " << "phys:      " << device.id.phys;
      print() << "     " << "busType:   " << toString(device.id.busType);
      print() << "     " << "devices:   " << subDeviceList.join(", ");
      print() << "     " << "readable:  " << (allReadable ? "true" : "false");
      print() << "     " << "writable:  " << (allWriteable ? "true" : "false");
    }
  }

  // -----------------------------------------------------------------------------------------------
  void addDevices(ProjecteurApplication::Options& options, const QStringList& devices)
  {
    for (auto& deviceValue : devices) {
      const auto devAttribs = deviceValue.split(":");
      const uint16_t vendorId = devAttribs.size() > 0 ? devAttribs[0].toUShort(nullptr, 16) : 0;
      const uint16_t productId = devAttribs.size() > 1 ? devAttribs[1].toUShort(nullptr, 16) : 0;
      if (vendorId == 0 || productId == 0) {
        error() << i18n("Invalid vendor/productId pair: ") << deviceValue;
      } else {
        const QString name = (devAttribs.size() >= 3) ? devAttribs[2] : "";
        options.additionalDevices.push_back({vendorId, productId, false, name});
      }
    }
  }

  // -----------------------------------------------------------------------------------------------
  struct ProjecteurCmdLineParser
  {
    QCommandLineParser parser;

    const QCommandLineOption versionOption_ = {QStringList{ "v", "version"}, i18n("Print application version.")};
    const QCommandLineOption fullVersionOption_ = QCommandLineOption{QStringList{ "f", "fullversion" }};
    const QCommandLineOption helpOption_ = {QStringList{ "h", "help"}, i18n("Show command line usage.")};
    const QCommandLineOption fullHelpOption_ = {QStringList{ "help-all"}, i18n("Show complete command line usage with all properties.")};
    const QCommandLineOption cfgFileOption_ = {QStringList{ "cfg" }, i18n("Set custom config file."), "file"};
    const QCommandLineOption commandOption_ = {QStringList{ "c", "command"}, i18n("Send command/property to a running instance."), "cmd"};
    const QCommandLineOption deviceInfoOption_ = {QStringList{ "d", "device-scan"}, i18n("Print device-scan results.")};
    const QCommandLineOption disableUInputOption_ = {QStringList{ "disable-uinput" }, i18n("Disable uinput support.")};
    const QCommandLineOption showDlgOnStartOption_ = {QStringList{ "show-dialog" }, i18n("Show preferences dialog on start.")};
    const QCommandLineOption hideSysTrayOption_ = {QStringList{ "hide-systray-icon"}, i18n("Hide the system tray icon.")};
    const QCommandLineOption dialogMinOnlyOption_ = {QStringList{ "m", "minimize-only" }, i18n("Only allow minimizing the dialog.")};
    const QCommandLineOption disableOverlayOption_ = {QStringList{ "disable-overlay" }, i18n("Disable spotlight overlay completely.")};
    const QCommandLineOption additionalDeviceOption_ = {QStringList{ "D", "additional-device"},
                               i18n("Additional accepted device; DEVICE = vendorId:productId\n"
                                        "                         "
                                        "e.g., -D 04b3:310c; e.g. -D 0x0c45:0x8101"), "device"};

    // ---------------------------------------------------------------------------------------------
    ProjecteurCmdLineParser()
    {
      parser.setApplicationDescription(i18n("Wayland application for the Logitech Spotlight device."));
      parser.addOptions({versionOption_, helpOption_, fullHelpOption_, commandOption_,
                        cfgFileOption_, fullVersionOption_, deviceInfoOption_,
                        disableUInputOption_, showDlgOnStartOption_, dialogMinOnlyOption_,
                        disableOverlayOption_, additionalDeviceOption_, hideSysTrayOption_});
    }

    // ---------------------------------------------------------------------------------------------
    bool versionOptionSet() const { return parser.isSet(versionOption_); }
    bool fullVersionOptionSet() const { return parser.isSet(fullVersionOption_); }
    bool helpOptionSet() const { return parser.isSet(helpOption_); }
    bool fullHelpOptionSet() const { return parser.isSet(fullHelpOption_); }
    bool additionalDeviceOptionSet() const { return parser.isSet(additionalDeviceOption_); }
    auto additionalDeviceOptionValues() const { return parser.values(additionalDeviceOption_); }
    bool deviceInfoOptionSet() const { return parser.isSet(deviceInfoOption_); }
    bool commandOptionSet() const { return parser.isSet(commandOption_); }
    bool disableUInputOptionSet() const { return parser.isSet(disableUInputOption_); }
    bool showDlgOnStartOptionSet() const { return parser.isSet(showDlgOnStartOption_); }
    bool dialogMinOnlyOptionSet() const { return parser.isSet(dialogMinOnlyOption_); }
    bool disableOverlayOptionSet() const { return parser.isSet(disableOverlayOption_); }
    auto commandOptionValues() const { return parser.values(commandOption_); }
    bool cfgFileOptionSet() const { return parser.isSet(cfgFileOption_); }
    auto cfgFileOptionValue() const { return parser.value(cfgFileOption_); }
    bool hideSysTrayOptionSet() const { return parser.isSet(hideSysTrayOption_); }

    // ---------------------------------------------------------------------------------------------
    void processArgs(int argc, char** argv)
    {
      const QStringList args = [argc, &argv]()
      {
        const QStringList qtAppKeyValueOptions = {
          "-platform", "-platformpluginpath", "-platformtheme", "-plugin", "-display"
        };
        const QStringList qtAppSingleOptions = {"-reverse"};
        QStringList args;
        for (int i = 0; i < argc; ++i)
        { // Skip some default arguments supported by QtGuiApplication, we don't want to parse them
          // but they will get passed through to the ProjecteurApp.
          if (qtAppKeyValueOptions.contains(argv[i])) { ++i; }
          else if (qtAppSingleOptions.contains(argv[i])) { continue; }
          else { args.push_back(argv[i]); }
        }
        return args;
      }();

      parser.process(args);
    }

    // ---------------------------------------------------------------------------------------------
    auto value(const QCommandLineOption& option) const { return parser.value(option); }
    auto isSet(const QCommandLineOption& option) const { return parser.isSet(option); }
    auto values(const QCommandLineOption& option) const { return parser.values(option); }

    // ---------------------------------------------------------------------------------------------
    void printHelp(bool fullHelp)
    {
      print() << QCoreApplication::applicationName() << " "
              << projecteur::version_string() << std::endl;
      print() << "Usage: projecteur [OPTION]..." << std::endl;
      print() << "<Options>";
      print() << "  -h, --help             " << helpOption_.description();
      print() << "  --help-all             " << fullHelpOption_.description();
      print() << "  -v, --version          " << versionOption_.description();
      print() << "  --cfg FILE             " << cfgFileOption_.description();
      print() << "  -d, --device-scan      " << deviceInfoOption_.description();
      print() << "  -D DEVICE              " << additionalDeviceOption_.description();
      if (fullHelp) {
        print() << "  --disable-uinput       " << disableUInputOption_.description();
        print() << "  --show-dialog          " << showDlgOnStartOption_.description();
        print() << "  --hide-systray-icon    " << hideSysTrayOption_.description();
        print() << "  -m, --minimize-only    " << dialogMinOnlyOption_.description();
      }
      print() << "  -c COMMAND|PROPERTY    " << commandOption_.description() << std::endl;
      print() << "<Commands>";
      print() << "  spot=[on|off|toggle]     " << i18n("Turn spotlight on/off or toggle.");
      print() << "  laser=[on|off|toggle]    " << i18n("Turn laser pointer on/off or toggle.");
      print() << "  pointer=[spotlight|laser|toggle] " << i18n("Set or toggle pointer mode.");
      if (fullHelp) {
        print() << "  preset=NAME              " << i18n("Set a preset.");
        print() << "  vibrate[=I[,L]]          " << i18n("Send vibrate command to device with intensity,length.");
        print() << "  spot.size.adjust=[+|-]N  " << i18n("Increase or decrease spot size by N.");
        print() << "  laser.size.adjust=[+|-]N " << i18n("Increase or decrease laser size by N.");
      }
      print() << "  settings=[show|hide]     " << i18n("Show/hide preferences dialog.");
      print() << "  quit                     " << i18n("Quit the running instance.");

      // Early return if the user not explicitly requested the full help
      if (!fullHelp) { return; }

      print() << "\n" << "<Properties>";
      int maxPropertyStringLength = 0;

      const std::vector<std::pair<QString, QString>> propertiesList =
        [&maxPropertyStringLength]()
        {
          std::vector<std::pair<QString, QString>> list;
          // Fill temporary list with properties to be able to format our output better
          Settings settings; // <-- FIXME unnecessary Settings instance
          for (const auto& sp : settings.stringProperties())
          {
            list.emplace_back(
              QString("%1=[%2]").arg(sp.first, sp.second.typeToString(sp.second.type)),
                                     getValuesDescription(sp.second));

            maxPropertyStringLength = qMax(maxPropertyStringLength, list.back().first.size());
          }
          return list;
        }();

      for (const auto& sp : propertiesList) {
        print() << "  " << std::left << std::setw(maxPropertyStringLength + 3) << sp.first << sp.second;
      }
    }
  };

} // end anonymous namespace


// -------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  KLocalizedString::setApplicationDomain("projecteur");
  const auto aboutData = projecteurAboutData();
  KAboutData::setApplicationData(aboutData);
  QCoreApplication::setApplicationName(aboutData.componentName());
  QCoreApplication::setOrganizationDomain(aboutData.organizationDomain());
  QCoreApplication::setApplicationVersion(aboutData.version());
  QGuiApplication::setApplicationDisplayName(aboutData.displayName());
  QGuiApplication::setDesktopFileName(aboutData.desktopFileName());
  ProjecteurApplication::Options options;
  {
    ProjecteurCmdLineParser parser;
    parser.processArgs(argc, argv);

    if (parser.helpOptionSet() || parser.fullHelpOptionSet())
    {
      parser.printHelp(parser.fullHelpOptionSet());
      return 0;
    }

    if (parser.additionalDeviceOptionSet()) {
      addDevices(options, parser.additionalDeviceOptionValues());
    }

    // Print version information, if option is set
    if (parser.versionOptionSet() || parser.fullVersionOptionSet())
    {
      printVersionInfo(options, parser.fullVersionOptionSet());
      return 0;
    }

    // Print device information if option is set
    if (parser.deviceInfoOptionSet())
    {
      printDeviceInfo(options);
      return 0;
    }

    // Check and trim ipc commands if set
    if (parser.commandOptionSet())
    {
      options.commands = parser.commandOptionValues();
      for (auto& value : options.commands) {
        value = value.trimmed();
      }
      options.commands.removeAll("");

      if (options.commands.isEmpty()) {
        error() << i18n("Command/Properties cannot be an empty string.");
        return PROJECTEUR_ERROR_EMPTY_COMMAND_PROPS;
      }
    }

    if (parser.cfgFileOptionSet()) {
      options.configFile = parser.cfgFileOptionValue();
    }

    options.enableUInput = !parser.disableUInputOptionSet();
    options.showPreferencesOnStart = parser.showDlgOnStartOptionSet();
    options.dialogMinimizeOnly = parser.dialogMinOnlyOptionSet();
    options.disableOverlay = parser.disableOverlayOptionSet();
    options.hideSysTrayIcon = parser.hideSysTrayOptionSet();

  }

  ProjecteurApplication app(argc, argv, options);
  if (!app.isPrimaryInstance()) {
    if (app.startupExitCode() == PROJECTEUR_ERROR_NO_INSTANCE_FOUND) {
      error() << i18n("Cannot send commands '%1' - no running application instance found.",
                      options.commands.join("; "));
    }
    return app.startupExitCode();
  }

  QObject::connect(app.dbusService(), &KDBusService::activateRequested, &app,
    [&app](const QStringList& arguments, const QString& /*workingDirectory*/) {
      QStringList commands;
      bool showPreferences = false;
      for (qsizetype i = 1; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if ((argument == QStringLiteral("-c") || argument == QStringLiteral("--command"))
            && i + 1 < arguments.size()) {
          commands.push_back(arguments[++i].trimmed());
        } else if (argument.startsWith(QStringLiteral("--command="))) {
          commands.push_back(argument.mid(QStringLiteral("--command=").size()).trimmed());
        } else if (argument == QStringLiteral("--show-dialog")) {
          showPreferences = true;
        }
      }
      commands.removeAll(QString());
      if (!commands.isEmpty()) {
        app.applyCommands(commands);
      } else if (showPreferences) {
        app.activate();
      }
    });

  signal(SIGINT, ctrl_c_signal_handler);
  return app.exec();
}
