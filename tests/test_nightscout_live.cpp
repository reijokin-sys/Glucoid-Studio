#include "storefixtures.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Optional live Nightscout check. Runs only when both environment variables
// are present; the token is never printed. Use synthetic test data only.
#include "nightscout.h"
#include "store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <memory>

using namespace Studio;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QByteArray url = qgetenv("STUDIO_NIGHTSCOUT_URL");
    const QByteArray token = qgetenv("STUDIO_NIGHTSCOUT_TOKEN");
    if (url.isEmpty() || token.isEmpty()) {
        std::fprintf(stderr, "SKIP: set STUDIO_NIGHTSCOUT_URL and STUDIO_NIGHTSCOUT_TOKEN\n");
        return 77;
    }
    std::unique_ptr<QTemporaryDir> temp;
    QString root = QString::fromUtf8(qgetenv("STUDIO_NIGHTSCOUT_ROOT"));
    if (root.isEmpty()) {
        temp = std::make_unique<QTemporaryDir>();
        root = temp->path();
    }
    QDir().mkpath(root);
    const QString database = nightscoutDatasetPath(root, QString::fromUtf8(url));
    QDir().mkpath(QFileInfo(database).absolutePath());
    FixtureStore store(database);
    if (!store.good()) {
        std::fprintf(stderr, "store unavailable\n");
        return 1;
    }
    std::printf("root=%s\n", qPrintable(root));
    NightscoutImporter importer(store, QString::fromUtf8(url), QString::fromUtf8(token));
    QObject::connect(&importer, &NightscoutImporter::finished, [&](bool ok, const QString& message) {
        std::printf("finished ok=%d message=%s\n", ok, qPrintable(message));
        if (ok) { std::printf("glucose=%d amounts=%d\n", int(store.readings(0, 9e15).size()), int(store.amounts(0, 9e15).size())); }
        app.exit(ok ? 0 : 1);
    });
    QTimer::singleShot(120000, &app, [&app] { app.exit(2); });
    importer.start();
    return app.exec();
}
