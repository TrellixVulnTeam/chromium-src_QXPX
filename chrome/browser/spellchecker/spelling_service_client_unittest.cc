// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/spellcheck/browser/spelling_service_client.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/json/json_reader.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind_test_util.h"
#include "base/values.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/spellcheck/browser/pref_names.h"
#include "components/spellcheck/common/spellcheck_result.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/base/load_flags.h"
#include "net/http/http_util.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kSpellingServiceURL[] = "https://www.googleapis.com/rpc";

// A class derived from the SpellingServiceClient class used by the
// SpellingServiceClientTest class. This class sets the URLLoaderFactory so
// tests can control requests and responses.
class TestingSpellingServiceClient : public SpellingServiceClient {
 public:
  TestingSpellingServiceClient()
      : success_(false),
        test_shared_loader_factory_(
            base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
                &test_url_loader_factory_)) {
    SetURLLoaderFactoryForTesting(test_shared_loader_factory_);
  }
  ~TestingSpellingServiceClient() {}

  void SetExpectedTextCheckResult(bool success,
                                  const std::string& sanitized_request_text,
                                  const char* text) {
    success_ = success;
    sanitized_request_text_ = sanitized_request_text;
    corrected_text_.assign(base::UTF8ToUTF16(text));
  }

  void VerifyResponse(bool success,
                      const base::string16& request_text,
                      const std::vector<SpellCheckResult>& results) {
    EXPECT_EQ(success_, success);
    base::string16 text(base::UTF8ToUTF16(sanitized_request_text_));
    for (auto it = results.begin(); it != results.end(); ++it) {
      text.replace(it->location, it->length, it->replacements[0]);
    }
    EXPECT_EQ(corrected_text_, text);
  }

  bool ParseResponseSuccess(const std::string& data) {
    std::vector<SpellCheckResult> results;
    return ParseResponse(data, &results);
  }

  network::TestURLLoaderFactory* test_url_loader_factory() {
    return &test_url_loader_factory_;
  }

 private:
  bool success_;
  std::string sanitized_request_text_;
  base::string16 corrected_text_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> test_shared_loader_factory_;
};

// A test class used for testing the SpellingServiceClient class. This class
// implements a callback function used by the SpellingServiceClient class to
// monitor the class calls the callback with expected results.
class SpellingServiceClientTest : public testing::Test {
 public:
  void OnTextCheckComplete(int tag,
                           bool success,
                           const base::string16& text,
                           const std::vector<SpellCheckResult>& results) {
    client_.VerifyResponse(success, text, results);
  }

 protected:
  bool GetExpectedCountry(const std::string& language, std::string* country) {
    static const struct {
      const char* language;
      const char* country;
    } kCountries[] = {
        {"af", "ZAF"}, {"en", "USA"},
    };
    for (size_t i = 0; i < base::size(kCountries); ++i) {
      if (!language.compare(kCountries[i].language)) {
        country->assign(kCountries[i].country);
        return true;
      }
    }
    return false;
  }

  content::TestBrowserThreadBundle thread_bundle_;
  TestingSpellingServiceClient client_;
  TestingProfile profile_;
};

}  // namespace

