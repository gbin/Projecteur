mod backend;

use cxx_qt_lib::{QAnyStringView, QGuiApplication, QQmlApplicationEngine, QString};

fn main() {
    cxx_qt::init_crate!(projecteur_app);
    cxx_qt::init_qml_module!("org.projecteur.rust");

    let mut application = QGuiApplication::new();
    application
        .pin_mut()
        .set_application_name(&QString::from("projecteur-rs"));
    application
        .pin_mut()
        .set_application_display_name(&QString::from("Projecteur (Rust port)"));
    application
        .pin_mut()
        .set_application_version(&QString::from(env!("CARGO_PKG_VERSION")));
    application
        .pin_mut()
        .set_organization_domain(&QString::from("projecteur.org"));
    application
        .pin_mut()
        .set_organization_name(&QString::from("Projecteur"));

    let mut engine = QQmlApplicationEngine::new();
    let _creation_failure = engine.pin_mut().on_object_creation_failed(|_, url| {
        eprintln!("projecteur-rs: failed to load QML module from {url:?}");
    });
    eprintln!("projecteur-rs: loading QML module");
    backend::ffi::load_qml_module(
        engine.pin_mut(),
        QAnyStringView::from("org.projecteur.rust"),
        QAnyStringView::from("Main"),
    );
    eprintln!("projecteur-rs: QML load call completed");

    std::process::exit(application.pin_mut().exec());
}
