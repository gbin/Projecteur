use cxx_qt_build::{CxxQtBuilder, QmlModule};
use std::{env, fmt::Write as _, fs, path::PathBuf, process::Command};

fn main() {
    let output_directory = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo sets OUT_DIR"));
    let shader_directory = output_directory.join("shaders");
    fs::create_dir_all(&shader_directory).expect("create generated shader directory");
    let qsb = [
        env::var_os("QSB").map(PathBuf::from),
        Some(PathBuf::from("/usr/lib/qt6/bin/qsb")),
        Some(PathBuf::from("qsb")),
    ]
    .into_iter()
    .flatten()
    .find(|candidate| {
        Command::new(candidate)
            .arg("--version")
            .output()
            .is_ok_and(|output| output.status.success())
    })
    .expect("Qt Shader Tools 'qsb' is required to compile Projecteur's shaders");
    let mut shader_resources = String::from("<RCC><qresource prefix=\"/projecteur/shaders\">\n");
    for shader in ["diffusedot.frag", "dottrail.frag", "textzoom.frag"] {
        let source = PathBuf::from("../../qml/shaders").join(shader);
        let output = shader_directory.join(format!("{shader}.qsb"));
        println!("cargo:rerun-if-changed={}", source.display());
        let status = Command::new(&qsb)
            // A bare qsb pack has only SPIR-V; ShaderEffect also needs Qt RHI targets.
            .args(["--qt6", "-o"])
            .arg(&output)
            .arg(&source)
            .status()
            .unwrap_or_else(|error| panic!("run {}: {error}", qsb.display()));
        assert!(status.success(), "qsb failed for {}", source.display());
        writeln!(
            shader_resources,
            "<file alias=\"{shader}.qsb\">{}</file>",
            output.display()
        )
        .expect("append shader resource entry");
    }
    shader_resources.push_str("</qresource><qresource prefix=\"/projecteur\">\n");
    let tray_icon = PathBuf::from("../../icons/projecteur-tray.svg")
        .canonicalize()
        .expect("find Projecteur tray icon");
    writeln!(
        shader_resources,
        "<file alias=\"projecteur-tray.svg\">{}</file>",
        tray_icon.display()
    )
    .expect("append tray icon resource entry");
    shader_resources.push_str("</qresource></RCC>\n");
    let shader_qrc = output_directory.join("projecteur_shaders.qrc");
    fs::write(&shader_qrc, shader_resources).expect("write generated shader resource file");
    write_qml_loader(&output_directory);

    let builder = CxxQtBuilder::new_qml_module(
        QmlModule::new("org.projecteur.rust")
            .version(1, 0)
            .qml_files([
                "qml/Main.qml",
                "qml/OverlayPreview.qml",
                "qml/SettingsWindow.qml",
                "qml/ShapeMask.qml",
                "qml/SettingRow.qml",
                "qml/ColorButton.qml",
            ]),
    )
    .include_dir(output_directory)
    .qrc(shader_qrc)
    .qt_module("Widgets")
    .qt_module("DBus")
    .file("src/backend.rs");
    // GCC 16 diagnoses a harmless Qt template completeness probe in generated
    // bridge code. Keep Cargo output focused on warnings we can act on.
    let builder = unsafe {
        builder.cc_builder(|compiler| {
            compiler.flag_if_supported("-Wno-sfinae-incomplete");
            compiler.include("/usr/include/KF6");
            compiler.include("/usr/include/KF6/KGlobalAccel");
            compiler.include("/usr/include/KF6/KXmlGui");
            compiler.include("/usr/include/KF6/KConfigWidgets");
            compiler.include("/usr/include/KF6/KConfigGui");
            compiler.include("/usr/include/KF6/KConfigCore");
            compiler.include("/usr/include/KF6/KConfig");
            compiler.include("/usr/include/KF6/KWidgetsAddons");
            compiler.include("/usr/include/KF6/KCoreAddons");
            compiler.include("/usr/include/KF6/KNotifications");
        })
    };
    builder.build();
    // The generated bridge is a static archive and refers to QApplication.
    // Repeat Qt libraries referenced by the generated helper after that archive
    // so GNU ld with --as-needed keeps them.
    println!("cargo:rustc-link-arg=-lQt6Widgets");
    println!("cargo:rustc-link-arg=-lQt6DBus");
    println!("cargo:rustc-link-lib=KF6GlobalAccel");
    println!("cargo:rustc-link-lib=KF6XmlGui");
    println!("cargo:rustc-link-lib=KF6WidgetsAddons");
    println!("cargo:rustc-link-lib=KF6CoreAddons");
    println!("cargo:rustc-link-lib=KF6Notifications");
}

