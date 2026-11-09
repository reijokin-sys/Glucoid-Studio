// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include "transport.h"
#include "analysis.h"
#include "importer.h"
#include "response.h"
#include "observation.h"
#include "automatic.h"
#include "dailyanalysis.h"
#include "nightscout.h"
#include <QFutureWatcher>
#include <QVariant>
#include <QLockFile>
#include <memory>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap dailyAnalysis READ dailyAnalysis NOTIFY changed)
    Q_PROPERTY(QVariantMap currentReading READ currentReading NOTIFY currentReadingChanged)
    Q_PROPERTY(QVariantMap automaticMeals READ automaticMeals NOTIFY changed)
    Q_PROPERTY(QVariantMap automaticSettings READ automaticSettings NOTIFY changed)
    Q_PROPERTY(bool automaticBusy READ automaticBusy NOTIFY changed)
    Q_PROPERTY(QVariantMap observationProfile READ observationProfile NOTIFY changed)
    Q_PROPERTY(QVariantMap observation READ observation NOTIFY changed)
    Q_PROPERTY(QVariantList artifactReviews READ artifactReviews NOTIFY changed)
    Q_PROPERTY(QVariantList responseCandidates READ responseCandidates NOTIFY changed)
    Q_PROPERTY(QVariantMap responsePolicy READ responsePolicy NOTIFY changed)
    Q_PROPERTY(QVariantMap response READ response NOTIFY changed)
    Q_PROPERTY(QVariantMap analysis READ analysis NOTIFY changed)
    Q_PROPERTY(QVariantMap comparisonChart READ comparisonChart NOTIFY changed)
    Q_PROPERTY(QVariantList dayComparisonCharts READ dayComparisonCharts NOTIFY changed)
    Q_PROPERTY(QVariantMap analysisSettings READ analysisSettings NOTIFY changed)
    Q_PROPERTY(QVariantList importHistory READ importHistory NOTIFY changed)
    Q_PROPERTY(QVariantList profileVersions READ profileVersions NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap chart READ chart NOTIFY changed)
    Q_PROPERTY(QVariantMap currentDayChart READ currentDayChart NOTIFY changed)
    Q_PROPERTY(QVariantList diary READ diary NOTIFY changed)
    Q_PROPERTY(QVariantList deletedDiary READ deletedDiary NOTIFY changed)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY changed)
    Q_PROPERTY(QVariantList summaries READ summaries NOTIFY changed)
    Q_PROPERTY(QVariantList episodes READ episodes NOTIFY changed)
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY changed)
    Q_PROPERTY(QVariantMap connection READ connection NOTIFY changed)
    Q_PROPERTY(QVariantList slots READ treatmentSlots NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString latestText READ latestText NOTIFY changed)
    Q_PROPERTY(QString dataRevision READ dataRevision NOTIFY changed)
