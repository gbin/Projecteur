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
    .file("src/backend.rs");
    // GCC 16 diagnoses a harmless Qt template completeness probe in generated
    // bridge code. Keep Cargo output focused on warnings we can act on.
    let builder = unsafe {
        builder.cc_builder(|compiler| {
            compiler.flag_if_supported("-Wno-sfinae-incomplete");
        })
    };
    builder.build();
    // The generated bridge is a static archive and refers to QApplication.
    // Repeat Qt Widgets after that archive so GNU ld with --as-needed keeps it.
    println!("cargo:rustc-link-arg=-lQt6Widgets");
}

fn write_qml_loader(output_directory: &std::path::Path) {
    fs::write(
        output_directory.join("projecteur_qml_loader.h"),
        r#"#pragma once
#include <QtCore/QAnyStringView>
#include <QtGui/QIcon>
#include <QtQml/QQmlApplicationEngine>
#include <QtWidgets/QApplication>

#include <memory>

namespace projecteur::generated {
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
} // namespace projecteur::generated
"#,
    )
    .expect("write generated QML loader header");
}