// Verifies that SpellingServiceClient::RequestTextCheck() creates a JSON
// request sent to the Spelling service as we expect. This test also verifies
// that it parses a JSON response from the service and calls the callback
// function. To avoid sending JSON-RPC requests to the service, this test uses a
// subclass of SpellingServiceClient that in turn sets the client's URL loader
// factory to a TestURLLoaderFactory. The client thinks it is issuing real
// network requests, but in fact the responses are entirely under our control
// and no network activity takes place.
// This test also uses a custom callback function that replaces all
// misspelled words with ones suggested by the service so this test can compare
// the corrected text with the expected results. (If there are not any
// misspelled words, |corrected_text| should be equal to |request_text|.)
TEST_F(SpellingServiceClientTest, RequestTextCheck) {
  static const struct {
    const wchar_t* request_text;
    std::string sanitized_request_text;
    SpellingServiceClient::ServiceType request_type;
    net::HttpStatusCode response_status;
    std::string response_data;
    bool success;
    const char* corrected_text;
    std::string language;
  } kTests[] = {
      {
          L"", "", SpellingServiceClient::SUGGEST, net::HttpStatusCode(500), "",
          false, "", "af",
      },
      {
          L"chromebook", "chromebook", SpellingServiceClient::SUGGEST,
          net::HttpStatusCode(200), "{}", true, "chromebook", "af",
      },
      {
          L"chrombook", "chrombook", SpellingServiceClient::SUGGEST,
          net::HttpStatusCode(200),
          "{\n"
          "  \"result\": {\n"
          "    \"spellingCheckResponse\": {\n"
          "      \"misspellings\": [{\n"
          "        \"charStart\": 0,\n"
          "        \"charLength\": 9,\n"
          "        \"suggestions\": [{ \"suggestion\": \"chromebook\" }],\n"
          "        \"canAutoCorrect\": false\n"
          "      }]\n"
          "    }\n"
          "  }\n"
          "}",
          true, "chromebook", "af",
      },
      {
          L"", "", SpellingServiceClient::SPELLCHECK, net::HttpStatusCode(500),
          "", false, "", "en",
      },
      {
          L"I have been to USA.", "I have been to USA.",
          SpellingServiceClient::SPELLCHECK, net::HttpStatusCode(200), "{}",
          true, "I have been to USA.", "en",
      },
      {
          L"I have bean to USA.", "I have bean to USA.",
          SpellingServiceClient::SPELLCHECK, net::HttpStatusCode(200),
          "{\n"
          "  \"result\": {\n"
          "    \"spellingCheckResponse\": {\n"
          "      \"misspellings\": [{\n"
          "        \"charStart\": 7,\n"
          "        \"charLength\": 4,\n"
          "        \"suggestions\": [{ \"suggestion\": \"been\" }],\n"
          "        \"canAutoCorrect\": false\n"
          "      }]\n"
          "    }\n"
          "  }\n"
          "}",
          true, "I have been to USA.", "en",
      },
      {
          L"I\x2019mattheIn'n'Out.", "I'mattheIn'n'Out.",
          SpellingServiceClient::SPELLCHECK, net::HttpStatusCode(200),
          "{\n"
          "  \"result\": {\n"
          "    \"spellingCheckResponse\": {\n"
          "      \"misspellings\": [{\n"
          "        \"charStart\": 0,\n"
          "        \"charLength\": 16,\n"
          "        \"suggestions\":"
          " [{ \"suggestion\": \"I'm at the In'N'Out\" }],\n"
          "        \"canAutoCorrect\": false\n"
          "      }]\n"
          "    }\n"
          "  }\n"
          "}",
          true, "I'm at the In'N'Out.", "en",
      },
  };

  PrefService* pref = profile_.GetPrefs();
  pref->SetBoolean(spellcheck::prefs::kSpellCheckEnable, true);
  pref->SetBoolean(spellcheck::prefs::kSpellCheckUseSpellingService, true);

  for (size_t i = 0; i < base::size(kTests); ++i) {
    client_.test_url_loader_factory()->ClearResponses();
    net::HttpStatusCode http_status = kTests[i].response_status;
    network::ResourceResponseHead head;
    std::string headers(base::StringPrintf(
        "HTTP/1.1 %d %s\nContent-type: application/json\n\n",
        static_cast<int>(http_status), net::GetHttpReasonPhrase(http_status)));
    head.headers = new net::HttpResponseHeaders(
        net::HttpUtil::AssembleRawHeaders(headers.c_str(), headers.size()));
    head.mime_type = "application/json";
    network::URLLoaderCompletionStatus status;
    status.decoded_body_length = kTests[i].response_data.size();
    client_.test_url_loader_factory()->AddResponse(
        GURL(kSpellingServiceURL), head, kTests[i].response_data, status);
    net::HttpRequestHeaders intercepted_headers;
    std::string intercepted_body;
    client_.test_url_loader_factory()->SetInterceptor(
        base::BindLambdaForTesting(
            [&](const network::ResourceRequest& request) {
              intercepted_headers = request.headers;
              intercepted_body = network::GetUploadData(request);
            }));
    client_.SetExpectedTextCheckResult(kTests[i].success,
                                       kTests[i].sanitized_request_text,
                                       kTests[i].corrected_text);
    base::ListValue dictionary;
    dictionary.AppendString(kTests[i].language);
    pref->Set(spellcheck::prefs::kSpellCheckDictionaries, dictionary);

    client_.RequestTextCheck(
        &profile_, kTests[i].request_type,
        base::WideToUTF16(kTests[i].request_text),
        base::BindOnce(&SpellingServiceClientTest::OnTextCheckComplete,
                       base::Unretained(this), 0));
    thread_bundle_.RunUntilIdle();

    // Verify the request content type was JSON. (The Spelling service returns
    // an internal server error when this content type is not JSON.)
    std::string request_content_type;
    ASSERT_TRUE(intercepted_headers.GetHeader(
        net::HttpRequestHeaders::kContentType, &request_content_type));
    EXPECT_EQ("application/json", request_content_type);

    // Parse the JSON sent to the service, and verify its parameters.
    std::unique_ptr<base::DictionaryValue> value(
        static_cast<base::DictionaryValue*>(
            base::JSONReader::ReadDeprecated(intercepted_body,
                                             base::JSON_ALLOW_TRAILING_COMMAS)
                .release()));
    ASSERT_TRUE(value.get());
    std::string method;
    EXPECT_TRUE(value->GetString("method", &method));
    EXPECT_EQ("spelling.check", method);
    std::string version;
    EXPECT_TRUE(value->GetString("apiVersion", &version));
    EXPECT_EQ(base::StringPrintf("v%d", kTests[i].request_type), version);
    std::string sanitized_text;
    EXPECT_TRUE(value->GetString("params.text", &sanitized_text));
    EXPECT_EQ(kTests[i].sanitized_request_text, sanitized_text);
    std::string language;
    EXPECT_TRUE(value->GetString("params.language", &language));
    std::string expected_language =
        kTests[i].language.empty() ? std::string("en") : kTests[i].language;
    EXPECT_EQ(expected_language, language);
    std::string expected_country;
    ASSERT_TRUE(GetExpectedCountry(language, &expected_country));
    std::string country;
    EXPECT_TRUE(value->GetString("params.originCountry", &country));
    EXPECT_EQ(expected_country, country);
  }
}

