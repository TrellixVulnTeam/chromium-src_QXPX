// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/site_per_process_browsertest.h"

#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/scoped_observer.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "content/browser/frame_host/cross_process_frame_connector.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/navigation_controller_impl.h"
#include "content/browser/frame_host/navigator.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_child_frame.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/frame_messages.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "content/test/content_browser_test_utils_internal.h"
#include "content/test/did_commit_provisional_load_interceptor.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::ElementsAre;
using testing::WhenSorted;

namespace content {

namespace {

void UnloadPrint(FrameTreeNode* node, const char* message) {
  EXPECT_TRUE(
      ExecJs(node, JsReplace("window.onunload = function() { "
                             "  window.domAutomationController.send($1);"
                             "}",
                             message)));
}

}  // namespace

// Tests that there are no crashes if a subframe is detached in its unload
// handler. See https://crbug.com/590054.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, DetachInUnloadHandler) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(b))"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  EXPECT_EQ(
      " Site A ------------ proxies for B\n"
      "   +--Site B ------- proxies for A\n"
      "        +--Site B -- proxies for A\n"
      "Where A = http://a.com/\n"
      "      B = http://b.com/",
      DepictFrameTree(root));

  EXPECT_EQ(1, EvalJs(root->child_at(0), "frames.length;"));

  RenderFrameDeletedObserver deleted_observer(
      root->child_at(0)->child_at(0)->current_frame_host());

  // Add an unload handler to the grandchild that causes it to be synchronously
  // detached, then navigate it.
  EXPECT_TRUE(ExecuteScript(
      root->child_at(0)->child_at(0),
      "window.onunload=function(e){\n"
      "    window.parent.document.getElementById('child-0').remove();\n"
      "};\n"));
  auto script = JsReplace("window.document.getElementById('child-0').src = $1",
                          embedded_test_server()->GetURL(
                              "c.com", "/cross_site_iframe_factory.html?c"));
  EXPECT_TRUE(ExecuteScript(root->child_at(0), script));

  deleted_observer.WaitUntilDeleted();

  EXPECT_EQ(0, EvalJs(root->child_at(0), "frames.length;"));

  EXPECT_EQ(
      " Site A ------------ proxies for B\n"
      "   +--Site B ------- proxies for A\n"
      "Where A = http://a.com/\n"
      "      B = http://b.com/",
      DepictFrameTree(root));
}

// Tests that trying to navigate in the unload handler doesn't crash the
// browser.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, NavigateInUnloadHandler) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(b))"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  EXPECT_EQ(
      " Site A ------------ proxies for B\n"
      "   +--Site B ------- proxies for A\n"
      "        +--Site B -- proxies for A\n"
      "Where A = http://a.com/\n"
      "      B = http://b.com/",
      DepictFrameTree(root));

  EXPECT_EQ(1,
            EvalJs(root->child_at(0)->current_frame_host(), "frames.length;"));

  // Add an unload handler to B's subframe.
  EXPECT_TRUE(
      ExecuteScript(root->child_at(0)->child_at(0)->current_frame_host(),
                    "window.onunload=function(e){\n"
                    "    window.location = '#navigate';\n"
                    "};\n"));

  // Navigate B's subframe to a cross-site C.
  RenderFrameDeletedObserver deleted_observer(
      root->child_at(0)->child_at(0)->current_frame_host());
  auto script = JsReplace("window.document.getElementById('child-0').src = $1",
                          embedded_test_server()->GetURL(
                              "c.com", "/cross_site_iframe_factory.html"));
  EXPECT_TRUE(ExecuteScript(root->child_at(0)->current_frame_host(), script));

  // Wait until B's subframe RenderFrameHost is destroyed.
  deleted_observer.WaitUntilDeleted();

  // Check that C's subframe is alive and the navigation in the unload handler
  // was ignored.
  EXPECT_EQ(0, EvalJs(root->child_at(0)->child_at(0)->current_frame_host(),
                      "frames.length;"));

  EXPECT_EQ(
      " Site A ------------ proxies for B C\n"
      "   +--Site B ------- proxies for A C\n"
      "        +--Site C -- proxies for A B\n"
      "Where A = http://a.com/\n"
      "      B = http://b.com/\n"
      "      C = http://c.com/",
      DepictFrameTree(root));
}

