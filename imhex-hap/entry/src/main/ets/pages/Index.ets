import { common, Want, abilityAccessCtrl, ConfigurationConstant } from '@kit.AbilityKit';
import { unifiedDataChannel } from '@kit.ArkData';
import { pointer } from '@kit.InputKit';
import { fileIo, picker } from '@kit.CoreFileKit';
import { pasteboard } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { window, display, promptAction } from '@kit.ArkUI';
import { i18n } from '@kit.LocalizationKit';
import { notificationManager } from '@kit.NotificationKit';
import nativeModule from 'libentry.so';

const TAG = 'HmHex';

/**
 * In-app explanation shown before the system notification permission dialog.
 * Custom dialog (not AlertDialog) so the two buttons have no divider line.
 */
@CustomDialog
struct NotificationExplainDialog {
  controller: CustomDialogController;
  onConfirm: () => void = () => {};
  onDecline: () => void = () => {};

  build() {
    Column({ space: 16 }) {
      Text('允许通知')
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
      Text('HmHex 会在后台任务完成时（例如大文件加载、模式分析、哈希计算）发送通知提醒您。点击"确定"后，系统将询问是否允许接收通知；选择"不需要通知"则不再询问。')
        .fontSize(14)
        .lineHeight(20)
        .textAlign(TextAlign.Center)
      Row({ space: 12 }) {
        Button('不需要通知')
          .layoutWeight(1)
          .buttonStyle(ButtonStyleMode.NORMAL)
          .onClick(() => {
            this.controller.close();
            this.onDecline();
          })
        Button('确定')
          .layoutWeight(1)
          .onClick(() => {
            this.controller.close();
            this.onConfirm();
          })
      }
      .width('100%')
    }
    .padding(24)
    .width('80%')
  }
}
const LOG_DOMAIN = 0xD002D00;

hilog.info(LOG_DOMAIN, TAG, 'module loaded, nativeModule=%{public}s',
  nativeModule === undefined ? 'undefined' : (typeof nativeModule));

@Entry
@Component
struct Index {
  private xcomponentController: XComponentController = new XComponentController();
  private notifDialogController: CustomDialogController | null = null;

  // Per-launch resource handles, cleaned up in aboutToDisappear so a rebuild
  // of this page in the same process never stacks duplicate timers/listeners.
  private themePollingId: number = -1;
  private taskPollingId: number = -1;
  private mainWindow: window.Window | null = null;
  private onOpenFileHandler = (uri: string): void => {
    hilog.error(LOG_DOMAIN, TAG, 'open-with (running): %{public}s', uri);
    this.importFiles([uri], true);
  };
  private onTitleRectHandler = (rect: window.TitleButtonRect): void => {
    hilog.error(LOG_DOMAIN, TAG, 'title button rect: %{public}s', JSON.stringify(rect));
    nativeModule.setTitleButtonWidth(rect.width);
  };

