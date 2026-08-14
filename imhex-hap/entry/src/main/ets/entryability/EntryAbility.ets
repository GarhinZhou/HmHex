import { AbilityConstant, ConfigurationConstant, UIAbility, Want, Configuration } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import nativeModule from 'libentry.so';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(0x0000, 'HmHex', 'Ability onCreate');
    this.handleOpenWithWant(want);
  }

  /**
   * "Open with HmHex" from the system file manager: the picked file arrives
   * as a file:// uri in the want. Stash it in AppStorage for cold starts
   * (consumed by Index.ets after the XComponent loads) AND emit it on the
   * shared event hub for already-running instances (Index.ets subscribes on
   * load).
   */
  private handleOpenWithWant(want: Want): void {
    try {
      // Only react to explicit "open file" intents; ignore home launches etc.
      if (want.action !== 'ohos.want.action.viewData') {
        return;
      }
      const uri: string = want.uri ?? '';
      if (uri.length > 0 && uri.startsWith('file://')) {
        AppStorage.setOrCreate('pendingOpenFileUri', uri);
        this.context.eventHub.emit('hmhex_open_file', uri);
        hilog.error(0x0000, 'HmHex', 'open-with uri: %{public}s', uri);
      }
    } catch (e) {
      hilog.error(0x0000, 'HmHex', 'handleOpenWithWant failed: %{public}s', JSON.stringify(e));
    }
  }

  /**
   * App is already running when a file is opened via "Open with HmHex":
   * forward the uri the same way (Index.ets picks it up from AppStorage).
   */
  onNewWant(want: Want): void {
    hilog.info(0x0000, 'HmHex', 'Ability onNewWant');
    this.handleOpenWithWant(want);
  }

  onDestroy(): void {
    hilog.info(0x0000, 'HmHex', 'Ability onDestroy');
  }

  /**
   * System dark/light mode changes: forward them to the native side so the
   * "Native" theme (Settings -> Interface -> Color) follows the system.
   */
  onConfigurationUpdate(newConfig: Configuration): void {
    try {
      const cm = newConfig.colorMode;
      if (cm === ConfigurationConstant.ColorMode.COLOR_MODE_DARK ||
          cm === ConfigurationConstant.ColorMode.COLOR_MODE_LIGHT) {
        nativeModule.notifySystemTheme(cm === ConfigurationConstant.ColorMode.COLOR_MODE_DARK ? 1 : 0);
      }
    } catch (e) {
      hilog.error(0x0000, 'HmHex', 'theme notify failed: %{public}s', JSON.stringify(e));
    }
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(0x0000, 'HmHex', 'Failed to load content: %{public}s', JSON.stringify(err));
        return;
      }
      hilog.info(0x0000, 'HmHex', 'Content loaded');
      this.setupWindow(windowStage);
    });
  }

  /**
   * Immersive window: hides the system title bar but keeps the caption
   * buttons (minimize/maximize/close) floating over the top-right corner.
   * The content extends into the former title bar area. The caption button
   * width is forwarded to the native side so ImHex's own title bar buttons
   * move out of the way.
   */
  private setupWindow(windowStage: window.WindowStage): void {
    try {
      const win = windowStage.getMainWindowSync();
      // Follow the system dark/light mode (needed for the "Native" theme);
      // the window decorations stay dark to match ImHex's default UI.
      this.context.setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_NOT_SET);
      const dark = ConfigurationConstant.ColorMode.COLOR_MODE_DARK;
      win.setWindowDecorVisible(false);
      win.setWindowDecorHeight(56);
      // Smaller caption buttons, snug against the top-right corner: negative
      // right margin pulls them further into the corner, tighter spacing and
      // smaller hover background keep the group compact.
      win.setDecorButtonStyle({
        colorMode: dark,
        buttonIconSize: 18,
        closeButtonRightMargin: -8,
        spacingBetweenButtons: 6,
        buttonBackgroundSize: 26
      });
      win.setWindowBackgroundColor('#000000');
      // Note: always-on-top is NOT forced here anymore; the persisted
      // ImHex setting (Settings -> General -> Always on top) is re-applied
      // after startup via setTopmostCallback.
      hilog.info(0x0000, 'HmHex', 'immersive window configured');
    } catch (e) {
      hilog.error(0x0000, 'HmHex', 'window setup failed: %{public}s', JSON.stringify(e));
    }
  }

  onWindowStageDestroy(): void {
    hilog.info(0x0000, 'HmHex', 'Window stage destroyed');
  }

  onForeground(): void {
    hilog.info(0x0000, 'HmHex', 'Ability foreground');
  }

  onBackground(): void {
    hilog.info(0x0000, 'HmHex', 'Ability background');
  }
}