// Verifies that when navigating an OOPIF to same site and then canceling
// navigation from beforeunload handler popup will not remove the
// RemoteFrameView from OOPIF's owner element in the parent process. This test
// uses OOPIF visibility to make sure RemoteFrameView exists after beforeunload
// is handled.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       CanceledBeforeUnloadShouldNotClearRemoteFrameView) {
  GURL a_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  EXPECT_TRUE(NavigateToURL(shell(), a_url));

  FrameTreeNode* child_node =
      web_contents()->GetFrameTree()->root()->child_at(0);
  GURL b_url(embedded_test_server()->GetURL(
      "b.com", "/render_frame_host/beforeunload.html"));
  NavigateFrameToURL(child_node, b_url);
  FrameConnectorDelegate* frame_connector_delegate =
      static_cast<RenderWidgetHostViewChildFrame*>(
          child_node->current_frame_host()->GetView())
          ->FrameConnectorForTesting();

  // Need user gesture for 'beforeunload' to fire.
  PrepContentsForBeforeUnloadTest(web_contents());

  // Simulate user choosing to stay on the page after beforeunload fired.
  SetShouldProceedOnBeforeUnload(shell(), true /* proceed */,
                                 false /* success */);

  // First, hide the <iframe>. This goes through RemoteFrameView::Hide() and
  // eventually updates the FrameConnectorDelegate. Also,
  // RemoteFrameView::self_visible_ will be set to false which can only be
  // undone by calling RemoteFrameView::Show. Therefore, potential calls to
  // RemoteFrameView::SetParentVisible(true) would not update the visibility at
  // the browser side.
  ASSERT_TRUE(ExecuteScript(
      web_contents(),
      "document.querySelector('iframe').style.visibility = 'hidden';"));
  while (!frame_connector_delegate->IsHidden()) {
    base::RunLoop run_loop;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
    run_loop.Run();
  }

  // Now we navigate the child to about:blank, but since we do not proceed with
  // the navigation, the OOPIF should stay alive and RemoteFrameView intact.
  ASSERT_TRUE(ExecuteScript(
      web_contents(), "document.querySelector('iframe').src = 'about:blank';"));
  WaitForAppModalDialog(shell());

  // Sanity check: We should still have an OOPIF and hence a RWHVCF.
  ASSERT_TRUE(static_cast<RenderWidgetHostViewBase*>(
                  child_node->current_frame_host()->GetView())
                  ->IsRenderWidgetHostViewChildFrame());

  // Now make the <iframe> visible again. This calls RemoteFrameView::Show()
  // only if the RemoteFrameView is the EmbeddedContentView of the corresponding
  // HTMLFrameOwnerElement.
  ASSERT_TRUE(ExecuteScript(
      web_contents(),
      "document.querySelector('iframe').style.visibility = 'visible';"));
  while (frame_connector_delegate->IsHidden()) {
    base::RunLoop run_loop;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
    run_loop.Run();
  }
}

// Ensure that after a main frame with an OOPIF is navigated cross-site, the
// unload handler in the OOPIF sees correct main frame origin, namely the old
// and not the new origin.  See https://crbug.com/825283.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       ParentOriginDoesNotChangeInUnloadHandler) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));
  FrameTreeNode* root = static_cast<WebContentsImpl*>(shell()->web_contents())
                            ->GetFrameTree()
                            ->root();

  // Open a popup on b.com.  The b.com subframe on the main frame will use this
  // in its unload handler.
  GURL b_url(embedded_test_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(OpenPopup(shell()->web_contents(), b_url, "popup"));

  // Add an unload handler to b.com subframe, which will look up the top
  // frame's origin and send it via domAutomationController.  Unfortunately,
  // the subframe's browser-side state will have been torn down when it runs
  // the unload handler, so to ensure that the message can be received, send it
  // through the popup.
  EXPECT_TRUE(
      ExecuteScript(root->child_at(0),
                    "window.onunload = function(e) {"
                    "  window.open('','popup').domAutomationController.send("
                    "      'top-origin ' + location.ancestorOrigins[0]);"
                    "};"));

  // Navigate the main frame to c.com and wait for the message from the
  // subframe's unload handler.
  GURL c_url(embedded_test_server()->GetURL("c.com", "/title1.html"));
  DOMMessageQueue msg_queue;
  EXPECT_TRUE(NavigateToURL(shell(), c_url));
  std::string message, top_origin;
  while (msg_queue.WaitForMessage(&message)) {
    base::TrimString(message, "\"", &message);
    auto message_parts = base::SplitString(message, " ", base::TRIM_WHITESPACE,
                                           base::SPLIT_WANT_NONEMPTY);
    if (message_parts[0] == "top-origin") {
      top_origin = message_parts[1];
      break;
    }
  }

  // The top frame's origin should be a.com, not c.com.
  EXPECT_EQ(top_origin + "/", main_url.GetOrigin().spec());
}

