// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <KLocalizedString>

LanguageManager::LanguageManager(const QString& root, QObject* parent) : QObject(parent), preferenceFile(root + "/ui-language.json") {
    catalogReady = english.load(":/i18n/studio_en.qm") && finnish.load(":/i18n/studio_fi.qm");
    QFile file(preferenceFile);
    QString saved;
    if (file.open(QIODevice::ReadOnly)) saved = QJsonDocument::fromJson(file.readAll()).object()["language"].toString();
    apply(saved == "fi" ? "fi" : "en");
}
LanguageManager::~LanguageManager() {
    QCoreApplication::removeTranslator(&english);
    QCoreApplication::removeTranslator(&finnish);
}
void LanguageManager::apply(const QString& language) {
    QCoreApplication::removeTranslator(&english);
    QCoreApplication::removeTranslator(&finnish);
    current = language;
    QCoreApplication::installTranslator(current == "en" ? &english : &finnish);
    KLocalizedString::setLanguages({current});
    QLocale::setDefault(QLocale(current == "fi" ? "fi_FI" : "en_GB"));
}
bool LanguageManager::setLanguage(const QString& language) {
    if ((language != "en" && language != "fi") || !catalogReady) return false;
    if (language == current) return true;
    QSaveFile file(preferenceFile);
    if (!QDir().mkpath(QFileInfo(preferenceFile).absolutePath()) || !file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(QJsonObject{{"language", language}}).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) return false;
    apply(language);
    emit languageChanged();
    return true;
}
