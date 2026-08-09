#include <content/providers/memory_file_provider.hpp>
#include <hex/api/shortcut_manager.hpp>
#include <hex/api/tutorial_manager.hpp>
#include <hex/ui/view.hpp>
#include <hex/api/events/requests_interaction.hpp>
#include <hex/api/events/events_gui.hpp>

namespace hex::plugin::builtin {

    void registerIntroductionTutorial() {
        using enum TutorialManager::Position;
        // Tutorial text is kept as plain English literals: the tutorial is
        // not translated, and mixing untranslated Lang keys breaks both the
        // displayed text and the highlight matching in non-English UIs.
        auto &tutorial = TutorialManager::createTutorial("hex.builtin.tutorial.introduction", "This tutorial will guide you through the basic usage of ImHex to get you started.");

        {
            tutorial.addStep()
                .setMessage(
                    "Welcome to ImHex!",
                    "ImHex is a Reverse Engineering Suite and Hex Editor with its main focus on visualizing binary data for easy comprehension.\n\nYou can continue to the next step by clicking the right arrow button below.",
                    Bottom | Right
                )
                .allowSkip();
        }

        {
            auto &step = tutorial.addStep();
            static EventManager::EventList::iterator eventHandle;

            step.setMessage(
                "Opening Data",
                "ImHex supports loading data from a variety of sources. This includes Files, Raw disks, another Process's memory and more.\n\nAll these options can be found on the Welcome screen or under the File menu.",
                Bottom | Right
            )
            .addHighlight("Let's create a new empty file by clicking on the 'New File' button.",
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
                eventHandle = EventProviderOpened::subscribe([&step](prv::Provider *provider) {
                    if (dynamic_cast<MemoryFileProvider*>(provider))
                        step.complete();
                });
            })
            .onComplete([] {
                EventProviderOpened::unsubscribe(eventHandle);
            });
        }

        {
            tutorial.addStep()
            .addHighlight("This is the Hex Editor. It displays the individual bytes of the loaded data and also allows you to edit them by double clicking one.\n\nYou can navigate the data by using the arrow keys or the mouse wheel.", {
                View::toWindowName("hex.builtin.view.hex_editor.name")
            })
            .allowSkip();
        }

        {
            tutorial.addStep()
            .addHighlight("This is the Data Inspector. It displays the data of the currently selected bytes in a more readable format.\n\nYou can also edit the data here by double clicking on a row.", {
                View::toWindowName("hex.builtin.view.data_inspector.name")
            })
            .onAppear([]{
                ImHexApi::HexEditor::setSelection(Region { 0, 1 });
            })
            .allowSkip();
        }

        {
            tutorial.addStep()
            .addHighlight("This is the Pattern Editor. It allows you to write code using the Pattern Language which can highlight and decode binary data structures inside of your loaded data.\n\nYou can learn more about the Pattern Language in the documentation.", {
                View::toWindowName("hex.builtin.view.pattern_editor.name")
            })
            .addHighlight("This view contains a tree view representing the data structures you defined using the Pattern Language.", {
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

            step.addHighlight("You can find more tutorials and documentation in the Help menu.", {
                "##MainMenuBar",
                "##MenuBar",
                Lang("hex.builtin.menu.help")
            })
            .addHighlight({
                "###Menu_00",
                Lang("hex.builtin.view.tutorials.name")
            })
            .onAppear([&step] {
                EventViewOpened::subscribe([&step](const View *view){
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