// Verify that when the last active frame in a process is going away as part of
// OnSwapOut, the SwapOut ACK is received prior to the process starting to shut
// down, ensuring that any related unload work also happens before shutdown.
// See https://crbug.com/867274 and https://crbug.com/794625.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       SwapOutACKArrivesPriorToProcessShutdownRequest) {
  GURL start_url(embedded_test_server()->GetURL("a.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), start_url));
  RenderFrameHostImpl* rfh = web_contents()->GetMainFrame();
  rfh->DisableSwapOutTimerForTesting();

  // Navigate cross-site.  Since the current frame is the last active frame in
  // the current process, the process will eventually shut down.  Once the
  // process goes away, ensure that the SwapOut ACK was received (i.e., that we
  // didn't just simulate OnSwappedOut() due to the process erroneously going
  // away before the SwapOut ACK was received, as in https://crbug.com/867274).
  RenderProcessHostWatcher watcher(
      rfh->GetProcess(), RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  auto swapout_ack_filter = base::MakeRefCounted<ObserveMessageFilter>(
      FrameMsgStart, FrameHostMsg_SwapOut_ACK::ID);
  rfh->GetProcess()->AddFilter(swapout_ack_filter.get());
  GURL cross_site_url(embedded_test_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURLFromRenderer(shell(), cross_site_url));
  watcher.Wait();
  EXPECT_TRUE(swapout_ack_filter->has_received_message());
  EXPECT_TRUE(watcher.did_exit_normally());
}

class TestWCBeforeUnloadDelegate : public JavaScriptDialogManager,
                                   public WebContentsDelegate {
 public:
  explicit TestWCBeforeUnloadDelegate(WebContentsImpl* web_contents)
      : web_contents_(web_contents) {
    web_contents_->SetDelegate(this);
  }

  ~TestWCBeforeUnloadDelegate() override {
    if (!callback_.is_null())
      std::move(callback_).Run(true, base::string16());

    web_contents_->SetDelegate(nullptr);
    web_contents_->SetJavaScriptDialogManagerForTesting(nullptr);
  }

  void Wait() {
    run_loop_->Run();
    run_loop_ = std::make_unique<base::RunLoop>();
  }

  // WebContentsDelegate

  JavaScriptDialogManager* GetJavaScriptDialogManager(
      WebContents* source) override {
    return this;
  }

  // JavaScriptDialogManager

  void RunJavaScriptDialog(WebContents* web_contents,
                           RenderFrameHost* render_frame_host,
                           JavaScriptDialogType dialog_type,
                           const base::string16& message_text,
                           const base::string16& default_prompt_text,
                           DialogClosedCallback callback,
                           bool* did_suppress_message) override {
    NOTREACHED();
  }

  void RunBeforeUnloadDialog(WebContents* web_contents,
                             RenderFrameHost* render_frame_host,
                             bool is_reload,
                             DialogClosedCallback callback) override {
    callback_ = std::move(callback);
    run_loop_->Quit();
  }

  bool HandleJavaScriptDialog(WebContents* web_contents,
                              bool accept,
                              const base::string16* prompt_override) override {
    NOTREACHED();
    return true;
  }

  void CancelDialogs(WebContents* web_contents, bool reset_state) override {}

 private:
  WebContentsImpl* web_contents_;

  DialogClosedCallback callback_;

  std::unique_ptr<base::RunLoop> run_loop_ = std::make_unique<base::RunLoop>();

  DISALLOW_COPY_AND_ASSIGN(TestWCBeforeUnloadDelegate);
};

// This is a regression test for https://crbug.com/891423 in which tabs showing
// beforeunload dialogs stalled navigation and triggered the "hung process"
// dialog.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       NoCommitTimeoutWithBeforeUnloadDialog) {
  WebContentsImpl* web_contents =
      static_cast<WebContentsImpl*>(shell()->web_contents());

  // Navigate first tab to a.com.
  GURL a_url(embedded_test_server()->GetURL("a.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), a_url));
  RenderProcessHost* a_process = web_contents->GetMainFrame()->GetProcess();

  // Open b.com in a second tab.  Using a renderer-initiated navigation is
  // important to leave a.com and b.com SiteInstances in the same
  // BrowsingInstance (so the b.com -> a.com navigation in the next test step
  // will reuse the process associated with the first a.com tab).
  GURL b_url(embedded_test_server()->GetURL("b.com", "/title2.html"));
  Shell* new_shell = OpenPopup(web_contents, b_url, "newtab");
  WebContents* new_contents = new_shell->web_contents();
  EXPECT_TRUE(WaitForLoadStop(new_contents));
  RenderProcessHost* b_process = new_contents->GetMainFrame()->GetProcess();
  EXPECT_NE(a_process, b_process);

  // Disable the beforeunload hang monitor (otherwise there will be a race
  // between the beforeunload dialog and the beforeunload hang timer) and give
  // the page a gesture to allow dialogs.
  web_contents->GetMainFrame()->DisableBeforeUnloadHangMonitorForTesting();
  web_contents->GetMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
      base::string16());

  // Hang the first contents in a beforeunload dialog.
  TestWCBeforeUnloadDelegate test_delegate(web_contents);
  EXPECT_TRUE(
      ExecJs(web_contents, "window.onbeforeunload=function(e){ return 'x' }"));
  EXPECT_TRUE(ExecJs(web_contents,
                     "setTimeout(function() { window.location.reload() }, 0)"));
  test_delegate.Wait();

  // Attempt to navigate the second tab to a.com.  This will attempt to reuse
  // the hung process.
  base::TimeDelta kTimeout = base::TimeDelta::FromMilliseconds(100);
  NavigationHandleImpl::SetCommitTimeoutForTesting(kTimeout);
  GURL hung_url(embedded_test_server()->GetURL("a.com", "/title3.html"));
  UnresponsiveRendererObserver unresponsive_renderer_observer(new_contents);
  EXPECT_TRUE(
      ExecJs(new_contents, JsReplace("window.location = $1", hung_url)));

  // Verify that we will not be notified about the unresponsive renderer.
  // Before changes in https://crrev.com/c/1089797, the test would get notified
  // and therefore |hung_process| would be non-null.
  RenderProcessHost* hung_process =
      unresponsive_renderer_observer.Wait(kTimeout * 10);
  EXPECT_FALSE(hung_process);

  // Reset the timeout.
  NavigationHandleImpl::SetCommitTimeoutForTesting(base::TimeDelta());
}

// Test that unload handlers in iframes are run, even when the removed subtree
// is complicated with nested iframes in different processes.
//     A1                         A1
//    / \                        / \
//   B1  D  --- Navigate --->   E   D
//  / \
// C1  C2
// |   |
// B2  A2
//     |
//     C3
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, UnloadHandlerSubframes) {
  GURL main_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c(b),c(a(c))),d)"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  // Add a unload handler to every frames. It notifies the browser using the
  // DomAutomationController it has been executed.
  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  UnloadPrint(root, "A1");
  UnloadPrint(root->child_at(0), "B1");
  UnloadPrint(root->child_at(0)->child_at(0), "C1");
  UnloadPrint(root->child_at(0)->child_at(1), "C2");
  UnloadPrint(root->child_at(0)->child_at(0)->child_at(0), "B2");
  UnloadPrint(root->child_at(0)->child_at(1)->child_at(0), "A2");
  UnloadPrint(root->child_at(0)->child_at(1)->child_at(0)->child_at(0), "C3");
  DOMMessageQueue dom_message_queue(
      WebContents::FromRenderFrameHost(web_contents()->GetMainFrame()));

  // Disable the swap out timer on B1.
  root->child_at(0)->current_frame_host()->DisableSwapOutTimerForTesting();

  // Process B and C are expected to shutdown once every unload handler has
  // run.
  RenderProcessHostWatcher shutdown_B(
      root->child_at(0)->current_frame_host()->GetProcess(),
      RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  RenderProcessHostWatcher shutdown_C(
      root->child_at(0)->child_at(0)->current_frame_host()->GetProcess(),
      RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);

  // Navigate B to E.
  GURL e_url(embedded_test_server()->GetURL("e.com", "/title1.html"));
  NavigateFrameToURL(root->child_at(0), e_url);

  // Collect unload handler messages.
  std::string message;
  std::vector<std::string> messages;
  for (int i = 0; i < 6; ++i) {
    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    base::TrimString(message, "\"", &message);
    messages.push_back(message);
  }
  EXPECT_FALSE(dom_message_queue.PopMessage(&message));

  // Check every frame in the replaced subtree has executed its unload handler.
  EXPECT_THAT(messages,
              WhenSorted(ElementsAre("A2", "B1", "B2", "C1", "C2", "C3")));

  // In every renderer process, check ancestors have executed their unload
  // handler before their children. This is a slightly less restrictive
  // condition than the specification which requires it to be global instead of
  // per process.
  // https://html.spec.whatwg.org/multipage/browsing-the-web.html#unloading-documents
  //
  // In process B:
  auto B1 = std::find(messages.begin(), messages.end(), "B1");
  auto B2 = std::find(messages.begin(), messages.end(), "B2");
  EXPECT_LT(B1, B2);

  // In process C:
  auto C2 = std::find(messages.begin(), messages.end(), "C2");
  auto C3 = std::find(messages.begin(), messages.end(), "C3");
  EXPECT_LT(C2, C3);

  // Make sure the processes are deleted at some point.
  shutdown_B.Wait();
  shutdown_C.Wait();
}

