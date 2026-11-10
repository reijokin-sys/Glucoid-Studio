// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtGlobal>
namespace Studio {
inline constexpr qint64 initialImportHistoryMs = 183LL * 86400000;
// Always revisit at least 72 hours, including late/backdated user entries.
inline constexpr qint64 correctionHistoryMs = 3LL * 86400000;
} // namespace Studio
