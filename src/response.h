// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "domain.h"
namespace Studio {
QJsonObject defaultResponsePolicy();
QStringList validateResponsePolicy(const QJsonObject& policy);
bool responseProfileApproved(const QJsonObject& profile);
int responsePeriod(const QJsonObject& policy, qint64 time, const QString& zone);
QJsonObject responseEstimate(const QJsonObject& profile, const QJsonObject& policy, const QJsonArray& episodes, qint64 from, qint64 to, bool unsafe,
                             qint64 availableUntil = 0);
QJsonObject responseComparison(const QJsonObject& recent, const QJsonObject& reference, const QJsonObject& policy);
QJsonObject responseProposal(const QJsonObject& profile, const QJsonObject& policy, const QJsonObject& estimate, int group, qint64 now = 0);
QJsonObject applyResponseProposal(const QJsonObject& profile, const QJsonObject& policy, const QJsonObject& proposal, qint64 now);
} // namespace Studio