// Check that unload handlers in iframe don't prevents the main frame to be
// deleted after a timeout.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, SlowUnloadHandlerInIframe) {
  GURL initial_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL next_url(embedded_test_server()->GetURL("c.com", "/title1.html"));

  // 1) Navigate on a page with an iframe.
  EXPECT_TRUE(NavigateToURL(shell(), initial_url));

  // 2) Act as if there was an infinite unload handler in B.
  auto filter = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_Detach::ID);
  RenderFrameHost* rfh_b =
      web_contents()->GetFrameTree()->root()->child_at(0)->current_frame_host();
  rfh_b->GetProcess()->AddFilter(filter.get());

  // 3) Navigate and check the old frame is deleted after some time.
  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  RenderFrameDeletedObserver deleted_observer(root->current_frame_host());
  EXPECT_TRUE(NavigateToURL(shell(), next_url));
  deleted_observer.WaitUntilDeleted();
}

// Navigate from A(B(A(B)) to C. Check the unload handler are executed, executed
// in the right order and the processes for A and B are removed.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, Unload_ABAB) {
  GURL initial_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(a(b)))"));
  GURL next_url(embedded_test_server()->GetURL("c.com", "/title1.html"));

  // 1) Navigate on a page with an iframe.
  EXPECT_TRUE(NavigateToURL(shell(), initial_url));

  // 2) Add unload handler on every frame.
  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  UnloadPrint(root, "A1");
  UnloadPrint(root->child_at(0), "B1");
  UnloadPrint(root->child_at(0)->child_at(0), "A2");
  UnloadPrint(root->child_at(0)->child_at(0)->child_at(0), "B2");
  root->current_frame_host()->DisableSwapOutTimerForTesting();

  DOMMessageQueue dom_message_queue(
      WebContents::FromRenderFrameHost(web_contents()->GetMainFrame()));
  RenderProcessHostWatcher shutdown_A(
      root->current_frame_host()->GetProcess(),
      RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  RenderProcessHostWatcher shutdown_B(
      root->child_at(0)->current_frame_host()->GetProcess(),
      RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);

  // 3) Navigate cross process.
  EXPECT_TRUE(NavigateToURL(shell(), next_url));

  // 4) Wait for unload handler messages and check they are sent in order.
  std::vector<std::string> messages;
  std::string message;
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
    base::TrimString(message, "\"", &message);
    messages.push_back(message);
  }
  EXPECT_FALSE(dom_message_queue.PopMessage(&message));

  EXPECT_THAT(messages, WhenSorted(ElementsAre("A1", "A2", "B1", "B2")));
  auto A1 = std::find(messages.begin(), messages.end(), "A1");
  auto A2 = std::find(messages.begin(), messages.end(), "A2");
  auto B1 = std::find(messages.begin(), messages.end(), "B1");
  auto B2 = std::find(messages.begin(), messages.end(), "B2");
  EXPECT_LT(A1, A2);
  EXPECT_LT(B1, B2);

  // Make sure the processes are deleted at some point.
  shutdown_A.Wait();
  shutdown_B.Wait();
}