// Verify that SpellingServiceClient::IsAvailable() returns true only when it
// can send suggest requests or spellcheck requests.
TEST_F(SpellingServiceClientTest, AvailableServices) {
  const SpellingServiceClient::ServiceType kSuggest =
      SpellingServiceClient::SUGGEST;
  const SpellingServiceClient::ServiceType kSpellcheck =
      SpellingServiceClient::SPELLCHECK;

  // When a user disables spellchecking or prevent using the Spelling service,
  // this function should return false both for suggestions and for spellcheck.
  PrefService* pref = profile_.GetPrefs();
  pref->SetBoolean(spellcheck::prefs::kSpellCheckEnable, false);
  pref->SetBoolean(spellcheck::prefs::kSpellCheckUseSpellingService, false);
  EXPECT_FALSE(client_.IsAvailable(&profile_, kSuggest));
  EXPECT_FALSE(client_.IsAvailable(&profile_, kSpellcheck));

  pref->SetBoolean(spellcheck::prefs::kSpellCheckEnable, true);
  pref->SetBoolean(spellcheck::prefs::kSpellCheckUseSpellingService, true);

  // For locales supported by the SpellCheck service, this function returns
  // false for suggestions and true for spellcheck. (The comment in
  // SpellingServiceClient::IsAvailable() describes why this function returns
  // false for suggestions.) If there is no language set, then we
  // do not allow any remote.
  pref->Set(spellcheck::prefs::kSpellCheckDictionaries, base::ListValue());

  EXPECT_FALSE(client_.IsAvailable(&profile_, kSuggest));
  EXPECT_FALSE(client_.IsAvailable(&profile_, kSpellcheck));

  static const char* kSupported[] = {
      "en-AU", "en-CA", "en-GB", "en-US", "da-DK", "es-ES",
  };
  // If spellcheck is allowed, then suggest is not since spellcheck is a
  // superset of suggest.
  for (size_t i = 0; i < base::size(kSupported); ++i) {
    base::ListValue dictionary;
    dictionary.AppendString(kSupported[i]);
    pref->Set(spellcheck::prefs::kSpellCheckDictionaries, dictionary);

    EXPECT_FALSE(client_.IsAvailable(&profile_, kSuggest));
    EXPECT_TRUE(client_.IsAvailable(&profile_, kSpellcheck));
  }

  // This function returns true for suggestions for all and false for
  // spellcheck for unsupported locales.
  static const char* kUnsupported[] = {
      "af-ZA", "bg-BG", "ca-ES", "cs-CZ", "de-DE", "el-GR", "et-EE", "fo-FO",
      "fr-FR", "he-IL", "hi-IN", "hr-HR", "hu-HU", "id-ID", "it-IT", "lt-LT",
      "lv-LV", "nb-NO", "nl-NL", "pl-PL", "pt-BR", "pt-PT", "ro-RO", "ru-RU",
      "sk-SK", "sl-SI", "sh",    "sr",    "sv-SE", "tr-TR", "uk-UA", "vi-VN",
  };
  for (size_t i = 0; i < base::size(kUnsupported); ++i) {
    SCOPED_TRACE(std::string("Expected language ") + kUnsupported[i]);
    base::ListValue dictionary;
    dictionary.AppendString(kUnsupported[i]);
    pref->Set(spellcheck::prefs::kSpellCheckDictionaries, dictionary);

    EXPECT_TRUE(client_.IsAvailable(&profile_, kSuggest));
    EXPECT_FALSE(client_.IsAvailable(&profile_, kSpellcheck));
  }
}

// Verify that an error in JSON response from spelling service will result in
// ParseResponse returning false.
TEST_F(SpellingServiceClientTest, ResponseErrorTest) {
  EXPECT_TRUE(client_.ParseResponseSuccess("{\"result\": {}}"));
  EXPECT_FALSE(client_.ParseResponseSuccess("{\"error\": {}}"));
}