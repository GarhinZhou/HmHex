#include <hex/api/tutorial_manager.hpp>
#include <hex/api/imhex_api/system.hpp>
#include <hex/api/localization_manager.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/api/events/events_gui.hpp>

#include <hex/helpers/auto_reset.hpp>

#include <imgui_internal.h>
#include <hex/helpers/utils.hpp>
#include <hex/helpers/scaling.hpp>
#include <wolv/utils/core.hpp>

#include <map>
#include <unordered_map>

#include <imgui.h>

namespace hex {

    namespace {

        AutoReset<std::map<std::string, TutorialManager::Tutorial>> s_tutorials;
        auto s_currentTutorial = s_tutorials->end();

        AutoReset<std::map<ImGuiID, std::string>> s_highlights;
        AutoReset<std::vector<std::pair<ImRect, std::string>>> s_highlightDisplays;
        AutoReset<std::map<ImGuiID, ImRect>> s_interactiveHelpDisplays;

        // Element label -> highlight text, matched against ImGui's ItemInfo
        // labels. Unlike the ID-based matching this works in any UI language:
        // the labels of localized elements (e.g. the welcome screen buttons)
        // resolve to the same strings as the tutorial's Lang entries.
        AutoReset<std::unordered_map<std::string, std::string>> s_highlightLabels;
        // ImGuiID -> label, captured from the previous frame's ItemInfo calls
        // (ItemAdd fires before ItemInfo for the same element).
        AutoReset<std::unordered_map<ImGuiID, std::string>> s_itemLabels;

        AutoReset<std::map<ImGuiID, std::function<void()>>> s_interactiveHelpItems;
        ImRect s_hoveredRect;
        ImGuiID s_hoveredId;
        ImGuiID s_activeHelpId;
        bool s_helpHoverActive = false;


        class IDStack {
        public:
            IDStack() {
                idStack.push_back(0);
            }

            void add(const char *string) {
                const ImGuiID seed = idStack.back();
                const ImGuiID id = ImHashStr(string, 0, seed);

                idStack.push_back(id);
            }

            void add(const std::string &string) {
                const ImGuiID seed = idStack.back();
                const ImGuiID id = ImHashStr(string.c_str(), string.length(), seed);

                idStack.push_back(id);
            }

            void add(const void *pointer) {
                const ImGuiID seed = idStack.back();
                const ImGuiID id = ImHashData(&pointer, sizeof(pointer), seed);

                idStack.push_back(id);
            }

            void add(int value) {
                const ImGuiID seed = idStack.back();
                const ImGuiID id = ImHashData(&value, sizeof(value), seed);

                idStack.push_back(id);
            }

            ImGuiID get() {
                return idStack.back();
            }
        private:
            ImVector<ImGuiID> idStack;
        };

        ImGuiID calculateId(const auto &ids) {
            IDStack idStack;

            for (const auto &id : ids) {
                std::visit(wolv::util::overloaded {
                        [&idStack](const Lang &id) {
                            idStack.add(id.get());
                        },
                        [&idStack](const auto &id) {
                            idStack.add(id);
                        }
                }, id);
            }

            return idStack.get();
        }

    }

    void TutorialManager::init() {
        // Capture element labels (from the previous frame's ItemInfo) so
        // localized elements can be matched by their resolved text.
        EventImGuiItemLabeled::subscribe([](ImGuiID id, const char *label) {
            if (label != nullptr)
                s_itemLabels->insert_or_assign(id, std::string(label));
        });

        EventImGuiElementRendered::subscribe([](ImGuiID id, const std::array<float, 4> bb){
            const auto boundingBox = ImRect(bb[0], bb[1], bb[2], bb[3]);

            if (!ImGui::IsRectVisible(boundingBox.Min, boundingBox.Max))
                return;

            {
                const auto element = hex::s_highlights->find(id);
                if (element != hex::s_highlights->end()) {
                    hex::s_highlightDisplays->emplace_back(boundingBox, element->second);

                    const auto window = ImGui::GetCurrentWindow();
                    if (window != nullptr && window->DockNode != nullptr && window->DockNode->TabBar != nullptr)
                        window->DockNode->TabBar->NextSelectedTabId = window->TabId;
                } else {
                    // Fall back to label matching: localized elements (their
                    // labels resolve via Lang) match the tutorial's Lang
                    // entries regardless of the UI language.
                    const auto labelIt = hex::s_itemLabels->find(id);
                    if (labelIt != hex::s_itemLabels->end()) {
                        const auto labelHighlight = hex::s_highlightLabels->find(labelIt->second);
                        if (labelHighlight != hex::s_highlightLabels->end())
                            hex::s_highlightDisplays->emplace_back(boundingBox, labelHighlight->second);
                    }
                }
            }

            {
                const auto element = s_interactiveHelpItems->find(id);
                if (element != s_interactiveHelpItems->end()) {
                    (*s_interactiveHelpDisplays)[id] = boundingBox;
                }

            }

            if (id != 0 && boundingBox.Contains(ImGui::GetMousePos())) {
                if ((s_hoveredRect.GetArea() == 0 || boundingBox.GetArea() < s_hoveredRect.GetArea()) && s_interactiveHelpItems->contains(id)) {
                    s_hoveredRect = boundingBox;
                    s_hoveredId = id;
                }
            }
        });
    }

