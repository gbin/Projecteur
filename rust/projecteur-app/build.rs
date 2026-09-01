use cxx_qt_build::{CxxQtBuilder, QmlModule};
use std::{env, fs, path::PathBuf};

fn main() {
    let output_directory = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo sets OUT_DIR"));
    fs::write(
        output_directory.join("projecteur_qml_loader.h"),
        r"#pragma once
#include <QtCore/QAnyStringView>
#include <QtQml/QQmlApplicationEngine>

namespace projecteur::generated {
inline void load_qml_module(QQmlApplicationEngine& engine,
                            QAnyStringView uri,
                            QAnyStringView type_name)
{
    engine.loadFromModule(uri, type_name);
}
} // namespace projecteur::generated
",
    )
    .expect("write generated QML loader header");

    let builder = CxxQtBuilder::new_qml_module(
        QmlModule::new("org.projecteur.rust")
            .version(1, 0)
            .qml_files(["qml/Main.qml", "qml/OverlayPreview.qml"]),
    )
    .include_dir(output_directory)
    .file("src/backend.rs");
    // GCC 16 diagnoses a harmless Qt template completeness probe in generated
    // bridge code. Keep Cargo output focused on warnings we can act on.
    let builder = unsafe {
        builder.cc_builder(|compiler| {
            compiler.flag_if_supported("-Wno-sfinae-incomplete");
        })
    };
    builder.build();
}