// Start with A(B(C)), navigate C to D and then B to E. By emulating a slow
// unload handler in B,C and D, the end result is C is in pending deletion in B
// and B is in pending deletion in A.
//   (1)     (2)     (3)
//|       |       |       |
//|   A   |  A    |   A   |
//|   |   |  |    |    \  |
//|   B   |  B    |  B  E |
//|   |   |   \   |   \   |
//|   C   | C  D  | C  D  |
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, UnloadNestedPendingDeletion) {
  std::string onunload_script = "window.onunload = function(){}";
  GURL url_abc(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c))"));
  GURL url_d(embedded_test_server()->GetURL("d.com", "/title1.html"));
  GURL url_e(embedded_test_server()->GetURL("e.com", "/title1.html"));

  // 1) Navigate to a page with an iframe.
  EXPECT_TRUE(NavigateToURL(shell(), url_abc));
  RenderFrameHostImpl* rfh_a = web_contents()->GetMainFrame();
  RenderFrameHostImpl* rfh_b = rfh_a->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_c = rfh_b->child_at(0)->current_frame_host();
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::NotRun, rfh_a->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::NotRun, rfh_b->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::NotRun, rfh_c->unload_state_);

  // Act as if there was a slow unload handler on rfh_b and rfh_c.
  // The navigating frames are waiting for FrameHostMsg_SwapoutACK.
  auto swapout_ack_filter_b = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_SwapOut_ACK::ID);
  auto swapout_ack_filter_c = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_SwapOut_ACK::ID);
  rfh_b->GetProcess()->AddFilter(swapout_ack_filter_b.get());
  rfh_c->GetProcess()->AddFilter(swapout_ack_filter_c.get());
  EXPECT_TRUE(ExecuteScript(rfh_b->frame_tree_node(), onunload_script));
  EXPECT_TRUE(ExecuteScript(rfh_c->frame_tree_node(), onunload_script));
  rfh_b->DisableSwapOutTimerForTesting();
  rfh_c->DisableSwapOutTimerForTesting();

  RenderFrameDeletedObserver delete_b(rfh_b), delete_c(rfh_c);

  // 2) Navigate rfh_c to D.
  NavigateFrameToURL(rfh_c->frame_tree_node(), url_d);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::NotRun, rfh_a->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::NotRun, rfh_b->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, rfh_c->unload_state_);
  RenderFrameHostImpl* rfh_d = rfh_b->child_at(0)->current_frame_host();

  RenderFrameDeletedObserver delete_d(rfh_d);

  // Act as if there was a slow unload handler on rfh_d.
  // The non navigating frames are waiting for FrameHostMsg_Detach.
  auto detach_filter = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_Detach::ID);
  rfh_d->GetProcess()->AddFilter(detach_filter.get());
  EXPECT_TRUE(ExecuteScript(rfh_d->frame_tree_node(), onunload_script));

  // 3) Navigate rfh_b to E.
  NavigateFrameToURL(rfh_b->frame_tree_node(), url_e);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::NotRun, rfh_a->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, rfh_b->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, rfh_c->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, rfh_d->unload_state_);

  // rfh_d completes its unload event. It deletes the frame, including rfh_c.
  EXPECT_FALSE(delete_c.deleted());
  EXPECT_FALSE(delete_d.deleted());
  rfh_d->OnDetach();
  EXPECT_TRUE(delete_c.deleted());
  EXPECT_TRUE(delete_d.deleted());

  // rfh_b completes its unload event.
  EXPECT_FALSE(delete_b.deleted());
  rfh_b->OnSwapOutACK();
  EXPECT_TRUE(delete_b.deleted());
}