public:
    explicit Backend(const QString &root,QObject *parent=nullptr,Studio::Importer::Fetch jugglucoFetch={});
    ~Backend()override;
    QVariantMap dailyAnalysis() const { return dailyResult.toVariantMap(); }
    Q_INVOKABLE void refreshDailyAnalysis();
    QVariantMap currentReading() const { return m_currentReading; }
    Q_INVOKABLE void refreshCurrentReading();
    QVariantMap automaticMeals()const{return automaticResult.toVariantMap();}
    QVariantMap automaticSettings()const;
    bool automaticBusy()const{return automaticRunning;}
    Q_INVOKABLE void startAutomaticAnalysis(bool refreshSource=false);
    Q_INVOKABLE void setAutomaticPeriod(int days);
    Q_INVOKABLE bool saveAutomaticTargets(double low,double high,double hypo);
    Q_INVOKABLE void openResearchView();
    QString status()const{return m_status;}
    bool busy()const{return m_busy;}
    QVariantMap chart()const{return m_chart;}
    QVariantMap currentDayChart()const{return m_currentDayChart;}
    QVariantList diary()const{return m_diary;}
    QVariantList deletedDiary()const{return m_deletedDiary;}
    QVariantList labels()const{return db.mappings().toVariantList();}
    QVariantList summaries()const{return m_summaries;}
    QVariantList episodes()const{return db.episodes().toVariantList();}
    QVariantMap profile()const;
    QVariantMap observationProfile()const{return db.observationProfile().toVariantMap();}
    QVariantMap observation()const{return m_analysis.value("observation").toMap();}
    Q_INVOKABLE QVariantMap activateObservationProfile(const QVariantMap &profile);
    QVariantList treatmentSlots()const;
    QVariantMap connection()const;
    QString storagePath()const{return root;}
    QString latestText()const{return m_latestText;}
    QString dataRevision()const{return db.revision();}
    bool healthy()const{return db.available();}
    QVariantList artifactReviews()const{return db.artifactReviews().toVariantList();}
    Q_INVOKABLE bool reviewGlucoseArtifact(const QString &from,const QString &to,const QString &reason,const QString &evidence,bool confirmed);
    Q_INVOKABLE bool removeGlucoseArtifact(const QString &id);
    QVariantList responseCandidates()const;
    QVariantMap responsePolicy()const;
    QVariantMap response()const{return responseResult.toVariantMap();}
    Q_INVOKABLE QVariantMap saveResponsePolicy(const QVariantMap &policy,bool approve);
    Q_INVOKABLE QVariantMap saveResponseEpisode(const QVariantMap &episode);
    Q_INVOKABLE void analyseResponse(int referenceDays);
    Q_INVOKABLE QVariantMap proposeResponse(int group);
    Q_INVOKABLE QVariantMap acceptResponse();
    QVariantMap analysis()const{return m_analysis;}
    QVariantMap comparisonChart()const{return m_comparisonChart;}
    QVariantList dayComparisonCharts()const{return m_dayComparisonCharts;}
    QVariantMap analysisSettings()const;
    QVariantList importHistory()const{return db.imports().toVariantList();}
    QVariantList profileVersions()const;
    Q_INVOKABLE void selectProfile(int version);
    Q_INVOKABLE void setAnalysisWindow(int days,const QString &lastDate);
    Q_INVOKABLE bool saveAnalysisSettings(const QVariantMap &settings);
    Q_INVOKABLE void syncAll();
    Q_INVOKABLE bool importFolder(const QString &path);
    Q_INVOKABLE void chooseImportFolder();
    Q_INVOKABLE bool excludeFromAnalysis(const QString &id,const QString &reason);
    Q_INVOKABLE bool confirmDay(const QString &date,bool complete);
    Q_INVOKABLE QVariantMap saveAnalysisProfile(const QVariantMap &profile,bool approve);
    Q_INVOKABLE QVariantMap saveResponseProfile(const QVariantMap &profile,bool approve);
    Q_INVOKABLE QString saveReport();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setWindow(const QString &date,int days);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void importGlucoid();
    Q_INVOKABLE void saveConnection(const QString &mode,const QString &address,const QString &auth,const QString &secret,const QString &serial);
    Q_INVOKABLE bool saveSourceConnection(const QString &source,const QString &mode,const QString &address,const QString &auth,const QString &secret,const QString &serial,const QString &wireguardInterface=QStringLiteral("wg0"));
    Q_INVOKABLE QVariantMap savedConnection(const QString &source) const;
    Q_INVOKABLE void refreshSourceChoice();
    Q_INVOKABLE bool mapLabel(const QString &label,const QString &kind,const QString &medicine);
    Q_INVOKABLE bool addLocal(const QString &date,double value,const QString &kind,const QString &medicine,const QString &purpose);
    Q_INVOKABLE bool annotate(const QString &id,const QString &purpose,const QString &note);
    Q_INVOKABLE bool removeAmount(const QString &id);
    Q_INVOKABLE bool restoreAmount(const QString &id);
    Q_INVOKABLE QVariantMap saveProfile(const QVariantMap &profile,bool approve);
    Q_INVOKABLE QVariantMap saveEpisode(const QVariantMap &episode);
    Q_INVOKABLE QVariantMap assess(const QString &kind,const QString &slot);
    Q_INVOKABLE QVariantMap acceptRecommendation();
    Q_INVOKABLE QVariantMap bolus(const QVariantMap &input);
    Q_INVOKABLE QString backup();
    Q_INVOKABLE void seedDemo();
signals:
    void changed();
    void currentReadingChanged();
    void syncFinished(bool ok);
private:
    QVariantMap m_currentReading;
    QFutureWatcher<QJsonObject> dailyWatcher;
    QJsonObject dailyResult;
    bool dailyEnabled=false,dailyRunning=false,dailyPending=false;
    QString dailyRequestedKey,runningDailyKey;
    QTimeZone pendingDailyZone;
    qint64 pendingDailyNow=0;
    void requestDailyAnalysis();
    void launchDailyAnalysis();
    QFutureWatcher<QJsonObject> automaticWatcher;
    QJsonObject automaticResult,pendingAutomaticSettings;
    bool automaticEnabled=false,automaticRunning=false,automaticPending=false,automaticSourceStarted=false;
    quint64 automaticGeneration=0,runningAutomaticGeneration=0;
    QString automaticRequestedKey,runningAutomaticKey;
    qint64 pendingAutomaticFrom=0,pendingAutomaticTo=0,pendingAutomaticUntil=0;
    void requestAutomaticAnalysis();
    void launchAutomaticAnalysis();
    QString root,m_status,m_latestText;
    bool m_busy=false;
    QString dateWindow;
    int dayWindow=1,analysisDays=7,selectedProfileVersion=0;
    QString analysisLastDate;
    Studio::Store db;
    Studio::Transport transport;
    Studio::Importer::Fetch jugglucoFetch;
    QVariantMap m_chart,m_currentDayChart,m_analysis,m_comparisonChart;
    QVariantList m_dayComparisonCharts;
    QVariantList m_diary,m_deletedDiary,m_summaries;
    Studio::Connection config;
    QJsonObject lastRecommendation,responseResult,responseDraft;
    QString responseRevision;
    QString recommendationRevision;
    std::unique_ptr<QLockFile> syncLock;
    std::unique_ptr<Studio::Importer> importer;
    std::unique_ptr<Studio::NightscoutImporter> nightscoutImporter;
    void startSync(bool full);
    void refreshAnalysis();
    QJsonObject evaluationProfile()const;
    QVariantMap saveHistoricalProfile(const QVariantMap &profile,bool approve,bool responseOnly);
    bool writeConnection();
    QString secret(bool allowPrompt=false);
    bool writeSecret(const QString &value,const QString &key={});
    bool reloadSource();
    void clearSourceResults();
    void finishSync(const QString &error);
};
