// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/identity/public/cpp/accounts_cookie_mutator.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/gtest_util.h"
#include "base/test/scoped_task_environment.h"
#include "components/signin/core/browser/list_accounts_test_utils.h"
#include "google_apis/gaia/gaia_auth_fetcher.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "services/identity/public/cpp/accounts_in_cookie_jar_info.h"
#include "services/identity/public/cpp/identity_manager.h"
#include "services/identity/public/cpp/identity_test_environment.h"
#include "services/identity/public/cpp/test_identity_manager_observer.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kTestUnavailableAccountId[] = "unavailable_account_id";
const char kTestOtherUnavailableAccountId[] = "other_unavailable_account_id";
const char kTestAccountEmail[] = "test_user@test.com";
const char kTestOtherAccountEmail[] = "test_other_user@test.com";
const char kTestAccountGaiaId[] = "gaia_id_for_test_user_test.com";
const char kTestAccessToken[] = "access_token";
const char kTestUberToken[] = "test_uber_token";
const char kTestOAuthMultiLoginResponse[] = R"(
    { "status": "OK",
      "cookies":[
        {
          "name":"CookieName",
          "value":"CookieValue",
          "domain":".google.com",
          "path":"/"
        }
      ]
    })";

enum class AccountsCookiesMutatorAction {
  kAddAccountToCookie,
  kSetAccountsInCookie,
  kTriggerCookieJarUpdateNoAccounts,
  kTriggerCookieJarUpdateOneAccount,
};

}  // namespace

namespace identity {
class AccountsCookieMutatorTest : public testing::Test {
 public:
  AccountsCookieMutatorTest() : identity_test_env_(&test_url_loader_factory_) {}

  ~AccountsCookieMutatorTest() override {}

  // Make an account available and returns the account ID.
  std::string AddAcountWithRefreshToken(const std::string& email) {
    return identity_test_env_.MakeAccountAvailable(email).account_id;
  }

  // Feed the TestURLLoaderFactory with the responses for the requests that will
  // be created by UberTokenFetcher when mergin accounts into the cookie jar.
  void PrepareURLLoaderResponsesForAction(AccountsCookiesMutatorAction action) {
    switch (action) {
      case AccountsCookiesMutatorAction::kAddAccountToCookie:
        test_url_loader_factory_.AddResponse(
            GaiaUrls::GetInstance()
                ->oauth1_login_url()
                .Resolve(base::StringPrintf("?source=%s&issueuberauth=1",
                                            GaiaConstants::kChromeSource))
                .spec(),
            kTestUberToken, net::HTTP_OK);

        test_url_loader_factory_.AddResponse(
            GaiaUrls::GetInstance()
                ->GetCheckConnectionInfoURLWithSource(
                    GaiaConstants::kChromeSource)
                .spec(),
            std::string(), net::HTTP_OK);

        test_url_loader_factory_.AddResponse(
            GaiaUrls::GetInstance()
                ->merge_session_url()
                .Resolve(base::StringPrintf(
                    "?uberauth=%s&continue=http://www.google.com&source=%s",
                    kTestUberToken, GaiaConstants::kChromeSource))
                .spec(),
            std::string(), net::HTTP_OK);
        break;
      case AccountsCookiesMutatorAction::kSetAccountsInCookie:
        test_url_loader_factory_.AddResponse(
            GaiaUrls::GetInstance()
                ->oauth_multilogin_url()
                .Resolve(base::StringPrintf("?source=%s",
                                            GaiaConstants::kChromeSource))
                .spec(),
            std::string(kTestOAuthMultiLoginResponse), net::HTTP_OK);
        break;
      case AccountsCookiesMutatorAction::kTriggerCookieJarUpdateNoAccounts:
        signin::SetListAccountsResponseNoAccounts(&test_url_loader_factory_);
        break;
      case AccountsCookiesMutatorAction::kTriggerCookieJarUpdateOneAccount:
        signin::SetListAccountsResponseOneAccount(
            kTestAccountEmail, kTestAccountGaiaId, &test_url_loader_factory_);
        break;
    }
  }

  identity::IdentityTestEnvironment* identity_test_env() {
    return &identity_test_env_;
  }

  TestIdentityManagerObserver* identity_manager_observer() {
    return identity_test_env_.identity_manager_observer();
  }

  AccountsCookieMutator* accounts_cookie_mutator() {
    return identity_test_env_.identity_manager()->GetAccountsCookieMutator();
  }

 private:
  base::test::ScopedTaskEnvironment task_environment_;
  network::TestURLLoaderFactory test_url_loader_factory_;
  identity::IdentityTestEnvironment identity_test_env_;