// A set of nested frames A1(B1(A2)) are pending deletion because of a
// navigation. This tests what happens if only A2 has an unload handler.
// If B1 receives FrameHostMsg_OnDetach before A2, it should not destroy itself
// and its children, but rather wait for A2.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest, PartialUnloadHandler) {
  GURL url_aba(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(a))"));
  GURL url_c(embedded_test_server()->GetURL("c.com", "/title1.html"));

  // 1) Navigate to A1(B1(A2))
  EXPECT_TRUE(NavigateToURL(shell(), url_aba));

  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  RenderFrameHostImpl* a1 = root->current_frame_host();
  RenderFrameHostImpl* b1 = a1->child_at(0)->current_frame_host();
  RenderFrameHostImpl* a2 = b1->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver delete_a1(a1);
  RenderFrameDeletedObserver delete_a2(a2);
  RenderFrameDeletedObserver delete_b1(b1);

  // Disable Detach and Swapout ACK. They will be called manually.
  auto swapout_ack_filter = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_SwapOut_ACK::ID);
  auto detach_filter_a = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_Detach::ID);
  auto detach_filter_b = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_Detach::ID);
  a1->GetProcess()->AddFilter(swapout_ack_filter.get());
  a1->GetProcess()->AddFilter(detach_filter_a.get());
  b1->GetProcess()->AddFilter(detach_filter_b.get());

  a1->DisableSwapOutTimerForTesting();

  // Add unload handler on A2, but not on the other frames.
  UnloadPrint(a2->frame_tree_node(), "A2");

  DOMMessageQueue dom_message_queue(
      WebContents::FromRenderFrameHost(web_contents()->GetMainFrame()));

  // 2) Navigate cross process.
  EXPECT_TRUE(NavigateToURL(shell(), url_c));

  // Check that unload handlers are executed.
  std::string message, message_unused;
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&message));
  EXPECT_FALSE(dom_message_queue.PopMessage(&message_unused));
  EXPECT_EQ("\"A2\"", message);

  // No RenderFrameHost are deleted so far.
  EXPECT_FALSE(delete_a1.deleted());
  EXPECT_FALSE(delete_b1.deleted());
  EXPECT_FALSE(delete_a2.deleted());
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, a1->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::Completed, b1->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, a2->unload_state_);

  // 3) B1 receives confirmation it has been deleted. This has no effect,
  //    because it is still waiting on A2 to be deleted.
  b1->OnDetach();
  EXPECT_FALSE(delete_a1.deleted());
  EXPECT_FALSE(delete_b1.deleted());
  EXPECT_FALSE(delete_a2.deleted());
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, a1->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::Completed, b1->unload_state_);
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, a2->unload_state_);

  // 4) A2 received confirmation that it has been deleted and destroy B1 and A2.
  a2->OnDetach();
  EXPECT_FALSE(delete_a1.deleted());
  EXPECT_TRUE(delete_b1.deleted());
  EXPECT_TRUE(delete_a2.deleted());
  EXPECT_EQ(RenderFrameHostImpl::UnloadState::InProgress, a1->unload_state_);

  // 5) A1 receives SwapOutACK and deletes itself.
  a1->OnSwapOutACK();
  EXPECT_TRUE(delete_a1.deleted());
}

