// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "store.h"
#include <QObject>
#include <functional>
namespace Studio {
class Importer:public QObject {
    Q_OBJECT
public:
    using Fetch=std::function<void(const QString&,std::function<void(QByteArray,QString)>)>;
    Importer(Store &store,Fetch fetch,QObject*parent=nullptr):QObject(parent),db(store),fetch(std::move(fetch)){}
    void start(qint64 from,qint64 to,const QString &transport);
signals:
    void progress(const QString &text);
    void finished(bool success,const QString &message);
private:
    struct Task{QString kind;qint64 from,to;int attempt=0;};
    Store &db;Fetch fetch;QList<Task> tasks;QString run; qint64 to=0;bool discovery=false,expanded=false;int completed=0;
    void enqueueGlucose(const QStringList &kinds,qint64 from,qint64 until);
    void next();void finish(bool ok,const QString&why);
};
}
