// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QTranslator>

class LanguageManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
  public:
    explicit LanguageManager(const QString& root, QObject* parent = nullptr);
    ~LanguageManager() override;
    QString language() const { return current; }
    bool ready() const { return catalogReady; }
    Q_INVOKABLE bool setLanguage(const QString& language);
  signals:
    void languageChanged();

  private:
    QString preferenceFile, current;
    QTranslator english, finnish;
    bool catalogReady = false;
    void apply(const QString& language);
};