#[allow(clippy::too_many_lines)] // The generated C++ helper is kept in one auditable template.
fn write_qml_loader(output_directory: &std::path::Path) {
    fs::write(
        output_directory.join("projecteur_qml_loader.h"),
        r#"#pragma once
#include <QtCore/QAnyStringView>
#include <QtGui/QIcon>
#include <QtGui/QAction>
#include <QtGui/QKeySequence>
#include <QtQml/QQmlApplicationEngine>
#include <QtWidgets/QApplication>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>

#include <KActionCollection>
#include <KAboutApplicationDialog>
#include <KAboutData>
#include <KGlobalAccel>
#include <KNotification>
#include <KShortcutsDialog>

#include <memory>

namespace projecteur::generated {
inline KActionCollection*& global_action_collection()
{
    static KActionCollection* collection = nullptr;
    return collection;
}

inline void setup_application_metadata()
{
    KAboutData aboutData(
        QStringLiteral("Projecteur"), QStringLiteral("Projecteur"),
        qApp->applicationVersion(),
        QStringLiteral("A KDE Plasma spotlight for Logitech presenter devices."),
        KAboutLicense::MIT,
        QStringLiteral("Copyright 2018–2021 Jahn Fuchs\n"
                       "Current development copyright 2026 Guillaume Binet"),
        {}, QStringLiteral("https://github.com/gbin/Projecteur"),
        QStringLiteral("https://github.com/gbin/Projecteur/issues"));
    aboutData.setOrganizationDomain("projecteur.org");
    aboutData.setDesktopFileName(QStringLiteral("org.projecteur.Projecteur"));
    aboutData.setOtherText(QStringLiteral(
        "Official KDE Plasma/Wayland edition of Projecteur.\n\n"
        "This build uses the Rust application backend."));
    aboutData.addAuthor(
        QStringLiteral("Guillaume Binet"), QStringLiteral("Projecteur maintainer"), {},
        QStringLiteral("https://github.com/gbin"));
    aboutData.addAuthor(
        QStringLiteral("Jahn Fuchs"), QStringLiteral("Original Projecteur author"), {},
        QStringLiteral("https://github.com/jahnf"));
    KAboutData::setApplicationData(aboutData);
}

inline void show_about_dialog()
{
    auto* dialog = new KAboutApplicationDialog(KAboutData::applicationData());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

inline void send_notification(const QString& eventId, const QString& title,
                              const QString& text, const QString& iconName)
{
    KNotification::event(eventId, title, text, iconName,
                         KNotification::CloseOnTimeout,
                         QStringLiteral("projecteur"));
}

inline bool suppress_shake_cursor_effect()
{
    QDBusInterface effects(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
        QStringLiteral("org.kde.kwin.Effects"));
    if (!effects.isValid()) {
        return false;
    }
    const QDBusReply<bool> loaded = effects.call(
        QStringLiteral("isEffectLoaded"), QStringLiteral("shakecursor"));
    if (!loaded.isValid() || !loaded.value()) {
        return false;
    }
    const QDBusReply<void> unloaded = effects.call(
        QStringLiteral("unloadEffect"), QStringLiteral("shakecursor"));
    return unloaded.isValid();
}

inline bool restore_shake_cursor_effect()
{
    QDBusInterface effects(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
        QStringLiteral("org.kde.kwin.Effects"));
    if (!effects.isValid()) {
        return false;
    }
    const QDBusReply<bool> loaded = effects.call(
        QStringLiteral("loadEffect"), QStringLiteral("shakecursor"));
    return loaded.isValid() && loaded.value();
}

inline std::unique_ptr<QGuiApplication> create_widget_application()
{
    static int argc = 1;
    static char applicationName[] = "projecteur-rs";
    static char* argv[] = {applicationName, nullptr};
    auto application = std::unique_ptr<QGuiApplication>(new QApplication(argc, argv));
    application->setQuitOnLastWindowClosed(false);
    application->setDesktopFileName(QStringLiteral("org.projecteur.Projecteur"));
    application->setWindowIcon(QIcon(QStringLiteral(":/projecteur/projecteur-tray.svg")));
    return application;
}

inline void load_qml_module(QQmlApplicationEngine& engine,
                            QAnyStringView uri,
                            QAnyStringView type_name)
{
    engine.loadFromModule(uri, type_name);
}

inline void setup_global_shortcuts()
{
    if (global_action_collection()) {
        return;
    }
    auto* collection = new KActionCollection(qApp, QStringLiteral("Projecteur"));
    global_action_collection() = collection;
    collection->setComponentName(QStringLiteral("Projecteur"));
    collection->setComponentDisplayName(QStringLiteral("Projecteur"));

    const auto call = [](const QString& method, const QVariantList& arguments = {}) {
        QDBusInterface control(
            QStringLiteral("org.projecteur.Projecteur"),
            QStringLiteral("/org/projecteur/Projecteur/Control"),
            QStringLiteral("org.projecteur.Projecteur"));
        control.asyncCallWithArgumentList(method, arguments);
    };
    const auto addAction =
        [collection](const QString& id, const QString& text, const QString& iconName,
                     auto callback) {
            auto* action = new QAction(QIcon::fromTheme(iconName), text, collection);
            collection->addAction(id, action);
            QObject::connect(action, &QAction::triggered, collection, std::move(callback));
            KGlobalAccel::setGlobalShortcut(action, QList<QKeySequence>{});
        };

    addAction(QStringLiteral("toggle_spotlight"), QStringLiteral("Toggle Spotlight"),
              QStringLiteral("view-visible"), [call]() {
        call(QStringLiteral("ApplyCommands"), {QStringList{QStringLiteral("spot=toggle")}});
    });
    addAction(QStringLiteral("show_preferences"), QStringLiteral("Show Preferences"),
              QStringLiteral("configure"), [call]() {
        call(QStringLiteral("ShowPreferences"));
    });
    addAction(QStringLiteral("start_restart_timer"),
              QStringLiteral("Start or Restart Presentation Timer"),
              QStringLiteral("chronometer"), [call]() {
        call(QStringLiteral("RestartTimer"));
    });
    addAction(QStringLiteral("reset_timer"), QStringLiteral("Reset Presentation Timer"),
              QStringLiteral("edit-undo"), [call]() {
        call(QStringLiteral("ResetTimer"));
    });
    addAction(QStringLiteral("next_preset"), QStringLiteral("Next Spotlight Preset"),
              QStringLiteral("go-next"), [call]() {
        call(QStringLiteral("ApplyCommands"), {QStringList{QStringLiteral("preset.next")}});
    });
    addAction(QStringLiteral("previous_preset"), QStringLiteral("Previous Spotlight Preset"),
              QStringLiteral("go-previous"), [call]() {
        call(QStringLiteral("ApplyCommands"), {QStringList{QStringLiteral("preset.previous")}});
    });
}

inline void show_global_shortcuts_editor()
{
    if (auto* collection = global_action_collection()) {
        KShortcutsDialog::showDialog(
            collection, KShortcutsEditor::LetterShortcutsDisallowed);
    }
}

inline QString format_key_combination(qint32 key)
{
    return QKeySequence(key).toString(QKeySequence::NativeText);
}
} // namespace projecteur::generated
"#,
    )
    .expect("write generated QML loader header");
}
