// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/unified_consent/unified_consent_metrics.h"

#include <utility>

#include "base/metrics/histogram_macros.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/sync/base/model_type.h"
#include "components/sync/driver/sync_user_settings.h"
#include "components/unified_consent/pref_names.h"

namespace unified_consent {
namespace metrics {

namespace {

// Sync data types that can be customized in settings.
// Used in histograms. Do not change existing values, append new values at the
// end.
enum class SyncDataType {
  kNone = 0,
  kApps = 1,
  kBookmarks = 2,
  kExtensions = 3,
  kHistory = 4,
  kSettings = 5,
  kThemes = 6,
  kTabs = 7,
  kPasswords = 8,
  kAutofill = 9,
  kPayments = 10,

  kMaxValue = kPayments
};

typedef std::pair<SyncDataType, syncer::ModelType> DT;

// Records a sample in the SyncAndGoogleServicesSettings histogram. Wrapped in a
// function to avoid code size issues caused by histogram macros.
void RecordSettingsHistogramSample(SettingsHistogramValue value) {
  UMA_HISTOGRAM_ENUMERATION("UnifiedConsent.SyncAndGoogleServicesSettings",
                            value);
}

// Checks if a pref is enabled and if so, records a sample in the
// SyncAndGoogleServicesSettings histogram. Returns true if a sample was
// recorded.
bool RecordSettingsHistogramFromPref(const char* pref_name,
                                     PrefService* pref_service,
                                     SettingsHistogramValue value) {
  if (!pref_service->GetBoolean(pref_name))
    return false;
  RecordSettingsHistogramSample(value);
  return true;
}

void RecordSyncDataTypeSample(SyncDataType data_type) {
  UMA_HISTOGRAM_ENUMERATION(
      "UnifiedConsent.SyncAndGoogleServicesSettings.AfterAdvancedOptIn."
      "SyncDataTypesOff",
      data_type);
}

}  // namespace

void RecordSettingsHistogram(PrefService* pref_service) {
  bool metric_recorded = RecordSettingsHistogramFromPref(
      prefs::kUrlKeyedAnonymizedDataCollectionEnabled, pref_service,
      metrics::SettingsHistogramValue::kUrlKeyedAnonymizedDataCollection);
  if (!metric_recorded)
    RecordSettingsHistogramSample(metrics::SettingsHistogramValue::kNone);
}

void RecordSyncSetupDataTypesHistrogam(syncer::SyncUserSettings* sync_settings,
                                       PrefService* pref_service) {
  bool metric_recorded = false;

  for (DT data_type : {DT(SyncDataType::kApps, syncer::APPS),
                       DT(SyncDataType::kBookmarks, syncer::BOOKMARKS),
                       DT(SyncDataType::kExtensions, syncer::EXTENSIONS),
                       DT(SyncDataType::kHistory, syncer::TYPED_URLS),
                       DT(SyncDataType::kSettings, syncer::PREFERENCES),
                       DT(SyncDataType::kThemes, syncer::THEMES),
                       DT(SyncDataType::kTabs, syncer::PROXY_TABS),
                       DT(SyncDataType::kPasswords, syncer::PASSWORDS),
                       DT(SyncDataType::kAutofill, syncer::AUTOFILL)}) {
    if (!sync_settings->GetChosenDataTypes().Has(data_type.second)) {
      RecordSyncDataTypeSample(data_type.first);
      metric_recorded = true;
    }
  }

  if (!autofill::prefs::IsPaymentsIntegrationEnabled(pref_service)) {
    RecordSyncDataTypeSample(SyncDataType::kPayments);
    metric_recorded = true;
  }

  if (!metric_recorded)
    RecordSyncDataTypeSample(SyncDataType::kNone);
}

}  // namespace metrics
}  // namespace unified_consent