  aboutToDisappear(): void {
    // Page/ability teardown: release everything that was registered in
    // startNative(), so a same-process rebuild starts from a clean slate.
    try {
      if (this.themePollingId >= 0) {
        clearInterval(this.themePollingId);
        this.themePollingId = -1;
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'clear theme polling failed: %{public}s', JSON.stringify(e));
    }
    try {
      if (this.taskPollingId >= 0) {
        clearInterval(this.taskPollingId);
        this.taskPollingId = -1;
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'clear task polling failed: %{public}s', JSON.stringify(e));
    }
    try {
      const ctx = getContext(this) as common.UIAbilityContext;
      ctx.eventHub.off('hmhex_open_file', this.onOpenFileHandler);
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'eventHub off failed: %{public}s', JSON.stringify(e));
    }
    try {
      if (this.mainWindow !== null) {
        this.mainWindow.off('windowTitleButtonRectChange', this.onTitleRectHandler);
        this.mainWindow = null;
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'window off failed: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * Version tag of the bundled ImHex resources. Bump this whenever the
   * rawfile resources change so devices re-deploy them on next launch;
   * otherwise the deploy is skipped and startup stays fast.
   */
  private static readonly RESOURCE_VERSION: string = 'v2';

  private deployedFileCount: number = 0;

  private resourceFlagPath(): string {
    return (getContext(this) as common.UIAbilityContext).filesDir + '/imhex_resources.flag';
  }

  /**
   * True when the bundled resources were deployed by a previous launch with
   * the current RESOURCE_VERSION. Skips the (slow) full redeploy.
   *
   * Also verifies the patterns folder actually contains files: if a previous
   * deploy failed (or the sandbox data was cleaned), the flag alone must not
   * suppress the deploy forever, or the built-in pattern list comes up empty.
   */
  private resourcesDeployed(): boolean {
    try {
      const context = getContext(this) as common.UIAbilityContext;
      const patternsDir = context.filesDir + '/.local/share/imhex/patterns';
      try {
        const entries = fileIo.listFileSync(patternsDir);
        if (entries.length === 0) {
          return false;
        }
      } catch (e) {
        // listFileSync unavailable (older SDK) or directory missing: fall
        // back to a plain existence check.
        try {
          if (!fileIo.accessSync(patternsDir)) {
            return false;
          }
        } catch (e2) {
          return false;
        }
      }
    } catch (e) {
      return false;
    }
    try {
      const flag = fileIo.openSync(this.resourceFlagPath(), fileIo.OpenMode.READ_ONLY);
      const buf = new ArrayBuffer(64);
      const len = fileIo.readSync(flag.fd, buf);
      fileIo.closeSync(flag);
      const bytes = new Uint8Array(buf.slice(0, len));
      let text = '';
      for (let i = 0; i < bytes.length; i++) {
        text += String.fromCharCode(bytes[i]);
      }
      return text.trim() === Index.RESOURCE_VERSION;
    } catch (e) {
      return false;
    }
  }

  /**
   * Creates a directory tree level by level (no options argument, which
   * differs across SDK versions). Errors are tolerated (a directory may
   * already exist) but logged once so a failing mkdir is distinguishable
   * from a failing file write in the deploy logs.
   */
  private static s_mkdirErrorLogged: boolean = false;

  private mkdirs(dir: string): void {
    const parts: string[] = dir.split('/');
    let current: string = '';
    for (const part of parts) {
      if (part.length === 0) {
        continue;
      }
      current += '/' + part;
      try {
        fileIo.mkdirSync(current);
      } catch (e) {
        if (!Index.s_mkdirErrorLogged) {
          Index.s_mkdirErrorLogged = true;
          hilog.error(LOG_DOMAIN, TAG, 'mkdir failed for %{public}s: %{public}s', current, JSON.stringify(e));
        }
      }
    }
  }

  private deployDir(context: common.UIAbilityContext, baseDir: string, relDir: string): Promise<void> {
    return context.resourceManager.getRawFileList('imhex/' + relDir).then((entries: string[]) => {
      const tasks: Promise<void>[] = [];
      for (const entry of entries) {
        const rel: string = relDir + '/' + entry;
        tasks.push(context.resourceManager.getRawFileContent('imhex/' + rel).then((data: Uint8Array) => {
          try {
            const dest: string = baseDir + '/' + rel;
            const slash: number = dest.lastIndexOf('/');
            if (slash > 0) {
              this.mkdirs(dest.substring(0, slash));
            }
            const file = fileIo.openSync(dest,
              fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
            fileIo.writeSync(file.fd, data.buffer);
            fileIo.closeSync(file);
            this.deployedFileCount++;
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'Failed to deploy %{public}s: %{public}s', rel, JSON.stringify(e));
          }
        }).catch((err: BusinessError) => {
          // Reading a directory as content fails: recurse into it.
          return this.deployDir(context, baseDir, rel).catch((e: BusinessError) => {
            hilog.error(LOG_DOMAIN, TAG, 'deploy dir failed %{public}s: %{public}s', rel, JSON.stringify(e));
          });
        }));
      }
      return Promise.all(tasks).then(() => {});
    }).catch((err: BusinessError) => {
      hilog.error(LOG_DOMAIN, TAG, 'rawfile list failed for %{public}s: %{public}s', relDir, JSON.stringify(err));
    });
  }

  /**
   * Deploys the bundled ImHex resources (patterns, includes, constants,
   * encodings, magic, nodes, yara) from rawfile into the sandbox data
   * directory (~/.local/share/imhex). ImHex scans these folders at startup,
   * so the native side must not start before this finishes.
   *
   * getRawFileList is not recursive and returns directory names alongside
   * files, so each entry is first tried as a file; entries that fail to read
   * (directories) are recursed into.
   *
   * The whole deploy is skipped on later launches when the RESOURCE_VERSION
   * flag file is present, keeping startup fast.
   */
  private deployResources(): Promise<void> {
    const context = getContext(this) as common.UIAbilityContext;
    const baseDir: string = context.filesDir + '/.local/share/imhex';
    const subDirs: string[] = ['patterns', 'includes', 'constants', 'encodings', 'magic', 'nodes', 'yara'];

    this.deployedFileCount = 0;
    try {
      fileIo.mkdirSync(baseDir);
    } catch (e) {
      // Already exists (or benign error): keep going.
    }
    const allTasks: Promise<void>[] = [];
    for (const sub of subDirs) {
      allTasks.push(this.deployDir(context, baseDir, sub));
    }
    return Promise.all(allTasks).then(() => {
      hilog.info(LOG_DOMAIN, TAG, 'All ImHex resources deployed: %{public}d files', this.deployedFileCount);
      // Only mark the deploy as done when it actually produced files; a
      // failed/empty deploy must not suppress the next launch's retry.
      if (this.deployedFileCount === 0) {
        hilog.error(LOG_DOMAIN, TAG, 'deploy produced no files, NOT writing skip flag');
        return;
      }
      // Mark the deploy as done so the next launch skips it entirely.
      try {
        const flag = fileIo.openSync(this.resourceFlagPath(),
          fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
        const version = Index.RESOURCE_VERSION;
        const bytes = new Uint8Array(version.length);
        for (let i = 0; i < bytes.length; i++) {
          bytes[i] = version.charCodeAt(i);
        }
        fileIo.writeSync(flag.fd, bytes.buffer);
        fileIo.closeSync(flag);
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'resource flag write failed: %{public}s', JSON.stringify(e));
      }
    });
  }

  /**
   * Starts the native ImHex side (NAPI call into libentry.so).
   */
  private startNative(): void {
    try {
      const surfaceId: string = this.xcomponentController.getXComponentSurfaceId();
      hilog.info(LOG_DOMAIN, TAG, 'surfaceId: %{public}s', surfaceId);
      if (nativeModule === undefined) {
        hilog.error(LOG_DOMAIN, TAG, 'nativeModule is undefined');
        return;
      }
      const filesDir: string = (getContext(this) as common.UIAbilityContext).filesDir;
      hilog.info(LOG_DOMAIN, TAG, 'filesDir: %{public}s', filesDir);
      let densityDpi: number = 160;
      try {
        const d = display.getDefaultDisplaySync();
        densityDpi = d.densityDPI;
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'getDefaultDisplaySync failed: %{public}s', JSON.stringify(e));
      }
      hilog.info(LOG_DOMAIN, TAG, 'densityDPI: %{public}d', densityDpi);
      // Forward the system language before ImHex initializes so the UI
      // follows the system language (zh-CN etc.) when no explicit language
      // setting is stored.
      try {
        nativeModule.setSystemLanguage(i18n.getSystemLanguage());
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'setSystemLanguage failed: %{public}s', JSON.stringify(e));
      }
      nativeModule.startImHex(surfaceId, filesDir, densityDpi);
      hilog.info(LOG_DOMAIN, TAG, 'startImHex() returned');
      // "Open with HmHex" — two paths:
      // 1. Running instance: EntryAbility emits on the shared event hub.
      // 2. Cold start: the uri was stashed in AppStorage (consumed below).
      try {
        const ctx = getContext(this) as common.UIAbilityContext;
        ctx.eventHub.on('hmhex_open_file', this.onOpenFileHandler);
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'open-with eventHub subscribe failed: %{public}s', JSON.stringify(e));
      }
      try {
        const pendingUri: string | undefined = AppStorage.get<string>('pendingOpenFileUri');
        if (pendingUri !== undefined && pendingUri.length > 0) {
          AppStorage.setOrCreate('pendingOpenFileUri', '');
          hilog.error(LOG_DOMAIN, TAG, 'open-with (cold start): %{public}s', pendingUri);
          this.importFiles([pendingUri], true);
        }
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'open-with handling failed: %{public}s', JSON.stringify(e));
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'startImHex() threw: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * Logs the current window size. On 2in1 the XComponent surface and the
   * window size determine both the rendering resolution and (indirectly) the
   * mouse coordinate transform that the ArkUI input system relies on.
   */
  private logWindowSize(): void {
    try {
      const context = getContext(this) as common.UIAbilityContext;
      window.getLastWindow(context).then((win: window.Window) => {
        const props = win.getWindowProperties();
        const r = props.windowRect;
        hilog.info(LOG_DOMAIN, TAG, 'window size: %{public}d x %{public}d',
          r.width, r.height);
      }).catch((err: BusinessError) => {
        hilog.error(LOG_DOMAIN, TAG, 'getLastWindow failed: %{public}s', JSON.stringify(err));
      });
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'logWindowSize threw: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * Opens the system file picker (DocumentViewPicker), copies the selected
   * file(s) into the sandbox (native code cannot fopen picker URIs) and hands
   * the sandbox paths to the native side.
   */
  private openWithPicker(multiple: boolean): void {
    const context = getContext(this) as common.UIAbilityContext;
    const documentPicker = new picker.DocumentViewPicker(context);
    const options = new picker.DocumentSelectOptions();
    options.maxSelectNumber = multiple ? 10 : 1;
    documentPicker.select(options).then((uris: string[]) => {
      if (uris.length === 0) {
        hilog.info(LOG_DOMAIN, TAG, 'picker: no files selected');
        // Drop any pending dialog callback so it can never fire later.
        try {
          nativeModule.cancelFileDialog();
        } catch (e) {
          hilog.error(LOG_DOMAIN, TAG, 'cancelFileDialog failed: %{public}s', JSON.stringify(e));
        }
        return;
      }
      this.importFiles(uris);
    }).catch((err: BusinessError) => {
      hilog.error(LOG_DOMAIN, TAG, 'picker failed: %{public}s', JSON.stringify(err));
    });
  }

  /**
   * Copies picked URIs into filesDir/opened_files and forwards each path to
   * the native side (ImHex opens it as a provider).
   *
   * @param dropped true when the file came from a drag & drop: it opens via
   *        RequestOpenFile directly, independent of any pending file dialog
   *        callback; false for the system picker (dialog callback path).
   */
  private importFiles(uris: string[], dropped: boolean = false): void {
    const context = getContext(this) as common.UIAbilityContext;
    const destDir: string = context.filesDir + '/opened_files';
    this.mkdirs(destDir);
    const bufSize = 1024 * 1024;
    for (const uri of uris) {
      if (uri === undefined || uri.length === 0) {
        hilog.error(LOG_DOMAIN, TAG, 'import: empty uri skipped');
        continue;
      }
      try {
        const src = fileIo.openSync(uri, fileIo.OpenMode.READ_ONLY);
        let name: string = src.name && src.name.length > 0 ? src.name : ('file_' + Date.now());
        // Media URIs (file://media/...) can yield names that are not plain
        // basenames; strip any directory part so the dest path stays sane.
        const slash: number = name.lastIndexOf('/');
        if (slash >= 0) {
          name = name.substring(slash + 1);
        }
        if (name.length === 0) {
          name = 'file_' + Date.now();
        }
        const dest: string = destDir + '/' + name;
        const out = fileIo.openSync(dest,
          fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
        const buf = new ArrayBuffer(bufSize);
        let read = 0;
        do {
          read = fileIo.readSync(src.fd, buf);
          if (read > 0) {
            fileIo.writeSync(out.fd, buf.slice(0, read));
          }
        } while (read > 0);
        fileIo.closeSync(src);
        fileIo.closeSync(out);
        hilog.info(LOG_DOMAIN, TAG, 'imported %{public}s -> %{public}s', uri, dest);
        if (dropped) {
          nativeModule.openFileDropped(dest);
        } else {
          nativeModule.openFile(dest);
        }
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'import failed for %{public}s: %{public}s', uri, JSON.stringify(e));
      }
    }
  }

  /**
   * Receives files dragged in from the system file manager (2in1 drag & drop)
   * and imports them through the same pipeline as the picker. The drop may be
   * delivered to both the XComponent and the root Column, so repeated
   * deliveries of the same drop within a short window are deduplicated.
   */
  private lastDropAt: number = 0;

  private handleFileDrop(event: DragEvent): void {
    try {
      const now = Date.now();
      if (now - this.lastDropAt < 500) {
        hilog.error(LOG_DOMAIN, TAG, 'drop: duplicate delivery ignored');
        return;
      }
      this.lastDropAt = now;
      const data = event.getData();
      if (data === undefined || data === null) {
        hilog.error(LOG_DOMAIN, TAG, 'drop: no data');
        return;
      }
      const records = data.getRecords();
      const uris: string[] = [];
      for (const record of records) {
        let recordType = 'unknown';
        try {
          recordType = record.getType();
        } catch (e) {
          hilog.error(LOG_DOMAIN, TAG, 'drop: getType threw: %{public}s', JSON.stringify(e));
        }
        if (record instanceof unifiedDataChannel.File) {
          // Image/Video extend File; media records may carry no uri (pure
          // pixel data) — those cannot be imported, log and skip them.
          let uri = '';
          try {
            uri = record.uri;
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'drop: uri getter threw: %{public}s', JSON.stringify(e));
          }
          if (uri !== undefined && uri.length > 0) {
            uris.push(uri);
            hilog.error(LOG_DOMAIN, TAG, 'drop: file uri=%{public}s type=%{public}s', uri, recordType);
          } else {
            hilog.error(LOG_DOMAIN, TAG, 'drop: file record without uri ignored, type=%{public}s', recordType);
          }
        } else if (record instanceof unifiedDataChannel.SystemDefinedRecord) {
          // Files dragged from the system file manager are often delivered
          // as system-defined records (e.g. images as SystemDefinedPixelMap)
          // instead of File; the file reference lives in `details.uri`.
          let uri = '';
          try {
            const details = record.details;
            if (details !== undefined) {
              const v = details['uri'];
              if (typeof v === 'string' && v.length > 0) {
                uri = v;
              }
            }
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'drop: details read threw: %{public}s', JSON.stringify(e));
          }
          if (uri.length > 0) {
            uris.push(uri);
            hilog.error(LOG_DOMAIN, TAG, 'drop: system record uri=%{public}s type=%{public}s', uri, recordType);
          } else {
            hilog.error(LOG_DOMAIN, TAG, 'drop: system record without uri, type=%{public}s details=%{public}s',
              recordType, JSON.stringify(record.details));
          }
        } else {
          // Folders cannot be imported as providers; tell the user instead
          // of failing silently.
          if (recordType.includes('folder') || recordType.includes('directory')) {
            hilog.error(LOG_DOMAIN, TAG, 'drop: folder dropped, type=%{public}s', recordType);
            try {
              promptAction.showToast({ message: '不支持拖入文件夹' });
            } catch (e) {
              hilog.error(LOG_DOMAIN, TAG, 'drop: toast failed: %{public}s', JSON.stringify(e));
            }
            continue;
          }

          // Types like general.image / general.video arrive as a base
          // UnifiedRecord whose value object carries the original file uri
          // (with drag-drop read permission). Try the known key names so
          // images, videos and other system records open like any file.
          let uri = '';
          try {
            const value = record.getValue();
            if (value !== null && value !== undefined && typeof value === 'object') {
              const obj = value as Record<string, Object | string | number | boolean>;
              const candidates = ['oriUri', 'uri', 'imageUri', 'videoUri', 'fileUri'];
              for (const key of candidates) {
                const v = obj[key];
                if (typeof v === 'string' && v.length > 0) {
                  uri = v;
                  break;
                }
              }
            }
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'drop: value uri extraction threw: %{public}s', JSON.stringify(e));
          }
          if (uri.length > 0) {
            uris.push(uri);
            hilog.error(LOG_DOMAIN, TAG, 'drop: record uri=%{public}s type=%{public}s', uri, recordType);
          } else {
            hilog.error(LOG_DOMAIN, TAG, 'drop: record without extractable uri, type=%{public}s', recordType);
          }
        }
      }
      if (uris.length > 0) {
        this.importFiles(uris, true);
      } else {
        hilog.error(LOG_DOMAIN, TAG, 'drop: no importable files');
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'drop handler threw: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * Opens the system save picker (DocumentViewPicker.save) and hands the
   * picked file's fd to the native side, which writes the current provider
   * data into it.
   */
  private openSavePicker(): void {
    const context = getContext(this) as common.UIAbilityContext;
    const documentPicker = new picker.DocumentViewPicker(context);
    const options = new picker.DocumentSaveOptions();
    options.newFileNames = ['unnamed.bin'];
    documentPicker.save(options).then((uris: string[]) => {
      if (uris.length === 0) {
        hilog.info(LOG_DOMAIN, TAG, 'save picker: cancelled');
        // Drop any pending save callback so it can never fire later.
        try {
          nativeModule.cancelFileDialog();
        } catch (e) {
          hilog.error(LOG_DOMAIN, TAG, 'cancelFileDialog failed: %{public}s', JSON.stringify(e));
        }
        return;
      }
      try {
        const file = fileIo.openSync(uris[0],
          fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
        // The fd is owned by the native side from here on (it closes it).
        nativeModule.saveToFd(file.fd);
        hilog.info(LOG_DOMAIN, TAG, 'saveToFd called with fd %{public}d', file.fd);
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'save open failed: %{public}s', JSON.stringify(e));
      }
    }).catch((err: BusinessError) => {
      hilog.error(LOG_DOMAIN, TAG, 'save picker failed: %{public}s', JSON.stringify(err));
    });
  }

  /**
   * Bridges ImHex's internal clipboard to the system pasteboard:
   * - write direction: native writes are forwarded here and stored into the
   *   system pasteboard, so Ctrl+C puts ImHex content into the OS clipboard.
   * - read direction: glfwGetClipboardString asks for the current system
   *   pasteboard content; this callback reads it and mirrors it back into
   *   the native cache, so Ctrl+V pastes content copied in other apps.
   *   Requires ohos.permission.READ_PASTEBOARD (ACL, granted via the
   *   AGC-signed profile; requested at startup below).
   */
  /**
   * Polls the system dark/light mode every 2 seconds and forwards changes to
   * the native side. onConfigurationUpdate does not fire reliably for
   * colorMode changes on this SDK, so polling is the dependable fallback.
   */
  private startThemePolling(): void {
    const context = getContext(this) as common.UIAbilityContext;
    let lastCm = -2;
    this.themePollingId = setInterval(() => {
      try {
        const cm = context.config.colorMode;
        if (cm !== undefined && cm !== lastCm) {
          lastCm = cm;
          if (cm === ConfigurationConstant.ColorMode.COLOR_MODE_DARK ||
              cm === ConfigurationConstant.ColorMode.COLOR_MODE_LIGHT) {
            nativeModule.notifySystemTheme(cm === ConfigurationConstant.ColorMode.COLOR_MODE_DARK ? 1 : 0);
          }
        }
      } catch (e) {
        // Ignore transient errors; the next tick retries.
      }
    }, 2000);
  }

  /**
   * True once the user chose "no notifications needed" on a previous launch
   * (persisted via a marker file so the dialog is not shown again).
   */
  private notificationDeclined(): boolean {
    try {
      const context = getContext(this) as common.UIAbilityContext;
      return fileIo.accessSync(context.filesDir + '/.notif_declined');
    } catch (e) {
      return false;
    }
  }

  private markNotificationDeclined(): void {
    try {
      const context = getContext(this) as common.UIAbilityContext;
      const f = fileIo.openSync(context.filesDir + '/.notif_declined',
        fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
      fileIo.closeSync(f);
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'markNotificationDeclined failed: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * Requests notification permission with an in-app explanation dialog
   * first (the system permission dialog itself cannot be customized).
   */
  private requestNotificationPermission(): void {
    const context = getContext(this) as common.UIAbilityContext;
    try {
      notificationManager.isNotificationEnabled().then((enabled: boolean) => {
        if (enabled || this.notificationDeclined()) {
          return;
        }
        this.notifDialogController = new CustomDialogController({
          builder: NotificationExplainDialog({
            onConfirm: () => {
              notificationManager.requestEnableNotification(context).catch((err: BusinessError) => {
                hilog.error(LOG_DOMAIN, TAG, 'requestEnableNotification failed: %{public}s', JSON.stringify(err));
              });
            },
            onDecline: () => {
              this.markNotificationDeclined();
            }
          }),
          autoCancel: false,
          alignment: DialogAlignment.Center
        });
        this.notifDialogController.open();
      }).catch((err: BusinessError) => {
        hilog.error(LOG_DOMAIN, TAG, 'isNotificationEnabled failed: %{public}s', JSON.stringify(err));
      });
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'requestNotificationPermission threw: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * Polls the number of running ImHex background tasks and posts a system
   * notification when a task finishes (ImHex has no task-finished event;
   * getRunningTaskCount is the available signal).
   */
  private startTaskNotificationPolling(): void {
    let wasBusy = false;
    let hasWorked = false;
    this.taskPollingId = setInterval(() => {
      try {
        const count: number = nativeModule.getRunningTaskCount();
        const busy = count > 0;
        if (busy) {
          hasWorked = true;
        } else if (wasBusy && hasWorked) {
          // Transition busy -> idle: a background task just finished.
          hasWorked = false;
          try {
            const request: notificationManager.NotificationRequest = {
              id: 1,
              content: {
                notificationContentType: notificationManager.ContentType.NOTIFICATION_CONTENT_BASIC_TEXT,
                normal: {
                  title: 'HmHex 任务完成',
                  text: '后台任务已完成'
                }
              }
            };
            notificationManager.publish(request).catch((err: BusinessError) => {
              hilog.error(LOG_DOMAIN, TAG, 'publish notification failed: %{public}s', JSON.stringify(err));
            });
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'publish notification threw: %{public}s', JSON.stringify(e));
          }
        }
        wasBusy = busy;
      } catch (e) {
        // Ignore transient errors; next tick retries.
      }
    }, 2000);
  }

  private setupClipboardBridge(): void {
    try {
      const systemPasteboard = pasteboard.getSystemPasteboard();

      nativeModule.setClipboardCallback((text: string) => {
        hilog.info(LOG_DOMAIN, TAG, 'clipboard write: %{public}s', text.substring(0, 64));
        try {
          const pasteData = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, text);
          systemPasteboard.setData(pasteData).catch((err: BusinessError) => {
            hilog.error(LOG_DOMAIN, TAG, 'pasteboard setData failed: %{public}s', JSON.stringify(err));
          });
        } catch (e) {
          hilog.error(LOG_DOMAIN, TAG, 'clipboard write threw: %{public}s', JSON.stringify(e));
        }
      });

      // Read-through: invoked from glfwGetClipboardString on the render
      // thread. Read the system pasteboard asynchronously and push the text
      // back via updateClipboardCache.
      nativeModule.setClipboardReadCallback(() => {
        systemPasteboard.getData().then((pasteData: pasteboard.PasteData) => {
          const record = pasteData.getRecordAt(0);
          if (record === undefined) {
            return;
          }
          const text: string = record.toPlainText();
          if (text.length > 0) {
            nativeModule.updateClipboardCache(text);
          }
          hilog.error(LOG_DOMAIN, TAG, 'clipboard read: %{public}s', text.substring(0, 64));
        }).catch((err: BusinessError) => {
          hilog.error(LOG_DOMAIN, TAG, 'clipboard getData failed: %{public}s', JSON.stringify(err));
        });
      });

      hilog.info(LOG_DOMAIN, TAG, 'clipboard bridge ready (read+write)');
    } catch (e) {
      hilog.error(LOG_DOMAIN, TAG, 'clipboard bridge failed: %{public}s', JSON.stringify(e));
    }
  }

  build() {
    Column() {
      XComponent({ id: 'imhex', type: 'surface', libraryname: 'entry', controller: this.xcomponentController })
        .focusable(true)
        .onLoad(() => {
          hilog.info(LOG_DOMAIN, TAG, 'XComponent onLoad fired');
          this.logWindowSize();
          // Register the file-open bridge: ImHex's openFileBrowser() notifies
          // this callback, which opens the system picker and imports the file.
          try {
            nativeModule.setFileOpenCallback((multiple: number) => {
              hilog.info(LOG_DOMAIN, TAG, 'file open requested, multiple=%{public}d', multiple);
              this.openWithPicker(multiple > 0);
            });
            hilog.info(LOG_DOMAIN, TAG, 'setFileOpenCallback registered');
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'setFileOpenCallback failed: %{public}s', JSON.stringify(e));
          }
          // Register the save bridge: ImHex's save dialog notifies this
          // callback, which opens the system save picker and passes the
          // picked file's fd back to the native side.
          try {
            nativeModule.setSaveFileCallback(() => {
              hilog.info(LOG_DOMAIN, TAG, 'save requested');
              this.openSavePicker();
            });
            hilog.info(LOG_DOMAIN, TAG, 'setSaveFileCallback registered');
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'setSaveFileCallback failed: %{public}s', JSON.stringify(e));
          }
          // Always-on-top toggle from ImHex's settings: apply it via the
          // system window API.
          try {
            nativeModule.setTopmostCallback((topmost: number) => {
              // ERROR level: INFO logs are filtered on this system.
              hilog.error(LOG_DOMAIN, TAG, 'set topmost: %{public}d', topmost);
              const context = getContext(this) as common.UIAbilityContext;
              window.getLastWindow(context).then((win: window.Window) => {
                win.setWindowTopmost(topmost > 0).then(() => {
                  hilog.error(LOG_DOMAIN, TAG, 'setWindowTopmost OK=%{public}d', topmost > 0 ? 1 : 0);
                }).catch((err: BusinessError) => {
                  hilog.error(LOG_DOMAIN, TAG, 'setWindowTopmost rejected: %{public}s', JSON.stringify(err));
                });
              }).catch((err: BusinessError) => {
                hilog.error(LOG_DOMAIN, TAG, 'getLastWindow failed: %{public}s', JSON.stringify(err));
              });
            });
            hilog.info(LOG_DOMAIN, TAG, 'setTopmostCallback registered');
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'setTopmostCallback failed: %{public}s', JSON.stringify(e));
          }
          this.setupClipboardBridge();
          this.startThemePolling();
          this.requestNotificationPermission();
          this.startTaskNotificationPolling();
          // Initial system dark/light mode: the native side applies it only
          // when the "Native" theme is selected. Send immediately AND again
          // after ImHex finished initializing — the first notification may
          // arrive before the plugin registered the RequestChangeTheme
          // subscriber, in which case the event is dropped.
          try {
            const context = getContext(this) as common.UIAbilityContext;
            const cm = context.config.colorMode;
            if (cm === ConfigurationConstant.ColorMode.COLOR_MODE_DARK ||
                cm === ConfigurationConstant.ColorMode.COLOR_MODE_LIGHT) {
              const dark = cm === ConfigurationConstant.ColorMode.COLOR_MODE_DARK ? 1 : 0;
              nativeModule.notifySystemTheme(dark);
              setTimeout(() => {
                try {
                  nativeModule.notifySystemTheme(dark);
                } catch (e2) {
                  hilog.error(LOG_DOMAIN, TAG, 'delayed theme notify failed: %{public}s', JSON.stringify(e2));
                }
              }, 3000);
            }
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'initial theme notify failed: %{public}s', JSON.stringify(e));
          }
          // Request the ACL clipboard-read permission. On PC/2in1 the first
          // request is granted silently by default (Settings > Privacy).
          try {
            const context = getContext(this) as common.UIAbilityContext;
            const atManager = abilityAccessCtrl.createAtManager();
            atManager.requestPermissionsFromUser(context, ['ohos.permission.READ_PASTEBOARD'])
              .then((result) => {
                hilog.error(LOG_DOMAIN, TAG, 'READ_PASTEBOARD request result: %{public}s', JSON.stringify(result));
              }).catch((err: BusinessError) => {
                hilog.error(LOG_DOMAIN, TAG, 'READ_PASTEBOARD request failed: %{public}s', JSON.stringify(err));
              });
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'READ_PASTEBOARD request threw: %{public}s', JSON.stringify(e));
          }
          // ImHex finished shutting down (File -> Exit ImHex): terminate the
          // ability so the app actually exits instead of freezing.
          try {
            nativeModule.setExitAppCallback(() => {
              hilog.error(LOG_DOMAIN, TAG, 'ImHex exited, terminating ability');
              try {
                const context = getContext(this) as common.UIAbilityContext;
                context.terminateSelf();
              } catch (e) {
                hilog.error(LOG_DOMAIN, TAG, 'terminateSelf failed: %{public}s', JSON.stringify(e));
              }
            });
            hilog.info(LOG_DOMAIN, TAG, 'setExitAppCallback registered');
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'setExitAppCallback failed: %{public}s', JSON.stringify(e));
          }
          // ImHex's openWebpage() (help menu, feedback, about page links...)
          // opens the URL with the system browser via startAbility.
          try {
            nativeModule.setOpenWebpageCallback((url: string) => {
              hilog.error(LOG_DOMAIN, TAG, 'open webpage: %{public}s', url);
              try {
                const context = getContext(this) as common.UIAbilityContext;
                const want: Want = {
                  action: 'ohos.want.action.viewData',
                  entities: ['entity.system.browsable'],
                  uri: url
                };
                context.startAbility(want).then(() => {
                  hilog.error(LOG_DOMAIN, TAG, 'browser started');
                }).catch((err: BusinessError) => {
                  hilog.error(LOG_DOMAIN, TAG, 'startAbility failed: %{public}s', JSON.stringify(err));
                });
              } catch (e) {
                hilog.error(LOG_DOMAIN, TAG, 'open webpage threw: %{public}s', JSON.stringify(e));
              }
            });
            hilog.info(LOG_DOMAIN, TAG, 'setOpenWebpageCallback registered');
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'setOpenWebpageCallback failed: %{public}s', JSON.stringify(e));
          }
          // ImGui cursor shape changes (text selection, resize, ...) are
          // applied as system pointer styles.
          try {
            let cursorWindowId: number = 0;
            const context = getContext(this) as common.UIAbilityContext;
            window.getLastWindow(context).then((win: window.Window) => {
              cursorWindowId = win.getWindowProperties().id;
            }).catch((err: BusinessError) => {
              hilog.error(LOG_DOMAIN, TAG, 'getLastWindow for cursor failed: %{public}s', JSON.stringify(err));
            });
            nativeModule.setCursorCallback((shape: number) => {
              if (cursorWindowId === 0) {
                return;
              }
              let style: pointer.PointerStyle = pointer.PointerStyle.DEFAULT;
              switch (shape) {
                case 0x36002: style = pointer.PointerStyle.TEXT_CURSOR; break;      // GLFW_IBEAM_CURSOR
                case 0x36003: style = pointer.PointerStyle.CROSS; break;            // GLFW_CROSSHAIR_CURSOR
                case 0x36004: style = pointer.PointerStyle.HAND_POINTING; break;    // GLFW_HAND_CURSOR
                case 0x36005: style = pointer.PointerStyle.RESIZE_LEFT_RIGHT; break;// GLFW_HRESIZE_CURSOR
                case 0x36006: style = pointer.PointerStyle.RESIZE_UP_DOWN; break;   // GLFW_VRESIZE_CURSOR
                default: style = pointer.PointerStyle.DEFAULT; break;
              }
              try {
                pointer.setPointerStyleSync(cursorWindowId, style);
              } catch (e) {
                hilog.error(LOG_DOMAIN, TAG, 'setPointerStyle failed: %{public}s', JSON.stringify(e));
              }
            });
            hilog.info(LOG_DOMAIN, TAG, 'setCursorCallback registered');
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'setCursorCallback failed: %{public}s', JSON.stringify(e));
          }
          // Immersive mode: track the system caption button area (minimize/
          // maximize/close) so ImHex's own title bar buttons shift left and
          // are not covered by them.
          try {
            const context = getContext(this) as common.UIAbilityContext;
            window.getLastWindow(context).then((win: window.Window) => {
              this.mainWindow = win;
              win.on('windowTitleButtonRectChange', this.onTitleRectHandler);
            }).catch((err: BusinessError) => {
              hilog.error(LOG_DOMAIN, TAG, 'getLastWindow for title rect failed: %{public}s', JSON.stringify(err));
            });
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'title rect listener failed: %{public}s', JSON.stringify(e));
          }
          if (this.resourcesDeployed()) {
            hilog.info(LOG_DOMAIN, TAG, 'resources already deployed, skipping');
            this.startNative();
          } else {
            this.deployResources().then(() => {
              this.startNative();
            });
          }
        })
        .onTouch((event: TouchEvent) => {
          // Input forwarding happens on the root Column; the XComponent surface
          // swallows its own events unless an inputMode is configured.
        })
        .onMouse((event: MouseEvent) => {
        })
        .onClick((event: ClickEvent) => {
        })
        .onHover((isHover: boolean, event: HoverEvent) => {
        })
        .onKeyEvent((event: KeyEvent) => {
          // Consume the event so it is not re-dispatched by the system
          // (unconsumed events get repeated, which can wedge key state).
          return true;
        })
        .onAxisEvent((event: AxisEvent) => {
          const vx = event.getHorizontalAxisValue();
          const vy = event.getVerticalAxisValue();
          hilog.info(LOG_DOMAIN, TAG, 'xc axis %{public}f,%{public}f', vx, vy);
          nativeModule.onScroll(vx, vy);
        })
        .onFocus(() => {
          hilog.info(LOG_DOMAIN, TAG, 'XComponent focused');
        })
        .onBlur(() => {
          hilog.info(LOG_DOMAIN, TAG, 'XComponent blurred');
          try {
            nativeModule.resetKeys();
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'resetKeys failed: %{public}s', JSON.stringify(e));
          }
        })
        .onDrop((event: DragEvent) => {
          this.handleFileDrop(event);
        })
        .onAreaChange((_oldValue: Area, _newValue: Area) => {
          // Window drag/resize: during a system window drag the mouse events
          // are consumed by the window manager, so ImHex's idle frame rate
          // logic sees no interaction and drops to ~5 FPS, making the resize
          // look frozen until the mouse is released. Keep rendering instead.
          try {
            nativeModule.unlockFrameRate();
          } catch (e) {
            hilog.error(LOG_DOMAIN, TAG, 'unlockFrameRate failed: %{public}s', JSON.stringify(e));
          }
        })
        .width('100%')
        .height('100%')
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#000000')
    // Root-level event forwarding: the XComponent surface swallows its own
    // input events when no inputMode is configured, so also attach the
    // handlers to the page root and forward everything to the native side.
    .onTouch((event: TouchEvent) => {
      if (event.touches.length > 0) {
        const touch = event.touches[0];
        nativeModule.onTouch(touch.x, touch.y, event.type);
      }
    })
    .onMouse((event: MouseEvent) => {
      nativeModule.onMouse(event.x, event.y, event.action, event.button);
    })
    .onAxisEvent((event: AxisEvent) => {
      nativeModule.onScroll(event.getHorizontalAxisValue(), event.getVerticalAxisValue());
    })
    .onDrop((event: DragEvent) => {
      this.handleFileDrop(event);
    })
    .onKeyEvent((event: KeyEvent) => {
      nativeModule.onKey(event.keyCode, event.type);
      // Consume the event (return true): an unconsumed event may be
      // re-dispatched by the system, and must not bubble to other windows.
      return true;
    })
    .onFocus(() => {
      hilog.info(LOG_DOMAIN, TAG, 'root focused');
    })
    .onBlur(() => {
      hilog.info(LOG_DOMAIN, TAG, 'root blurred');
      try {
        nativeModule.resetKeys();
      } catch (e) {
        hilog.error(LOG_DOMAIN, TAG, 'resetKeys failed: %{public}s', JSON.stringify(e));
      }
    })
    .focusable(true)
    .defaultFocus(true)
  }
}