  DISALLOW_COPY_AND_ASSIGN(AccountsCookieMutatorTest);
};

// Test that adding a non existing account without providing an access token
// results in an error due to such account not being available.
TEST_F(AccountsCookieMutatorTest, AddAccountToCookie_NonExistingAccount) {
  base::RunLoop run_loop;
  identity_manager_observer()->SetOnAddAccountToCookieCompletedCallback(
      run_loop.QuitClosure());
  accounts_cookie_mutator()->AddAccountToCookie(kTestUnavailableAccountId,
                                                gaia::GaiaSource::kChrome);
  run_loop.Run();

  EXPECT_EQ(identity_manager_observer()
                ->AccountFromAddAccountToCookieCompletedCallback(),
            kTestUnavailableAccountId);
  EXPECT_EQ(identity_manager_observer()
                ->ErrorFromAddAccountToCookieCompletedCallback()
                .state(),
            GoogleServiceAuthError::USER_NOT_SIGNED_UP);
}

// Test that adding an already available account without providing an access
// token results in such account being successfully merged into the cookie jar.
TEST_F(AccountsCookieMutatorTest, AddAccountToCookie_ExistingAccount) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kAddAccountToCookie);

  std::string account_id = AddAcountWithRefreshToken(kTestAccountEmail);
  base::RunLoop run_loop;
  identity_manager_observer()->SetOnAddAccountToCookieCompletedCallback(
      run_loop.QuitClosure());
  accounts_cookie_mutator()->AddAccountToCookie(account_id,
                                                gaia::GaiaSource::kChrome);
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      account_id, kTestAccessToken,
      base::Time::Now() + base::TimeDelta::FromHours(1));
  run_loop.Run();

  EXPECT_EQ(identity_manager_observer()
                ->AccountFromAddAccountToCookieCompletedCallback(),
            account_id);
  EXPECT_EQ(identity_manager_observer()
                ->ErrorFromAddAccountToCookieCompletedCallback()
                .state(),
            GoogleServiceAuthError::NONE);
}

// Test that adding a non existing account along with an access token, results
// on such account being successfully merged into the cookie jar.
TEST_F(AccountsCookieMutatorTest,
       AddAccountToCookieWithAccessToken_NonExistingAccount) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kAddAccountToCookie);

  base::RunLoop run_loop;
  identity_manager_observer()->SetOnAddAccountToCookieCompletedCallback(
      run_loop.QuitClosure());
  accounts_cookie_mutator()->AddAccountToCookieWithToken(
      kTestUnavailableAccountId, kTestAccessToken, gaia::GaiaSource::kChrome);
  run_loop.Run();

  EXPECT_EQ(identity_manager_observer()
                ->AccountFromAddAccountToCookieCompletedCallback(),
            kTestUnavailableAccountId);
  EXPECT_EQ(identity_manager_observer()
                ->ErrorFromAddAccountToCookieCompletedCallback()
                .state(),
            GoogleServiceAuthError::NONE);
}

// Test that adding an already available account along with an access token,
// results in such account being successfully merged into the cookie jar.
TEST_F(AccountsCookieMutatorTest,
       AddAccountToCookieWithAccessToken_ExistingAccount) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kAddAccountToCookie);

  std::string account_id = AddAcountWithRefreshToken(kTestAccountEmail);
  base::RunLoop run_loop;
  identity_manager_observer()->SetOnAddAccountToCookieCompletedCallback(
      run_loop.QuitClosure());
  accounts_cookie_mutator()->AddAccountToCookieWithToken(
      account_id, kTestAccessToken, gaia::GaiaSource::kChrome);

  run_loop.Run();

  EXPECT_EQ(identity_manager_observer()
                ->AccountFromAddAccountToCookieCompletedCallback(),
            account_id);
  EXPECT_EQ(identity_manager_observer()
                ->ErrorFromAddAccountToCookieCompletedCallback()
                .state(),
            GoogleServiceAuthError::NONE);
}

// Test that trying to set a list of accounts in the cookie jar where none of
// those accounts have refresh tokens in IdentityManager results in an error.
TEST_F(AccountsCookieMutatorTest, SetAccountsInCookie_AllNonExistingAccounts) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kSetAccountsInCookie);

  base::RunLoop run_loop;
  std::vector<std::string> accounts_ids = {kTestUnavailableAccountId,
                                           kTestOtherUnavailableAccountId};
  accounts_cookie_mutator()->SetAccountsInCookie(
      accounts_ids, gaia::GaiaSource::kChrome,
      base::BindOnce(
          [](base::OnceClosure quit_closure,
             const GoogleServiceAuthError& error) {
            EXPECT_EQ(error.state(),
                      GoogleServiceAuthError::USER_NOT_SIGNED_UP);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));

  run_loop.Run();
}

