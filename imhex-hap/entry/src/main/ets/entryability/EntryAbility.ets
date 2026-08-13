import { AbilityConstant, ConfigurationConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(0x0000, 'HmHex', 'Ability onCreate');
  }

  onDestroy(): void {
    hilog.info(0x0000, 'HmHex', 'Ability onDestroy');
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
      this.context.setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_DARK);
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
