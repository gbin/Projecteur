// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

import QtQuick
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    readonly property var backend: Plasmoid

    function formatTimer(seconds) {
        const safeSeconds = Math.max(0, seconds);
        const hours = Math.floor(safeSeconds / 3600);
        const minutes = Math.floor((safeSeconds % 3600) / 60);
        const remainingSeconds = safeSeconds % 60;
        const mm = minutes.toString().padStart(2, "0");
        const ss = remainingSeconds.toString().padStart(2, "0");
        return hours > 0 ? hours.toString() + ":" + mm + ":" + ss : mm + ":" + ss;
    }

    switchWidth: 320
    switchHeight: 320
    activationTogglesExpanded: true
    hideOnWindowDeactivate: true
    Plasmoid.icon: "projecteur"
    badgeText: {
        if (!backend || !backend.serviceAvailable || !backend.timerAvailable
                || !backend.timerEnabled)
            return "";

        if (backend.timerState === "completed")
            return "!";

        if (backend.timerState === "running") {
            const seconds = Math.max(0, backend.timerRemainingSeconds);
            return seconds >= 60 ? Math.ceil(seconds / 60).toString() : seconds.toString();
        }

        return "…";
    }
    Plasmoid.status: {
        if (!backend || !backend.serviceAvailable || !backend.trayVisible)
            return PlasmaCore.Types.HiddenStatus;
        if (backend.timerAvailable && backend.timerEnabled
                && backend.timerState === "completed")
            return PlasmaCore.Types.NeedsAttentionStatus;
        return PlasmaCore.Types.ActiveStatus;
    }
    toolTipMainText: i18n("Projecteur")
    toolTipSubText: {
        if (!backend || !backend.serviceAvailable)
            return i18n("Projecteur is not running");

        if (backend.timerAvailable && backend.timerEnabled
                && backend.timerState === "running")
            return i18n("Presentation timer: %1 remaining", root.formatTimer(backend.timerRemainingSeconds));

        if (backend.timerAvailable && backend.timerEnabled
                && backend.timerState === "completed")
            return i18n("Presentation timer finished");

        if (backend.timerAvailable && backend.timerEnabled
                && backend.timerState === "idle")
            return i18n("Timer ready: %1 — starts on the next presenter button press",
                        root.formatTimer(backend.timerDurationSeconds));

        if (backend.connectedDevices.length === 0)
            return i18n("No presenter connected");

        return i18np("%1 connected presenter", "%1 connected presenters", backend.connectedDevices.length);
    }
    Plasmoid.onActivated: root.expanded = !root.expanded

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("About Projecteur")
            icon.name: "help-about-symbolic"
            enabled: backend && backend.serviceAvailable
            onTriggered: backend.showAbout()
        },
        PlasmaCore.Action {
            text: i18n("Quit Projecteur")
            icon.name: "application-exit-symbolic"
            enabled: backend && backend.serviceAvailable
            onTriggered: backend.quitProjecteur()
        }
    ]

    fullRepresentation: FullRepresentation {
        backend: root.backend
        plasmoidItem: root
    }

}