// Test that trying to set a list of accounts in the cookie jar where some of
// those accounts have no refresh tokens in IdentityManager results in an error.
TEST_F(AccountsCookieMutatorTest, SetAccountsInCookie_SomeNonExistingAccounts) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kSetAccountsInCookie);

  std::string account_id = AddAcountWithRefreshToken(kTestAccountEmail);
  base::RunLoop run_loop;
  std::vector<std::string> accounts_ids = {account_id,
                                           kTestUnavailableAccountId};
  accounts_cookie_mutator()->SetAccountsInCookie(
      accounts_ids, gaia::GaiaSource::kChrome,
      base::BindOnce(
          [](base::OnceClosure quit_closure,
             const GoogleServiceAuthError& error) {
            EXPECT_EQ(error.state(),
                      GoogleServiceAuthError::USER_NOT_SIGNED_UP);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

// Test that trying to set a list of accounts in the cookie jar where all of
// those accounts have refresh tokens in IdentityManager results in them being
// successfully set.
TEST_F(AccountsCookieMutatorTest, SetAccountsInCookie_AllExistingAccounts) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kSetAccountsInCookie);

  std::string account_id = AddAcountWithRefreshToken(kTestAccountEmail);
  std::string other_account_id =
      AddAcountWithRefreshToken(kTestOtherAccountEmail);
  base::RunLoop run_loop;
  std::vector<std::string> accounts_ids = {account_id, other_account_id};
  accounts_cookie_mutator()->SetAccountsInCookie(
      accounts_ids, gaia::GaiaSource::kChrome,
      base::BindOnce(
          [](base::OnceClosure quit_closure,
             const GoogleServiceAuthError& error) {
            EXPECT_EQ(error.state(), GoogleServiceAuthError::NONE);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));

  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      account_id, kTestAccessToken,
      base::Time::Now() + base::TimeDelta::FromHours(1));
  identity_test_env()->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
      other_account_id, kTestAccessToken,
      base::Time::Now() + base::TimeDelta::FromHours(1));

  run_loop.Run();
}

// Test triggering the update of a cookie jar with no accounts works.
TEST_F(AccountsCookieMutatorTest, TriggerCookieJarUpdate_NoListedAccounts) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kTriggerCookieJarUpdateNoAccounts);

  base::RunLoop run_loop;
  identity_manager_observer()->SetOnAccountsInCookieUpdatedCallback(
      run_loop.QuitClosure());
  accounts_cookie_mutator()->TriggerCookieJarUpdate();
  run_loop.Run();

  const AccountsInCookieJarInfo& accounts_in_jar_info =
      identity_manager_observer()
          ->AccountsInfoFromAccountsInCookieUpdatedCallback();
  EXPECT_EQ(accounts_in_jar_info.signed_in_accounts.size(), 0U);
  EXPECT_EQ(accounts_in_jar_info.signed_out_accounts.size(), 0U);
  EXPECT_TRUE(accounts_in_jar_info.accounts_are_fresh);

  EXPECT_EQ(identity_manager_observer()
                ->ErrorFromAccountsInCookieUpdatedCallback()
                .state(),
            GoogleServiceAuthError::NONE);
}

// Test triggering the update of a cookie jar with one accounts works and that
// the received accounts match the data injected via the TestURLLoaderFactory.
TEST_F(AccountsCookieMutatorTest, TriggerCookieJarUpdate_OneListedAccounts) {
  PrepareURLLoaderResponsesForAction(
      AccountsCookiesMutatorAction::kTriggerCookieJarUpdateOneAccount);

  base::RunLoop run_loop;
  identity_manager_observer()->SetOnAccountsInCookieUpdatedCallback(
      run_loop.QuitClosure());
  accounts_cookie_mutator()->TriggerCookieJarUpdate();
  run_loop.Run();

  const AccountsInCookieJarInfo& accounts_in_jar_info =
      identity_manager_observer()
          ->AccountsInfoFromAccountsInCookieUpdatedCallback();
  EXPECT_EQ(accounts_in_jar_info.signed_in_accounts.size(), 1U);
  EXPECT_EQ(accounts_in_jar_info.signed_in_accounts[0].gaia_id,
            kTestAccountGaiaId);
  EXPECT_EQ(accounts_in_jar_info.signed_in_accounts[0].email,
            kTestAccountEmail);

  EXPECT_EQ(accounts_in_jar_info.signed_out_accounts.size(), 0U);
  EXPECT_TRUE(accounts_in_jar_info.accounts_are_fresh);

  EXPECT_EQ(identity_manager_observer()
                ->ErrorFromAccountsInCookieUpdatedCallback()
                .state(),
            GoogleServiceAuthError::NONE);
}

}  // namespace identity
