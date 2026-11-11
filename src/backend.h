// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include "transport.h"
#include "importer.h"
#include "observation.h"
#include "glucoseanalysis.h"
#include "dailyanalysis.h"
#include "nightscout.h"
#include <QFutureWatcher>
#include <QVariant>
#include <QLockFile>
#include <QtQml/qqml.h>
#include <memory>

class Backend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Backend is created by the application")
    Q_PROPERTY(QVariantMap dailyAnalysis READ dailyAnalysis NOTIFY changed)
    Q_PROPERTY(QVariantMap currentReading READ currentReading NOTIFY currentReadingChanged)
    Q_PROPERTY(QVariantMap observation READ observation NOTIFY changed)
    Q_PROPERTY(QVariantMap analysis READ analysis NOTIFY changed)
    Q_PROPERTY(QVariantList dayComparisonCharts READ dayComparisonCharts NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool backgroundBusy READ backgroundBusy NOTIFY changed)
    Q_PROPERTY(bool updateInProgress READ updateInProgress NOTIFY changed)
    Q_PROPERTY(bool updateComplete READ updateComplete NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
    Q_PROPERTY(QString backupStatus READ backupStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap currentDayChart READ currentDayChart NOTIFY changed)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY changed)
    Q_PROPERTY(QVariantMap connection READ connection NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString dataRevision READ dataRevision NOTIFY changed)
  public:
    explicit Backend(const QString& root, QObject* parent = nullptr, Studio::Importer::Fetch jugglucoFetch = {});
    ~Backend() override;
    void enableBackgroundWork();
    QString language() const;
    Q_INVOKABLE bool setLanguage(const QString& language);
    // Presentation timestamps always use the analysis timezone, never the
    // machine timezone. QML passes the pattern explicitly; Qt does not expose
    // C++ default arguments to QML method calls.
    Q_INVOKABLE QString formatTimestamp(qint64 at, const QString& format) const;
    Q_INVOKABLE QString today() const;
    Q_SLOT void retranslateUi();
    bool backgroundBusy() const { return summaryRunning || backupRunning; }
    bool summaryBusy() const { return summaryRunning; }
    bool updateInProgress() const { return m_busy || summaryRunning || dailyRunning; }
    bool updateComplete() const { return updateSucceeded && summarySucceeded && !updateInProgress(); }
    QString backupStatus() const { return m_backupStatus; }
    Q_INVOKABLE bool scheduleRestore(const QString& backupPath);
    Q_INVOKABLE void restartForRestore();
    QVariantMap dailyAnalysis() const { return dailyResult.toVariantMap(); }
    Q_INVOKABLE void refreshDailyAnalysis();
    QVariantMap currentReading() const { return m_currentReading; }
    Q_INVOKABLE void refreshCurrentReading();
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    QVariantMap currentDayChart() const { return m_currentDayChart; }
    QVariantList labels() const { return db.mappings().toVariantList(); }
    QVariantMap observation() const { return m_analysis.value("observation").toMap(); }
    QVariantMap connection() const;
    QString storagePath() const { return root; }
    QString dataRevision() const { return db.revision(); }
    bool healthy() const { return db.available(); }
    QString databaseError() const { return db.error(); }
    static QString databasePath(const QString& root);
    QVariantMap analysis() const { return m_analysis; }
    QVariantList dayComparisonCharts() const { return m_dayComparisonCharts; }
    QVariantMap analysisSettings() const;
    Q_INVOKABLE void setAnalysisWindow(int days, const QString& lastDate);
    bool saveAnalysisSettings(const QVariantMap& settings);
    Q_INVOKABLE void syncAll();
    Q_INVOKABLE bool importFolder(const QString& path);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setWindow(const QString& date, int days);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void importGlucoid();
    Q_INVOKABLE void saveConnection(const QString& mode, const QString& address, const QString& auth, const QString& secret, const QString& serial);
    Q_INVOKABLE bool saveSourceConnection(const QString& source, const QString& mode, const QString& address, const QString& auth, const QString& secret,
                                          const QString& serial, const QString& wireguardInterface = QStringLiteral("wg0"));
    Q_INVOKABLE QVariantMap savedConnection(const QString& source) const;
    Q_INVOKABLE void refreshSourceChoice();
    Q_INVOKABLE bool mapLabel(const QString& label, const QString& kind, const QString& medicine);
    Q_INVOKABLE QString backup();
    QString backupTo(const QString& destination);
    Q_INVOKABLE QString chooseRestoreBackup();
    void seedDemo();
  signals:
    void changed();
    void currentReadingChanged();
    void syncFinished(bool ok);

  private:
    // Isolated read-only worker: no source configuration reads or network work.
    struct SnapshotReader {};
    Backend(const QString& root, const QString& databasePath, SnapshotReader);
    bool updateSucceeded = false, summarySucceeded = false;
    bool backgroundWork = false, summaryRunning = false, summaryPending = false;
    bool backupRunning = false, backupSucceeded = false;
    QString summaryRequestedKey, runningSummaryKey, completedSummaryKey, m_backupStatus;
    QFutureWatcher<QVariantMap> summaryWatcher, backupWatcher;
    QString summaryKey() const;
    void requestSummary();
    void launchSummary();
    static QVariantMap backupSnapshot(const QString& path, const QString& destination, const QString& source);
    QVariantMap m_currentReading;
    QFutureWatcher<QJsonObject> dailyWatcher;
    QJsonObject dailyResult;
    bool dailyEnabled = false, dailyRunning = false, dailyPending = false;
    QString dailyRequestedKey, runningDailyKey;
    QTimeZone pendingDailyZone;
    qint64 pendingDailyNow = 0;
    void requestDailyAnalysis();
    void launchDailyAnalysis();
    QString root, m_status;
    bool m_busy = false;
    QString dateWindow;
    int analysisDays = 7;
    QString analysisLastDate;
    Studio::Store db;
    Studio::Transport transport;
    Studio::Importer::Fetch jugglucoFetch;
    QVariantMap m_currentDayChart, m_analysis;
    QVariantList m_dayComparisonCharts;
    Studio::Connection config;
    std::unique_ptr<QLockFile> syncLock;
    std::unique_ptr<Studio::Importer> importer;
    std::unique_ptr<Studio::NightscoutImporter> nightscoutImporter;
    void startSync(bool full);
    void refreshAnalysis();
    QVariantList buildDayComparisonCharts(const QJsonObject& settings, const QTimeZone& zone, const QDate& today) const;
    bool writeConnection();
    QString secret(bool allowPrompt = false);
    bool writeSecret(const QString& value, const QString& key = {});
    bool reloadSource();
    void clearSourceResults();
    void finishSync(const QString& error);
};
