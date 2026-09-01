#![allow(clippy::unnecessary_box_returns)]

use core::pin::Pin;

use cxx_qt_lib::QString;

#[cxx_qt::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;

        include!("cxx-qt-lib/qanystringview.h");
        type QAnyStringView<'a> = cxx_qt_lib::QAnyStringView<'a>;

        include!("cxx-qt-lib/qqmlapplicationengine.h");
        type QQmlApplicationEngine = cxx_qt_lib::QQmlApplicationEngine;
    }

    #[namespace = "projecteur::generated"]
    unsafe extern "C++" {
        include!("projecteur_qml_loader.h");

        fn load_qml_module(
            engine: Pin<&mut QQmlApplicationEngine>,
            uri: QAnyStringView,
            type_name: QAnyStringView,
        );
    }

    #[auto_cxx_name]
    extern "RustQt" {
        /// Initial Rust backend exposed to QML during the migration.
        #[qobject]
        #[qml_element]
        #[qproperty(bool, show_window)]
        #[qproperty(QString, status)]
        type ProjecteurBackend = super::ProjecteurBackendRust;

        #[qinvokable]
        fn confirm_qml_loaded(self: Pin<&mut ProjecteurBackend>);
    }

    impl cxx_qt::Initialize for ProjecteurBackend {}
}

/// Rust-owned state behind the initial QML bridge.
pub struct ProjecteurBackendRust {
    show_window: bool,
    status: QString,
}

impl Default for ProjecteurBackendRust {
    fn default() -> Self {
        eprintln!("projecteur-rs: constructing Rust backend");
        Self {
            show_window: std::env::args_os().any(|argument| argument == "--show-window"),
            status: QString::from("Rust backend initialized"),
        }
    }
}

impl cxx_qt::Initialize for ffi::ProjecteurBackend {
    fn initialize(self: Pin<&mut Self>) {}
}

impl ffi::ProjecteurBackend {
    fn confirm_qml_loaded(mut self: Pin<&mut Self>) {
        self.as_mut()
            .set_status(QString::from("Rust backend and QML are connected"));
        eprintln!("projecteur-rs: Rust backend and QML are connected");
    }
}