    const std::map<std::string, TutorialManager::Tutorial>& TutorialManager::getTutorials() {
        return s_tutorials;
    }

    std::map<std::string, TutorialManager::Tutorial>::iterator TutorialManager::getCurrentTutorial() {
        return s_currentTutorial;
    }


    TutorialManager::Tutorial& TutorialManager::createTutorial(const UnlocalizedString &unlocalizedName, const UnlocalizedString &unlocalizedDescription) {
        return s_tutorials->try_emplace(unlocalizedName, Tutorial(unlocalizedName, unlocalizedDescription)).first->second;
    }

    void TutorialManager::startHelpHover() {
        TaskManager::doLater([]{
            s_helpHoverActive = true;
        });
    }

    void TutorialManager::addInteractiveHelpText(std::initializer_list<std::variant<Lang, std::string, int>> &&ids, UnlocalizedString unlocalizedString) {
        auto id = calculateId(ids);

        s_interactiveHelpItems->emplace(id, [text = std::move(unlocalizedString)]{
            log::info("{}", Lang(text).get());
        });
    }

    void TutorialManager::addInteractiveHelpLink(std::initializer_list<std::variant<Lang, std::string, int>> &&ids, std::string link) {
        auto id = calculateId(ids);

        s_interactiveHelpItems->emplace(id, [link = std::move(link)]{
            hex::openWebpage(link);
        });
    }

    void TutorialManager::setLastItemInteractiveHelpPopup(std::function<void()> callback) {
        auto id = ImGui::GetItemID();

        if (!s_interactiveHelpItems->contains(id)) {
            s_interactiveHelpItems->emplace(id, [id]{
                s_activeHelpId = id;
            });
        }

        if (id == s_activeHelpId) {
            ImGui::SetNextWindowSize(scaled({ 400, 0 }));
            if (ImGui::BeginTooltip()) {
                callback();
                ImGui::EndTooltip();
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                s_activeHelpId = 0;
        }
    }

    void TutorialManager::setLastItemInteractiveHelpLink(std::string link) {
        auto id = ImGui::GetItemID();

        if (s_interactiveHelpItems->contains(id))
            return;

        s_interactiveHelpItems->emplace(id, [link = std::move(link)]{
            hex::openWebpage(link);
        });
    }


    void TutorialManager::startTutorial(const UnlocalizedString &unlocalizedName) {
        s_currentTutorial = s_tutorials->find(unlocalizedName);
        if (s_currentTutorial == s_tutorials->end())
            return;

        s_currentTutorial->second.start();
    }

    void TutorialManager::drawHighlights() {
        if (s_helpHoverActive) {
            const auto &drawList = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
            drawList->AddText(ImGui::GetMousePos() + scaled({ 10, -5, }), ImGui::GetColorU32(ImGuiCol_Text), "?");

            for (const auto &[id, boundingBox] : *s_interactiveHelpDisplays) {
                drawList->AddRect(
                    boundingBox.Min - ImVec2(5, 5),
                    boundingBox.Max + ImVec2(5, 5),
                    ImGui::GetColorU32(ImGuiCol_PlotHistogram),
                    5.0F,
                    ImDrawFlags_None,
                    2.0F
                );
            }

            s_interactiveHelpDisplays->clear();

            const bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (s_hoveredId != 0) {
                drawList->AddRectFilled(s_hoveredRect.Min, s_hoveredRect.Max, 0x30FFFFFF);

                if (mouseClicked) {
                    auto it = s_interactiveHelpItems->find(s_hoveredId);
                    if (it != s_interactiveHelpItems->end()) {
                        it->second();
                    }
                }

                s_hoveredId = 0;
                s_hoveredRect = {};
            }

            if (mouseClicked || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                s_helpHoverActive = false;
            }

            // Discard mouse click so it doesn't activate clicked item
            ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]     = false;
            ImGui::GetIO().MouseReleased[ImGuiMouseButton_Left] = false;
            ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left]  = false;
        }

