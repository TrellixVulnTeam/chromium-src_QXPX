// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_TAB_HELPER_H_
#define CHROME_BROWSER_EXTENSIONS_TAB_HELPER_H_

#include <memory>
#include <set>
#include <string>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observer.h"
#include "chrome/browser/extensions/active_tab_permission_granter.h"
#include "chrome/common/chrome_render_frame.mojom.h"
#include "chrome/common/extensions/webstore_install_result.h"
#include "chrome/common/web_application_info.h"
#include "content/public/browser/web_contents_binding_set.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "extensions/browser/extension_function_dispatcher.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/browser/script_executor.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/stack_frame.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace content {
class RenderFrameHost;
}

namespace gfx {
class Image;
}

namespace extensions {
class ExtensionActionRunner;
class BookmarkAppHelper;
class Extension;

// Per-tab extension helper. Also handles non-extension apps.
class TabHelper : public content::WebContentsObserver,
                  public ExtensionFunctionDispatcher::Delegate,
                  public ExtensionRegistryObserver,
                  public content::WebContentsUserData<TabHelper> {
 public:
  ~TabHelper() override;

  using OnceInstallCallback =
      base::OnceCallback<void(const ExtensionId& app_id, bool success)>;

  void CreateHostedAppFromWebContents(bool shortcut_app_requested,
                                      OnceInstallCallback callback);
  bool CanCreateBookmarkApp() const;

  // Sets the extension denoting this as an app. If |extension| is non-null this
  // tab becomes an app-tab. WebContents does not listen for unload events for
  // the extension. It's up to consumers of WebContents to do that.
  //
  // NOTE: this should only be manipulated before the tab is added to a browser.
  // TODO(sky): resolve if this is the right way to identify an app tab. If it
  // is, than this should be passed in the constructor.
  void SetExtensionApp(const Extension* extension);

  // Convenience for setting the app extension by id. This does nothing if
  // |extension_app_id| is empty, or an extension can't be found given the
  // specified id.
  void SetExtensionAppById(const ExtensionId& extension_app_id);

  // Returns true if an app extension has been set.
  bool is_app() const { return extension_app_ != nullptr; }

  // Return ExtensionId for extension app.
  // If an app extension has not been set, returns empty id.
  ExtensionId GetAppId() const;

  // If an app extension has been explicitly set for this WebContents its icon
  // is returned.
  //
  // NOTE: the returned icon is larger than 16x16 (its size is
  // extension_misc::EXTENSION_ICON_SMALLISH).
  SkBitmap* GetExtensionAppIcon();

  ScriptExecutor* script_executor() {
    return script_executor_.get();
  }

  ExtensionActionRunner* extension_action_runner() {
    return extension_action_runner_.get();
  }

  ActiveTabPermissionGranter* active_tab_permission_granter() {
    return active_tab_permission_granter_.get();
  }

 private:
  // Utility function to invoke member functions on all relevant
  // ContentRulesRegistries.
  template <class Func>
  void InvokeForContentRulesRegistries(const Func& func);

  // Different types of action when web app info is available.
  // OnDidGetWebApplicationInfo uses this to dispatch calls.
  enum WebAppAction {
    NONE,               // No action at all.
    CREATE_HOSTED_APP,  // Create and install a hosted app.
  };

  explicit TabHelper(content::WebContents* web_contents);

  friend class content::WebContentsUserData<TabHelper>;

  // Displays UI for completion of creating a bookmark hosted app.
  void FinishCreateBookmarkApp(const Extension* extension,
                               const WebApplicationInfo& web_app_info);

  // content::WebContentsObserver overrides.
  void RenderFrameCreated(content::RenderFrameHost* host) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  bool OnMessageReceived(const IPC::Message& message,
                         content::RenderFrameHost* sender) override;
  void DidCloneToNewWebContents(
      content::WebContents* old_web_contents,
      content::WebContents* new_web_contents) override;

  // ExtensionFunctionDispatcher::Delegate overrides.
  WindowController* GetExtensionWindowController() const override;
  content::WebContents* GetAssociatedWebContents() const override;

  // ExtensionRegistryObserver:
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const Extension* extension,
                           UnloadedExtensionReason reason) override;

  // Message handlers.
  void OnDidGetWebApplicationInfo(
      chrome::mojom::ChromeRenderFrameAssociatedPtr chrome_render_frame,
      bool shortcut_app_requested,
      const WebApplicationInfo& info);
  void OnGetAppInstallState(content::RenderFrameHost* host,
                            const GURL& requestor_url,
                            int return_route_id,
                            int callback_id);
  void OnContentScriptsExecuting(content::RenderFrameHost* host,
                                 const ExecutingScriptsMap& extension_ids,
                                 const GURL& on_url);

  // App extensions related methods:

  // Resets app_icon_ and if |extension| is non-null uses ImageLoader to load
  // the extension's image asynchronously.
  void UpdateExtensionAppIcon(const Extension* extension);

  const Extension* GetExtension(const ExtensionId& extension_app_id);

  void OnImageLoaded(const gfx::Image& image);

  // Requests application info for the specified page. This is an asynchronous
  // request. The delegate is notified by way of OnDidGetWebApplicationInfo when
  // the data is available.
  void GetApplicationInfo(WebAppAction action, bool shortcut_app_requested);

  // Sends our tab ID to |render_frame_host|.
  void SetTabId(content::RenderFrameHost* render_frame_host);

  Profile* profile_;

  // If non-null this tab is an app tab and this is the extension the tab was
  // created for.
  const Extension* extension_app_;

  // Icon for extension_app_ (if non-null) or a manually-set icon for
  // non-extension apps.
  SkBitmap extension_app_icon_;

  // Cached web app info data.
  WebApplicationInfo web_app_info_;

  // Which deferred action to perform when OnDidGetWebApplicationInfo is
  // notified from a WebContents.
  WebAppAction pending_web_app_action_;

  // Which navigation entry was active when the GetApplicationInfo request was
  // sent, for verification when the reply returns.
  int last_committed_nav_entry_unique_id_;

  std::unique_ptr<ScriptExecutor> script_executor_;

  std::unique_ptr<ExtensionActionRunner> extension_action_runner_;

  std::unique_ptr<ActiveTabPermissionGranter> active_tab_permission_granter_;

  std::unique_ptr<BookmarkAppHelper> bookmark_app_helper_;

  // Reponse to CreateHostedAppFromWebContents request.
  OnceInstallCallback install_callback_;

  ScopedObserver<ExtensionRegistry, ExtensionRegistryObserver>
      registry_observer_;

  // Vend weak pointers that can be invalidated to stop in-progress loads.
  base::WeakPtrFactory<TabHelper> image_loader_ptr_factory_;

  // Generic weak ptr factory for posting callbacks.
  base::WeakPtrFactory<TabHelper> weak_ptr_factory_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();

  DISALLOW_COPY_AND_ASSIGN(TabHelper);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_TAB_HELPER_H_