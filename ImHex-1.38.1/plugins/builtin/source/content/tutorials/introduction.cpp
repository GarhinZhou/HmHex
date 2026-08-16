#include <content/providers/memory_file_provider.hpp>
#include <hex/api/shortcut_manager.hpp>
#include <hex/api/tutorial_manager.hpp>
#include <hex/ui/view.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/events/events_gui.hpp>

namespace hex::plugin::builtin {

    void registerIntroductionTutorial() {
        using enum TutorialManager::Position;
        // Tutorial text is kept as plain Chinese literals: the tutorial is
        // not translated via Lang keys, and mixing untranslated Lang keys
        // breaks both the displayed text and the highlight matching in
        // non-English UIs. The highlight ID chains below use Lang() entries
        // that follow the UI language and are matched by resolved label.
        auto &tutorial = TutorialManager::createTutorial("hex.builtin.tutorial.introduction", "这个教程将带你了解 HmHex 的基本用法，帮助你快速上手。");

        {
            tutorial.addStep()
                .setMessage(
                    "欢迎使用 HmHex！",
                    "HmHex 是一款逆向工程套件和十六进制编辑器，专注于将二进制数据可视化，便于理解和分析。\n\n点击下方的右箭头按钮即可进入下一步。",
                    Bottom | Right
                )
                .allowSkip();
        }

        {
            auto &step = tutorial.addStep();

            step.setMessage(
                "打开数据",
                "HmHex 支持从多种来源加载数据，包括文件、原始磁盘、其他进程的内存等等。\n\n这些选项都可以在欢迎界面或“文件”菜单中找到。",
                Bottom | Right
            )
            .addHighlight("让我们点击“新建文件”按钮，创建一个新的空白文件。",
            {
                // Step-wise ImGui ID chain (window -> start subwindow ->
                // button label), mirroring how ImGui derives the button's
                // real ID. All parts follow the UI language, so the highlight
                // matches the localized button in any language.
                "Welcome Screen",
                Lang("hex.builtin.welcome.header.start"),
                Lang("hex.builtin.welcome.start.create_file")
            })
            .onAppear([&step] {
                // Token-based subscribe: EventManager deduplicates the same
                // token and unsubscribe(&step) below pairs with it, so the
                // subscription is properly cleaned up even when the step is
                // skipped (onComplete never runs in that case).
                EventProviderOpened::subscribe(&step, [&step](prv::Provider *provider) {
                    if (dynamic_cast<MemoryFileProvider*>(provider))
                        step.complete();
                });
            })
            .onComplete([&step] {
                EventProviderOpened::unsubscribe(&step);
            });
        }

        {
            tutorial.addStep()
            .addHighlight("这是十六进制编辑器，用于显示已加载数据的每一个字节，你也可以双击某个字节进行编辑。\n\n你可以使用方向键或鼠标滚轮浏览数据。", {
                View::toWindowName("hex.builtin.view.hex_editor.name")
            })
            .allowSkip();
        }

        {
            tutorial.addStep()
            .addHighlight("这是数据检查器，它会以更易读的格式显示当前选中字节的数据。\n\n你也可以在这里双击某一行来编辑数据。", {
                View::toWindowName("hex.builtin.view.data_inspector.name")
            })
            .onAppear([]{
                ImHexApi::HexEditor::setSelection(Region { 0, 1 });
            })
            .allowSkip();
        }

        {
            tutorial.addStep()
            .addHighlight("这是模式编辑器。你可以使用模式语言编写代码，对已加载数据中的二进制数据结构进行高亮显示和解码。\n\n你可以在文档中了解更多关于模式语言的内容。", {
                View::toWindowName("hex.builtin.view.pattern_editor.name")
            })
            .addHighlight("该视图包含一个树状视图，用于展示你使用模式语言定义的数据结构。", {
                View::toWindowName("hex.builtin.view.pattern_data.name")
            })
            .onAppear([] {
                RequestSetPatternLanguageCode::post("\n\n\n\n\n\nstruct Test {\n    u8 value;\n};\n\nTest test @ 0x00;");
                RequestTriggerPatternEvaluation::post();
            })
            .allowSkip();
        }

        {
            auto &step = tutorial.addStep();

            step.addHighlight("你可以在“帮助”菜单中找到更多教程和文档。", {
                "##MainMenuBar",
                "##MenuBar",
                Lang("hex.builtin.menu.help")
            })
            .addHighlight({
                "###Menu_00",
                Lang("hex.builtin.view.tutorials.name")
            })
            .onAppear([&step] {
                // Token-based subscribe (deduplicated + paired unsubscribe),
                // so no subscription leaks even when the step is skipped.
                EventViewOpened::subscribe(&step, [&step](const View *view){
                    if (view->getUnlocalizedName() == UnlocalizedString("hex.builtin.view.tutorials.name"))
                        step.complete();
                });
            })
            .onComplete([&step]{
                EventViewOpened::unsubscribe(&step);
            })
            .allowSkip();
        }
    }

}
