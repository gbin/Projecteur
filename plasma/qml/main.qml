// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

import QtQuick
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    readonly property var backend: Plasmoid

    switchWidth: 320
    switchHeight: 320
    activationTogglesExpanded: true
    hideOnWindowDeactivate: true
    Plasmoid.icon: "projecteur"
    Plasmoid.status: backend && backend.serviceAvailable && backend.trayVisible ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.HiddenStatus
    toolTipMainText: i18n("Projecteur")
    toolTipSubText: {
        if (!backend || !backend.serviceAvailable)
            return i18n("Projecteur is not running");

        if (backend.connectedDevices.length === 0)
            return i18n("No presenter connected");

        return i18np("%1 connected presenter", "%1 connected presenters", backend.connectedDevices.length);
    }
    Plasmoid.onActivated: root.expanded = !root.expanded

    fullRepresentation: FullRepresentation {
        backend: root.backend
        plasmoidItem: root
    }

}
