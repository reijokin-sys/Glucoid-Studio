// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "importer.h"
#include <QDateTime>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
namespace Studio {
// A transient failure (slow phone export, dropped connection, timeout) is
// retried with the same window so one bad response does not end the import.
static constexpr int retries = 2;
void Importer::enqueueGlucose(const QStringList& kinds, qint64 from, qint64 until) {
    constexpr qint64 maximum = 7LL * 86400000, minimumTail = 3600000;
    for (const auto& kind : kinds) {
        for (qint64 at = from; at < until;) {
            const auto remaining = until - at;
            // Balance the last two pieces instead of leaving a seconds-long
            // history request that Juggluco may reject with HTTP 500.
            const qint64 end = remaining > maximum && remaining < maximum + minimumTail ? at + (remaining / 2000) * 1000 : std::min(until, at + maximum);
            tasks << Task{kind, at, end};
            at = end;
        }
    }
}
void Importer::start(qint64 from, qint64 to, const QString& transport) {
    from = (from / 1000) * 1000; // Reconcile precisely the lower boundary requested from the seconds API.
    this->to = to;
    discovery = from == 0;
    expanded = false;
    completed = 0;
    tasks.clear();
    run = db.beginImport(from, to, transport);
    if (run.isEmpty()) {
        emit finished(false, QCoreApplication::translate("Studio", "Tuontierän aloittaminen epäonnistui."));
        return;
    }
    if (discovery) {
        tasks << Task{"history", 1000, to} << Task{"amounts", 1000, to};
    } else {
        enqueueGlucose({"stream", "history", "scans"}, from, to);
        tasks << Task{"amounts", from, to};
        expanded = true;
    }
    next();
}
void Importer::next() {
    if (tasks.isEmpty() && discovery && !expanded) {
        auto oldest = db.query("SELECT min(time) AS t FROM (SELECT time FROM glucose UNION ALL SELECT time FROM amounts WHERE local=0)");
        qint64 from = oldest.isEmpty() ? to - 90LL * 86400000 : qint64(oldest[0].toObject()["t"].toDouble(to - 90LL * 86400000)) - 7LL * 86400000;
        from = (std::max(1000LL, from) / 1000) * 1000;
        enqueueGlucose({"stream", "scans"}, from, to);
        db.run("UPDATE import_runs SET from_time=? WHERE id=?", {from, run});
        expanded = true;
    }
    if (tasks.isEmpty()) {
        finish(
            true,
            QString(QCoreApplication::translate("Studio", "Tuonti valmis: %1 varmennettua osaa. Historia on käytettävissä ilman puhelinta.")).arg(completed));
        return;
    }
    const Task task = tasks.takeFirst();
    emit progress(
        QString(QCoreApplication::translate("Studio", "Tuodaan %1 · valmis %2 osaa · jonossa %3")).arg(task.kind).arg(completed).arg(tasks.size() + 1));
    // API endtime is inclusive seconds; the store uses a half-open interval.
    const QString path = QString("/x/%1?header&starttime=%2&endtime=%3&mmol/L").arg(task.kind).arg(task.from / 1000).arg((task.to - 1) / 1000);
    fetch(path, [this, task](const QByteArray& bytes, const QString& error) {
        if (error == "HTTP 413" && task.to - task.from > 3600000) {
            const qint64 mid = ((task.from + task.to) / 2000) * 1000;
            tasks.prepend(Task{task.kind, mid, task.to});
            tasks.prepend(Task{task.kind, task.from, mid});
            QTimer::singleShot(0, this, &Importer::next);
            return;
        }
        if (!error.isEmpty()) {
            if (task.attempt < retries) {
                tasks.prepend(Task{task.kind, task.from, task.to, task.attempt + 1});
                emit progress(QString(QCoreApplication::translate("Studio", "Tuodaan %1 · uudelleenyritys %2/%3 · jonossa %4"))
                                  .arg(task.kind)
                                  .arg(task.attempt + 1)
                                  .arg(retries + 1)
                                  .arg(tasks.size()));
                QTimer::singleShot(1000 * (1 << task.attempt), this, &Importer::next);
                return;
            }
            finish(false,
                   QCoreApplication::translate("Studio", "Tuonti jäi osittaiseksi uudelleenyritysten jälkeen. Valmiit osat ja aiempi historia säilyivät. ") +
                       task.kind + ": " + error);
            return;
        }
        storePage(task, bytes);
    });
}
void Importer::storePage(const Task& task, const QByteArray& bytes) {
    if (!backgroundWrites) {
        const bool ok = db.importChunk(run, task.kind, bytes, task.from, task.to);
        pageStored(task, ok, ok ? QString() : db.error());
        return;
    }
    const auto path = db.path(), runId = run;
    writeWatcher.disconnect(this);
    connect(&writeWatcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this, task] {
        const auto result = writeWatcher.result();
        pageStored(task, result.first, result.second);
    });
    writeWatcher.setFuture(QtConcurrent::run([path, runId, task, bytes] {
        Store store(path);
        const bool ok = store.good() && store.importChunk(runId, task.kind, bytes, task.from, task.to);
        return qMakePair(ok, ok ? QString() : store.error());
    }));
}
void Importer::pageStored(const Task& task, bool ok, const QString& error) {
    if (!ok) {
        finish(false, error);
        return;
    }
    ++completed;
    db.setMeta("lastPieceTo:" + task.kind, QString::number(task.to));
    QTimer::singleShot(0, this, &Importer::next);
}
void Importer::finish(bool ok, const QString& why) {
    QString message = why;
    if (!db.endImport(run, ok, why)) {
        ok = false;
        message = db.error();
    }
    if (ok && !db.setMeta("lastSourceUntil", QString::number(to))) {
        ok = false;
        message = db.error();
    }
    emit finished(ok, message);
}
} // namespace Studio
