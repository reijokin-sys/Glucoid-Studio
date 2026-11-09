// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace Studio {
// Exploratory inverse model, not an identified causal effect or dose prescription.
// The theoretical target is explicit and independent of the treatment target range.
constexpr double theoreticalTargetMmol = 5.0;
// The fitted outcome is the return to the starting level at the end of the standard
// four-hour window: endpointGlucose - startGlucose, where a rise in between is allowed.
// totalWindow=false uses the meal's own bolus; true uses all rapid insulin recorded
// from the meal up to the endpoint (meal dose plus later corrections).
// A negative group pools every clock group (the fallback when one group is too thin).
// sugarLoad=true replaces the separate carbs and start-glucose terms with one load
// S = carbs + sugarLoadGramsPerMmol * (start - target); below target counts negative.
constexpr double sugarLoadGramsPerMmol = 5.0;  // 1 U per 10 g and 1 U per 2 mmol/l
QJsonObject theoreticalMealDose(const QJsonArray &meals, int group, const QString &cohort,
                               double target, bool totalWindow = false,
                               int minimumMeals = 24, int minimumDays = 12,
                               bool sugarLoad = false);
}
