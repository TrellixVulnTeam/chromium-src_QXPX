// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class TestDownloadsProxy {
  constructor() {
    /** @type {downloads.mojom.PageCallbackRouter} */
    this.callbackRouter = new downloads.mojom.PageCallbackRouter();

    /** @type {!downloads.mojom.PageInterface} */
    this.pageRouterProxy = this.callbackRouter.createProxy();

    /** @type {downloads.mojom.PageHandlerInterface} */
    this.handler = new TestDownloadsMojoHandler(this.pageRouterProxy);
  }
}

/** @implements {downloads.mojom.PageHandlerInterface} */
class TestDownloadsMojoHandler {
  /** @param {downloads.mojom.PageInterface} */
  constructor(pageRouterProxy) {
    /** @private {downloads.mojom.PageInterface} */
    this.pageRouterProxy_ = pageRouterProxy;

    /** @private {TestBrowserProxy} */
    this.callTracker_ = new TestBrowserProxy(['remove']);
  }

  /**
   * @param {string} methodName
   * @return {!Promise}
   */
  whenCalled(methodName) {
    return this.callTracker_.whenCalled(methodName);
  }

  /** @override */
  async remove(id) {
    this.pageRouterProxy_.removeItem(id);
    await this.pageRouterProxy_.flushForTesting();
    this.callTracker_.methodCalled('remove', id);
  }

  /** @override */
  getDownloads(searchTerms) {}

  /** @override */
  openFileRequiringGesture(id) {}

  /** @override */
  drag(id) {}

  /** @override */
  saveDangerousRequiringGesture(id) {}

  /** @override */
  discardDangerous(id) {}

  /** @override */
  retryDownload(id) {}

  /** @override */
  show(id) {}

  /** @override */
  pause(id) {}

  /** @override */
  resume(id) {}

  /** @override */
  undo() {}

  /** @override */
  cancel(id) {}

  /** @override */
  clearAll() {}

  /** @override */
  openDownloadsFolderRequiringGesture() {}
}

class TestIconLoader extends TestBrowserProxy {
  constructor() {
    super(['loadIcon']);

    /** @private */
    this.shouldIconsLoad_ = true;
  }

  /** @param {boolean} shouldIconsLoad */
  setShouldIconsLoad(shouldIconsLoad) {
    this.shouldIconsLoad_ = shouldIconsLoad;
  }

  /**
   * @param {!HTMLImageElement} imageEl
   * @param {string} filePath
   */
  loadIcon(imageEl, filePath) {
    this.methodCalled('loadIcon', filePath);
    return Promise.resolve(this.shouldIconsLoad_);
  }
}

/**
 * @param {Object=} config
 * @return {!downloads.Data}
 */
function createDownload(config) {
  return Object.assign(
      {
        byExtId: '',
        byExtName: '',
        dangerType: downloads.DangerType.NOT_DANGEROUS,
        dateString: '',
        fileExternallyRemoved: false,
        filePath: '/some/file/path',
        fileName: 'download 1',
        fileUrl: 'file:///some/file/path',
        id: '',
        lastReasonText: '',
        otr: false,
        percent: 100,
        progressStatusText: '',
        resume: false,
        retry: false,
        return: false,
        sinceString: 'Today',
        started: Date.now() - 10000,
        state: downloads.States.COMPLETE,
        total: -1,
        url: 'http://permission.site',
      },
      config || {});
}
