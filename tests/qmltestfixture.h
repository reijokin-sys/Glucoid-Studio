// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "backend.h"
#include <QQmlApplicationEngine>
#include <QVariant>
#include <qqml.h>

// Registers the typed QML interface used by the application windows. The
// production binary gets the same registration from qt_add_qml_module.
inline void registerStudioQmlTypes()
{
    static const bool registered=[]{
        qmlRegisterUncreatableType<Backend>("GlucoidStudio",1,0,"Backend",
            QStringLiteral("Backend is created by the application"));
        return true;
    }();
    Q_UNUSED(registered)
}

// Supplies the root window's required properties before the QML component is
// created. This replaces the untyped context properties used before 1.5.3.
inline void setStudioQmlProperties(QQmlApplicationEngine &engine,Backend &backend,
    const QString &appVersion=QString(),bool demoMode=true,bool uiTestMode=true,
    int uiTestWidth=1280)
{
    engine.setInitialProperties({
        {"studio",QVariant::fromValue(&backend)},
        {"appVersion",appVersion},
        {"demoMode",demoMode},
        {"uiTestMode",uiTestMode},
        {"uiTestWidth",uiTestWidth}
    });
}