// Test RenderFrameHostImpl::PendingDeletionCheckCompletedOnSubtree.
//
// After a navigation commit, some children with no unload handler may be
// eligible for immediate deletion. Several configurations are tested:
//
// Before navigation commit
//
//              0               |  N  : No unload handler
//   ‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑‑      | [N] : Unload handler
//  |  |  |  |  |   |     |     |
// [1] 2 [3] 5  7   9     12    |
//        |  |  |  / \   / \    |
//        4 [6] 8 10 11 13 [14] |
//
// After navigation commit (expected)
//
//              0               |  N  : No unload handler
//   ---------------------      | [N] : Unload handler
//  |     |  |            |     |
// [1]   [3] 5            12    |
//           |             \    |
//          [6]            [14] |
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       PendingDeletionCheckCompletedOnSubtree) {
  GURL url_1(embedded_test_server()->GetURL(
      "a.com",
      "/cross_site_iframe_factory.html?a(a,a,a(a),a(a),a(a),a(a,a),a(a,a))"));
  GURL url_2(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to 0(1,2,3(4),5(6),7(8),9(10,11),12(13,14));
  EXPECT_TRUE(NavigateToURL(shell(), url_1));

  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  RenderFrameHostImpl* rfh_0 = root->current_frame_host();
  RenderFrameHostImpl* rfh_1 = rfh_0->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_2 = rfh_0->child_at(1)->current_frame_host();
  RenderFrameHostImpl* rfh_3 = rfh_0->child_at(2)->current_frame_host();
  RenderFrameHostImpl* rfh_4 = rfh_3->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_5 = rfh_0->child_at(3)->current_frame_host();
  RenderFrameHostImpl* rfh_6 = rfh_5->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_7 = rfh_0->child_at(4)->current_frame_host();
  RenderFrameHostImpl* rfh_8 = rfh_7->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_9 = rfh_0->child_at(5)->current_frame_host();
  RenderFrameHostImpl* rfh_10 = rfh_9->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_11 = rfh_9->child_at(1)->current_frame_host();
  RenderFrameHostImpl* rfh_12 = rfh_0->child_at(6)->current_frame_host();
  RenderFrameHostImpl* rfh_13 = rfh_12->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_14 = rfh_12->child_at(1)->current_frame_host();

  RenderFrameDeletedObserver delete_a0(rfh_0), delete_a1(rfh_1),
      delete_a2(rfh_2), delete_a3(rfh_3), delete_a4(rfh_4), delete_a5(rfh_5),
      delete_a6(rfh_6), delete_a7(rfh_7), delete_a8(rfh_8), delete_a9(rfh_9),
      delete_a10(rfh_10), delete_a11(rfh_11), delete_a12(rfh_12),
      delete_a13(rfh_13), delete_a14(rfh_14);

  // Add the unload handlers.
  UnloadPrint(rfh_1->frame_tree_node(), "");
  UnloadPrint(rfh_3->frame_tree_node(), "");
  UnloadPrint(rfh_6->frame_tree_node(), "");
  UnloadPrint(rfh_14->frame_tree_node(), "");

  // Disable Detach and Swapout ACK.
  auto swapout_ack_filter = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_SwapOut_ACK::ID);
  auto detach_filter = base::MakeRefCounted<DropMessageFilter>(
      FrameMsgStart, FrameHostMsg_Detach::ID);
  rfh_0->GetProcess()->AddFilter(swapout_ack_filter.get());
  rfh_0->GetProcess()->AddFilter(detach_filter.get());
  rfh_0->DisableSwapOutTimerForTesting();

  // 2) Navigate cross process and check the tree. See diagram above.
  EXPECT_TRUE(NavigateToURL(shell(), url_2));

  EXPECT_FALSE(delete_a0.deleted());
  EXPECT_FALSE(delete_a1.deleted());
  EXPECT_TRUE(delete_a2.deleted());
  EXPECT_FALSE(delete_a3.deleted());
  EXPECT_TRUE(delete_a4.deleted());
  EXPECT_FALSE(delete_a5.deleted());
  EXPECT_FALSE(delete_a6.deleted());
  EXPECT_TRUE(delete_a7.deleted());
  EXPECT_TRUE(delete_a8.deleted());
  EXPECT_TRUE(delete_a9.deleted());
  EXPECT_TRUE(delete_a10.deleted());
  EXPECT_TRUE(delete_a11.deleted());
  EXPECT_FALSE(delete_a12.deleted());
  EXPECT_TRUE(delete_a13.deleted());
  EXPECT_FALSE(delete_a14.deleted());
}

// When an iframe is detached, check that unload handlers execute in all of its
// child frames. Start from A(B(C)) and delete B from A.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DetachedIframeUnloadHandlerABC) {
  GURL initial_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c))"));

  // 1) Navigate to a(b(c))
  EXPECT_TRUE(NavigateToURL(shell(), initial_url));
  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  RenderFrameHostImpl* rfh_a = root->current_frame_host();
  RenderFrameHostImpl* rfh_b = rfh_a->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_c = rfh_b->child_at(0)->current_frame_host();

  // 2) Add unload handlers on B and C.
  UnloadPrint(rfh_b->frame_tree_node(), "B");
  UnloadPrint(rfh_c->frame_tree_node(), "C");

  DOMMessageQueue dom_message_queue(web_contents());
  RenderProcessHostWatcher shutdown_B(
      rfh_b->GetProcess(), RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  RenderProcessHostWatcher shutdown_C(
      rfh_c->GetProcess(), RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);

  // 3) Detach B from A.
  ExecuteScriptAsync(root, "document.querySelector('iframe').remove();");

  // 4) Wait for unload handler.
  std::vector<std::string> messages(2);
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[0]));
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[1]));
  std::string unused;
  EXPECT_FALSE(dom_message_queue.PopMessage(&unused));

  std::sort(messages.begin(), messages.end());
  EXPECT_EQ("\"B\"", messages[0]);
  EXPECT_EQ("\"C\"", messages[1]);

  // Make sure the processes are deleted at some point.
  shutdown_B.Wait();
  shutdown_C.Wait();
}