        for (const auto &[rect, unlocalizedText] : *s_highlightDisplays) {
            const auto drawList = ImGui::GetForegroundDrawList();

            drawList->PushClipRectFullScreen();
            {
                auto highlightColor = ImGuiExt::GetCustomColorVec4(ImGuiCustomCol_Highlight);
                highlightColor.w *= ImSin(ImGui::GetTime() * 6.0F) / 4.0F + 0.75F;
                ImHexApi::System::unlockFrameRate();

                drawList->AddRect(rect.Min - ImVec2(5, 5), rect.Max + ImVec2(5, 5), ImColor(highlightColor), 5.0F, ImDrawFlags_None, 2.0F);
            }

            {
                if (!unlocalizedText.empty()) {
                    const auto mainWindowPos = ImHexApi::System::getMainWindowPosition();
                    const auto mainWindowSize = ImHexApi::System::getMainWindowSize();

                    const auto margin = ImGui::GetStyle().WindowPadding;

                    ImVec2 windowPos  = { rect.Min.x + 20_scaled, rect.Max.y + 10_scaled };
                    ImVec2 windowSize = { std::max<float>(rect.Max.x - rect.Min.x - 40_scaled, 300_scaled), 0 };

                    const char* text = Lang(unlocalizedText);
                    const auto textSize = ImGui::CalcTextSize(text, nullptr, false, windowSize.x - margin.x * 2);
                    windowSize.y = textSize.y + margin.y * 2;

                    if (windowPos.y + windowSize.y > mainWindowPos.y + mainWindowSize.y)
                        windowPos.y = rect.Min.y - windowSize.y - 15_scaled;
                    if (windowPos.y < mainWindowPos.y)
                        windowPos.y = rect.Min.y + 10_scaled;

                    ImGui::SetNextWindowPos(windowPos);
                    ImGui::SetNextWindowSize(windowSize);
                    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
                    if (ImGui::Begin(unlocalizedText.c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindowRead());
                        ImGuiExt::TextFormattedWrapped("{}", text);
                    }
                    ImGui::End();
                }
            }
            drawList->PopClipRect();

        }
        s_highlightDisplays->clear();
    }

    void TutorialManager::drawMessageBox(std::optional<Tutorial::Step::Message> message) {
        const auto windowStart = ImHexApi::System::getMainWindowPosition() + scaled({ 10, 10 });
        const auto windowEnd = ImHexApi::System::getMainWindowPosition() + ImHexApi::System::getMainWindowSize() - scaled({ 10, 10 });

        ImVec2 position = ImHexApi::System::getMainWindowPosition() + ImHexApi::System::getMainWindowSize() / 2.0F;
        ImVec2 pivot    = { 0.5F, 0.5F };

        if (!message.has_value()) {
            message = Tutorial::Step::Message {
                 Position::None,
                "",
                "",
                false
            };
        }

        if (message->position == Position::None) {
            message->position = Position::Bottom | Position::Right;
        }

        if ((message->position & Position::Top) == Position::Top) {
            position.y  = windowStart.y;
            pivot.y     = 0.0F;
        }
        if ((message->position & Position::Bottom) == Position::Bottom) {
            position.y  = windowEnd.y;
            pivot.y     = 1.0F;
        }
        if ((message->position & Position::Left) == Position::Left) {
            position.x  = windowStart.x;
            pivot.x     = 0.0F;
        }
        if ((message->position & Position::Right) == Position::Right) {
            position.x  = windowEnd.x;
            pivot.x     = 1.0F;
        }

        ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        if (ImGui::Begin("##TutorialMessage", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindowRead());

            if (!message->unlocalizedTitle.empty())
                ImGuiExt::Header(Lang(message->unlocalizedTitle), true);

            if (!message->unlocalizedMessage.empty()) {
                ImGui::PushTextWrapPos(300_scaled);
                ImGui::TextUnformatted(Lang(message->unlocalizedMessage));
                ImGui::PopTextWrapPos();
                ImGui::NewLine();
            }

            ImGui::BeginDisabled(s_currentTutorial->second.m_currentStep == s_currentTutorial->second.m_steps.begin());
            if (ImGui::ArrowButton("Backwards", ImGuiDir_Left)) {
                // Guard against exceptions unwinding through the frame and
                // corrupting the ImGui window/disabled stacks (hard assert in
                // EndFrame -> abort).
                try {
                    s_currentTutorial->second.m_currentStep->advance(-1);
                } catch (const std::exception &e) {
                    log::error("Tutorial navigation failed: {}", e.what());
                } catch (...) {
                    log::error("Tutorial navigation failed with unknown exception");
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::BeginDisabled(!message->allowSkip && s_currentTutorial->second.m_currentStep == s_currentTutorial->second.m_latestStep);
            if (ImGui::ArrowButton("Forwards", ImGuiDir_Right)) {
                try {
                    s_currentTutorial->second.m_currentStep->advance(1);
                } catch (const std::exception &e) {
                    log::error("Tutorial navigation failed: {}", e.what());
                } catch (...) {
                    log::error("Tutorial navigation failed with unknown exception");
                }
            }
            ImGui::EndDisabled();
        }
        ImGui::End();
    }

    void TutorialManager::drawTutorial() {
        // drawHighlights must run regardless of the tutorial state: it also
        // renders the interactive-help hover mode, which is independent of
        // the tutorial data.
        drawHighlights();

        // Guard against the tutorial storage being cleared during shutdown
        // (impl::cleanup() resets all AutoReset containers, which invalidates
        // s_currentTutorial); any late frame must not touch it.
        if (s_tutorials->empty() || s_currentTutorial == s_tutorials->end())
            return;

        const auto &currentStep = s_currentTutorial->second.m_currentStep;
        if (currentStep == s_currentTutorial->second.m_steps.end())
            return;

        const auto &message = currentStep->m_message;
        drawMessageBox(message);
    }



    void TutorialManager::reset() {
        s_tutorials->clear();
        s_currentTutorial = s_tutorials->end();

        s_highlights->clear();
        s_highlightDisplays->clear();
        s_highlightLabels->clear();
        s_itemLabels->clear();
    }

    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::addStep() {
        auto &newStep = m_steps.emplace_back(this);
        m_currentStep = m_steps.end();
        m_latestStep  = m_currentStep;

        return newStep;
    }

    void TutorialManager::Tutorial::start() {
        m_currentStep = m_steps.begin();
        m_latestStep  = m_currentStep;
        if (m_currentStep == m_steps.end())
            return;

        m_currentStep->addHighlights();
    }

    void TutorialManager::Tutorial::Step::addHighlights() const {
        if (m_onAppear)
            m_onAppear();

        for (const auto &[text, ids] : m_highlights) {
            s_highlights->emplace(calculateId(ids), text);

            // Register the resolved Lang labels for language-independent
            // matching (the displayed element label resolves to the same
            // string in the current language).
            for (const auto &id : ids) {
                if (std::holds_alternative<Lang>(id))
                    s_highlightLabels->insert_or_assign(std::get<Lang>(id).get(), std::string(text));
            }
        }
    }

    void TutorialManager::Tutorial::Step::removeHighlights() const {
        for (const auto &[text, ids] : m_highlights) {
            s_highlights->erase(calculateId(ids));

            for (const auto &id : ids) {
                if (std::holds_alternative<Lang>(id))
                    s_highlightLabels->erase(std::get<Lang>(id).get());
            }
        }
    }

    void TutorialManager::Tutorial::Step::advance(i32 steps) const {
        m_parent->m_currentStep->removeHighlights();

        if (m_parent->m_currentStep == m_parent->m_latestStep && steps > 0)
            std::advance(m_parent->m_latestStep, steps);
        std::advance(m_parent->m_currentStep, steps);

        if (m_parent->m_currentStep != m_parent->m_steps.end())
            m_parent->m_currentStep->addHighlights();
        else
            s_currentTutorial = s_tutorials->end();
    }


    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::Step::addHighlight(const UnlocalizedString &unlocalizedText, std::initializer_list<std::variant<Lang, std::string, int>>&& ids) {
        m_highlights.emplace_back(
            unlocalizedText,
            ids
        );

        return *this;
    }

    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::Step::addHighlight(std::initializer_list<std::variant<Lang, std::string, int>>&& ids) {
        return this->addHighlight("", std::forward<decltype(ids)>(ids));
    }



    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::Step::setMessage(const UnlocalizedString &unlocalizedTitle, const UnlocalizedString &unlocalizedMessage, Position position) {
        m_message = Message {
            position,
            unlocalizedTitle,
            unlocalizedMessage,
            false
        };

        return *this;
    }

    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::Step::allowSkip() {
        if (m_message.has_value()) {
            m_message->allowSkip = true;
        } else {
            m_message = Message {
                Position::Bottom | Position::Right,
                "",
                "",
                true
            };
        }

        return *this;
    }

    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::Step::onAppear(std::function<void()> callback) {
        m_onAppear = std::move(callback);

        return *this;
    }

    TutorialManager::Tutorial::Step& TutorialManager::Tutorial::Step::onComplete(std::function<void()> callback) {
        m_onComplete = std::move(callback);

        return *this;
    }




    bool TutorialManager::Tutorial::Step::isCurrent() const {
        const auto &currentStep = m_parent->m_currentStep;

        if (currentStep == m_parent->m_steps.end())
            return false;

        return &*currentStep == this;
    }

    void TutorialManager::Tutorial::Step::complete() const {
        if (this->isCurrent()) {
            this->advance();

            if (m_onComplete) {
                TaskManager::doLater([this] {
                    m_onComplete();
                });
            }
        }
    }

}