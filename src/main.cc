// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "projecteurapp.h"
#include "projecteur-GitVersion.h"

#include "logging.h"
#include "runguard.h"
#include "settings.h"

#include <QCommandLineParser>

#ifndef NDEBUG
#include <QQmlDebuggingEnabler>
#endif

#include <csignal>
#include <iomanip>
#include <iostream>

#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(x) #x

LOGGING_CATEGORY(appMain, "main")

namespace {
  // -----------------------------------------------------------------------------------------------
  constexpr int PROJECTEUR_ERROR_ANOTHER_INST_RUNNING = 42;
  constexpr int PROJECTEUR_ERROR_NO_INSTANCE_FOUND = 43;
  constexpr int PROJECTEUR_ERROR_EMPTY_COMMAND_PROPS = 44;

  // -----------------------------------------------------------------------------------------------
  class Main : public QObject {};

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
            << projecteur::version_string() << "; " << Main::tr("device scan") << std::endl;

    for (const auto& errmsg : result.errorMessages) {
      print() << "** " << Main::tr("Error: ") << errmsg;
    }

    print() << (!result.errorMessages.empty() ? "\n" : "")
            << Main::tr(" * Found %1 supported devices. (%2 readable, %3 writable)")
                .arg(result.devices.size()).arg(result.numDevicesReadable).arg(result.numDevicesWritable);

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

      print() << "     " << "vendorId:  " << logging::hexId(device.id.vendorId);
      print() << "     " << "productId: " << logging::hexId(device.id.productId);
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
        error() << Main::tr("Invalid vendor/productId pair: ") << deviceValue;
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

    const QCommandLineOption versionOption_ = {QStringList{ "v", "version"}, Main::tr("Print application version.")};
    const QCommandLineOption fullVersionOption_ = QCommandLineOption{QStringList{ "f", "fullversion" }};
    const QCommandLineOption helpOption_ = {QStringList{ "h", "help"}, Main::tr("Show command line usage.")};
    const QCommandLineOption fullHelpOption_ = {QStringList{ "help-all"}, Main::tr("Show complete command line usage with all properties.")};
    const QCommandLineOption cfgFileOption_ = {QStringList{ "cfg" }, Main::tr("Set custom config file."), "file"};
    const QCommandLineOption commandOption_ = {QStringList{ "c", "command"}, Main::tr("Send command/property to a running instance."), "cmd"};
    const QCommandLineOption deviceInfoOption_ = {QStringList{ "d", "device-scan"}, Main::tr("Print device-scan results.")};
    const QCommandLineOption logLvlOption_ = {QStringList{ "l", "log-level" }, Main::tr("Set log level (dbg,inf,wrn,err)."), "lvl"};
    const QCommandLineOption disableUInputOption_ = {QStringList{ "disable-uinput" }, Main::tr("Disable uinput support.")};
    const QCommandLineOption showDlgOnStartOption_ = {QStringList{ "show-dialog" }, Main::tr("Show preferences dialog on start.")};
    const QCommandLineOption hideSysTrayOption_ = {QStringList{ "hide-systray-icon"}, Main::tr("Hide the system tray icon.")};
    const QCommandLineOption dialogMinOnlyOption_ = {QStringList{ "m", "minimize-only" }, Main::tr("Only allow minimizing the dialog.")};
    const QCommandLineOption disableOverlayOption_ = {QStringList{ "disable-overlay" }, Main::tr("Disable spotlight overlay completely.")};
    const QCommandLineOption additionalDeviceOption_ = {QStringList{ "D", "additional-device"},
                               Main::tr("Additional accepted device; DEVICE = vendorId:productId\n"
                                        "                         "
                                        "e.g., -D 04b3:310c; e.g. -D 0x0c45:0x8101"), "device"};

    // ---------------------------------------------------------------------------------------------
    ProjecteurCmdLineParser()
    {
      parser.setApplicationDescription(Main::tr("Linux/X11 application for the Logitech Spotlight device."));
      parser.addOptions({versionOption_, helpOption_, fullHelpOption_, commandOption_,
                        cfgFileOption_, fullVersionOption_, deviceInfoOption_, logLvlOption_,
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
    bool logLvlOptionSet() const { return parser.isSet(logLvlOption_); }
    auto logLvlOptionValue() const { return parser.value(logLvlOption_); }
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
      print() << "  -l, --log-level LEVEL  " << logLvlOption_.description();
      print() << "  -D DEVICE              " << additionalDeviceOption_.description();
      if (fullHelp) {
        print() << "  --disable-uinput       " << disableUInputOption_.description();
        print() << "  --show-dialog          " << showDlgOnStartOption_.description();
        print() << "  --hide-systray-icon    " << hideSysTrayOption_.description();
        print() << "  -m, --minimize-only    " << dialogMinOnlyOption_.description();
      }
      print() << "  -c COMMAND|PROPERTY    " << commandOption_.description() << std::endl;
      print() << "<Commands>";
      print() << "  spot=[on|off|toggle]     " << Main::tr("Turn spotlight on/off or toggle.");
      if (fullHelp) {
        print() << "  preset=NAME              " << Main::tr("Set a preset.");
        print() << "  vibrate[=I[,L]]          " << Main::tr("Send vibrate command to device with intensity,length.");
        print() << "  spot.size.adjust=[+|-]N  " << Main::tr("Increase or decrease spot size by N.");
      }
      print() << "  settings=[show|hide]     " << Main::tr("Show/hide preferences dialog.");
      if (fullHelp) {
        print() << "  preset=NAME              " << Main::tr("Set a preset.");
      }
      print() << "  quit                     " << Main::tr("Quit the running instance.");

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
  QCoreApplication::setApplicationName("Projecteur");
  QCoreApplication::setApplicationVersion(projecteur::version_string());
  ProjecteurApplication::Options options;
  QStringList ipcCommands;
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
      ipcCommands = parser.commandOptionValues();
      for (auto& value : ipcCommands) {
        value = value.trimmed();
      }
      ipcCommands.removeAll("");

      if (ipcCommands.isEmpty()) {
        error() << Main::tr("Command/Properties cannot be an empty string.");
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

    if (parser.logLvlOptionSet()) {
      const auto lvl = logging::levelFromName(parser.logLvlOptionValue());
      if (lvl != logging::level::unknown) {
        logging::setCurrentLevel(lvl);
      } else {
        error() << Main::tr("Cannot set log level, unknown level: '%1'").arg(parser.logLvlOptionValue());
      }
    }
  }

  RunGuard guard(QCoreApplication::applicationName());
  if (!guard.tryToRun())
  {
    if (ipcCommands.size() > 0) {
      return ProjecteurCommandClientApp(ipcCommands, argc, argv).exec();
    }
    error() << Main::tr("Another application instance is already running. Exiting.");
    return PROJECTEUR_ERROR_ANOTHER_INST_RUNNING;
  }

  if (ipcCommands.size() > 0)
  {
    // No other application instance running - but command option was used.
    logInfo(appMain) << Main::tr("Cannot send commands '%1' - no running application instance found.").arg(ipcCommands.join("; "));
    logWarning(appMain) << Main::tr("Cannot send commands '%1' - no running application instance found.").arg(ipcCommands.join("; "));
    error() << Main::tr("Cannot send commands '%1' - no running application instance found.").arg(ipcCommands.join("; "));
    return PROJECTEUR_ERROR_NO_INSTANCE_FOUND;
  }

  ProjecteurApplication app(argc, argv, options);
  signal(SIGINT, ctrl_c_signal_handler);
  return app.exec();
}