// When an iframe is detached, check that unload handlers execute in all of its
// child frames. Start from A(B1(C(B2))) and delete B1 from A.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DetachedIframeUnloadHandlerABCB) {
  GURL initial_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b(c(b)))"));

  // 1) Navigate to a(b(c(b)))
  EXPECT_TRUE(NavigateToURL(shell(), initial_url));
  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  RenderFrameHostImpl* rfh_a = root->current_frame_host();
  RenderFrameHostImpl* rfh_b1 = rfh_a->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_c = rfh_b1->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_b2 = rfh_c->child_at(0)->current_frame_host();

  // 2) Add unload handlers on B1, B2 and C.
  UnloadPrint(rfh_b1->frame_tree_node(), "B1");
  UnloadPrint(rfh_b2->frame_tree_node(), "B2");
  UnloadPrint(rfh_c->frame_tree_node(), "C");

  DOMMessageQueue dom_message_queue(web_contents());
  RenderProcessHostWatcher shutdown_B(
      rfh_b1->GetProcess(), RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  RenderProcessHostWatcher shutdown_C(
      rfh_c->GetProcess(), RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);

  // 3) Detach B from A.
  ExecuteScriptAsync(root, "document.querySelector('iframe').remove();");

  // 4) Wait for unload handler.
  std::vector<std::string> messages(3);
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[0]));
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[1]));
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[2]));
  std::string unused;
  EXPECT_FALSE(dom_message_queue.PopMessage(&unused));

  std::sort(messages.begin(), messages.end());
  EXPECT_EQ("\"B1\"", messages[0]);
  EXPECT_EQ("\"B2\"", messages[1]);
  EXPECT_EQ("\"C\"", messages[2]);

  // Make sure the processes are deleted at some point.
  shutdown_B.Wait();
  shutdown_C.Wait();
}

// When an iframe is detached, check that unload handlers execute in all of its
// child frames. Start from A1(A2(B)), delete A2 from itself.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       DetachedIframeUnloadHandlerAAB) {
  GURL initial_url(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(a(b))"));

  // 1) Navigate to a(a(b)).
  EXPECT_TRUE(NavigateToURL(shell(), initial_url));
  FrameTreeNode* root = web_contents()->GetFrameTree()->root();
  RenderFrameHostImpl* rfh_a1 = root->current_frame_host();
  RenderFrameHostImpl* rfh_a2 = rfh_a1->child_at(0)->current_frame_host();
  RenderFrameHostImpl* rfh_b = rfh_a2->child_at(0)->current_frame_host();

  // 2) Add unload handlers on A2 ad B.
  UnloadPrint(rfh_a2->frame_tree_node(), "A2");
  UnloadPrint(rfh_b->frame_tree_node(), "B");

  DOMMessageQueue dom_message_queue(web_contents());
  RenderProcessHostWatcher shutdown_B(
      rfh_b->GetProcess(), RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);

  // 3) A2 detaches itself.
  ExecuteScriptAsync(rfh_a2->frame_tree_node(),
                     "parent.document.querySelector('iframe').remove();");

  // 4) Wait for unload handler.
  std::vector<std::string> messages(2);
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[0]));
  EXPECT_TRUE(dom_message_queue.WaitForMessage(&messages[1]));
  std::string unused;
  EXPECT_FALSE(dom_message_queue.PopMessage(&unused));

  std::sort(messages.begin(), messages.end());
  EXPECT_EQ("\"A2\"", messages[0]);
  EXPECT_EQ("\"B\"", messages[1]);

  // Make sure the process is deleted at some point.
  shutdown_B.Wait();
}

// Tests that running layout from an unload handler inside teardown of the
// RenderWidget (inside WidgetMsg_Close) can succeed.
IN_PROC_BROWSER_TEST_F(SitePerProcessBrowserTest,
                       RendererInitiatedWindowCloseWithUnload) {
  GURL main_url(embedded_test_server()->GetURL("a.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  FrameTreeNode* root = web_contents()->GetFrameTree()->root();

  // We will window.open() another URL on the same domain so they share a
  // renderer. This window has an unload handler that forces layout to occur.
  // Then we (in a new stack) close that window causing that layout. If all
  // goes well the window closes. If it goes poorly, the renderer may crash.
  //
  // This path is special because the unload results from window.close() which
  // avoids the user-initiated close path through ViewMsg_ClosePage. In that
  // path the unload handlers are run early, before the actual teardown of
  // the closing RenderWidget.
  GURL open_url = embedded_test_server()->GetURL(
      "a.com", "/unload_handler_force_layout.html");

  // Listen for messages from the window that the test opens, and convert them
  // into the document title, which we can wait on in the main test window.
  EXPECT_TRUE(
      ExecuteScript(root,
                    "window.addEventListener('message', function(event) {\n"
                    "  document.title = event.data;\n"
                    "});"));

  // This performs window.open() and waits for the title of the original
  // document to change to signal that the unload handler has been registered.
  {
    base::string16 title_when_loaded = base::UTF8ToUTF16("loaded");
    TitleWatcher title_watcher(shell()->web_contents(), title_when_loaded);
    EXPECT_TRUE(
        ExecuteScript(root, JsReplace("var w = window.open($1)", open_url)));
    EXPECT_EQ(title_watcher.WaitAndGetTitle(), title_when_loaded);
  }

  // The closes the window and waits for the title of the original document to
  // change again to signal that the unload handler has run.
  {
    base::string16 title_when_done = base::UTF8ToUTF16("unloaded");
    TitleWatcher title_watcher(shell()->web_contents(), title_when_done);
    EXPECT_TRUE(ExecuteScript(root, "w.close()"));
    EXPECT_EQ(title_watcher.WaitAndGetTitle(), title_when_done);
  }
}

}  // namespace content