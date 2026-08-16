#include <hex/api/content_registry/settings.hpp>
#include <hex/api/events/events_lifecycle.hpp>
#include <hex/api/events/events_gui.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/api/theme_manager.hpp>
#include <hex/api/tutorial_manager.hpp>

#include <hex/helpers/utils.hpp>
#include <hex/helpers/auto_reset.hpp>
#include <hex/helpers/scaling.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <fonts/vscode_icons.hpp>
#include <hex/ui/imgui_imhex_extensions.h>

#include <romfs/romfs.hpp>
#include <wolv/hash/uuid.hpp>
#include <wolv/utils/guards.hpp>

#include <list>
#include <ranges>
#include <string>
#include <fonts/fonts.hpp>

namespace hex::plugin::builtin {

    namespace {

        AutoReset<ImGuiExt::Texture> s_imhexBanner;
        AutoReset<ImGuiExt::Texture> s_compassTexture, s_globeTexture;
        std::list<std::pair<std::fs::path, ImGuiExt::Texture>> s_screenshots;
        nlohmann::json s_screenshotDescriptions;


        std::string s_uuid;

        class Blend {
        public:
            Blend(float start, float end) : m_time(0), m_start(start), m_end(end) {}

            [[nodiscard]] operator float() {
                m_time += ImGui::GetIO().DeltaTime;

                float t = m_time;
                t -= m_start;
                t /= (m_end - m_start);
                t = std::clamp(t, 0.0F, 1.0F);

                float square = t * t;
                return square / (2.0F * (square - t) + 1.0F);
            }

            void reset() {
                m_time = 0;
            }

        private:
            float m_time;
            float m_start, m_end;
        };

        EventManager::EventList::iterator s_drawEvent;
        void drawOutOfBoxExperience() {
            static float windowAlpha = 1.0F;
            static bool oobeDone = false;
            static bool tutorialEnabled = false;

            // Keep the frame rate unlocked so animations play nicely
            ImHexApi::System::unlockFrameRate();

            ImGui::SetNextWindowPos(ImHexApi::System::getMainWindowPosition());
            ImGui::SetNextWindowSize(ImHexApi::System::getMainWindowSize());

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ON_SCOPE_EXIT { ImGui::PopStyleVar(); };

            if (ImGui::Begin("##oobe", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
                ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindowRead());

                static Blend bannerSlideIn(-0.2F, 1.5F);
                static Blend bannerFadeIn(-0.2F, 1.5F);

                // Draw banner
                ImGui::SetCursorPos(scaled({ 25 * bannerSlideIn, 25 }));
                const auto bannerSize = s_imhexBanner->getSize() / (3.0F * (1.0F / ImHexApi::System::getGlobalScale()));
                ImGui::ImageWithBg(
                    *s_imhexBanner,
                    bannerSize,
                    { 0, 0 }, { 1, 1 },
                    { 0, 0, 0, 0 },
                    { 1, 1, 1, (bannerFadeIn - 0.5F) * 2.0F }
                );

                static u32 page = 0;
                switch (page) {
                    // Landing page
                    case 0: {
                        static Blend textFadeIn(2.0F, 2.5F);
                        static Blend buttonFadeIn(2.5F, 3.0F);

                        // Draw welcome text
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, textFadeIn);
                        ImGui::SameLine();
                        if (ImGui::BeginChild("Text", ImVec2(ImGui::GetContentRegionAvail().x, bannerSize.y))) {
                            fonts::Default().pushBold(1.2);
                            ImGuiExt::TextFormattedCentered("欢迎使用 HmHex！");
                            fonts::Default().pop();
                            ImGui::NewLine();
                            ImGui::NewLine();
                            ImGui::NewLine();
                            ImGuiExt::TextFormattedCentered("一款面向逆向工程师、黑客与安全研究人员的强大数据分析和可视化套件。");
                        }
                        ImGui::EndChild();

                        if (!s_screenshots.empty()) {
                            const auto imageSize = s_screenshots.front().second.getSize() * ImHexApi::System::getGlobalScale();
                            const auto padding = ImGui::GetStyle().CellPadding.x;
                            const auto stride = imageSize.x + padding * 2;
                            static bool imageHovered = false;
                            static std::string clickedImage;

                            // Move last screenshot to the front of the list when the last screenshot is out of view
                            static float position = 0;
                            if (position >= stride) {
                                position = 0;
                                s_screenshots.splice(s_screenshots.begin(), s_screenshots, std::prev(s_screenshots.end()), s_screenshots.end());
                            }

                            if (!imageHovered && clickedImage.empty())
                                position += (ImGui::GetIO().DeltaTime) * 40;

                            imageHovered = false;

                            auto drawList = ImGui::GetWindowDrawList();
                            const auto drawImage = [&](const std::fs::path &fileName, const ImGuiExt::Texture &screenshot) {
                                auto pos = ImGui::GetCursorScreenPos();

                                // Draw image
                                ImGui::Image(screenshot, imageSize);
                                imageHovered = imageHovered || ImGui::IsItemHovered();
                                auto currentHovered = ImGui::IsItemHovered();

                                if (ImGui::IsItemClicked()) {
                                    clickedImage = fileName.string();
                                    ImGui::OpenPopup("FeatureDescription");
                                }

                                // Draw shadow
                                auto &style = ImGui::GetStyle();
                                float shadowSize = style.WindowShadowSize * (currentHovered ? 3.0F : 1.0F);
                                ImU32 shadowCol = ImGui::GetColorU32(ImGuiCol_WindowShadow, currentHovered ? 2.0F : 1.0F);
                                ImVec2 shadowOffset = ImVec2(ImCos(style.WindowShadowOffsetAngle), ImSin(style.WindowShadowOffsetAngle)) * style.WindowShadowOffsetDist;
                                drawList->AddShadowRect(pos, pos + imageSize, shadowCol, shadowSize, shadowOffset, ImDrawFlags_ShadowCutOutShapeBackground);

                                ImGui::SameLine();
                            };

                            ImGui::NewLine();

                            u32 repeatCount = std::ceil(std::ceil(ImHexApi::System::getMainWindowSize().x / stride) / s_screenshots.size());
                            if (repeatCount == 0)
                                repeatCount = 1;

                            // Draw top screenshot row
                            ImGui::SetCursorPosX(-position);
                            for (u32 i = 0; i < repeatCount; i += 1) {
                                for (const auto &[fileName, screenshot] : s_screenshots | std::views::reverse) {
                                    drawImage(fileName, screenshot);
                                }
                            }

                            ImGui::NewLine();

                            // Draw bottom screenshot row
                            ImGui::SetCursorPosX(-stride + position);
                            for (u32 i = 0; i < repeatCount; i += 1) {
                                for (const auto &[fileName, screenshot] : s_screenshots) {
                                    drawImage(fileName, screenshot);
                                }
                            }

                            ImGui::SetNextWindowPos(ImGui::GetWindowPos() + (ImGui::GetWindowSize() / 2), ImGuiCond_Always, ImVec2(0.5F, 0.5F));
                            ImGui::SetNextWindowSize(ImVec2(400_scaled, 0), ImGuiCond_Always);
                            if (ImGui::BeginPopup("FeatureDescription")) {
                                const auto &description = s_screenshotDescriptions[clickedImage];
                                ImGuiExt::Header(description["title"].get<std::string>().c_str(), true);
                                ImGuiExt::TextFormattedWrapped("{}", description["description"].get<std::string>().c_str());
                                ImGui::EndPopup();
                            } else {
                                clickedImage.clear();
                            }

                            // Continue button (skip the language-selection
                            // page; the UI language defaults to Chinese)
                            const auto buttonSize = scaled({ 130, 50 });
                            ImGui::SetCursorPos(ImHexApi::System::getMainWindowSize() - buttonSize - scaled({ 10, 10 }));
                            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, buttonFadeIn);
                            if (ImGuiExt::DimmedButton(fmt::format("{} {}", "hex.ui.common.continue"_lang, ICON_VS_ARROW_RIGHT).c_str(), buttonSize))
                                page = 3;
                            ImGui::PopStyleVar();
                        }


                        ImGui::PopStyleVar();


                        break;
                    }

                    // Tutorial page
                    case 3: {
                        ImGui::NewLine();

                        ImGui::NewLine();
                        ImGui::NewLine();
                        ImGui::NewLine();

                        // Draw compass image
                        const auto imageSize = s_compassTexture->getSize() / (1.5F * (1.0F / ImHexApi::System::getGlobalScale()));
                        ImGui::SetCursorPos((ImGui::GetWindowSize() / 2 - imageSize / 2) - ImVec2(0, 50_scaled));
                        ImGui::Image(*s_compassTexture, imageSize);

                        // Draw information text about playing the tutorial
                        ImGui::SetCursorPosX(0);
                        ImGuiExt::TextFormattedCentered("hex.builtin.oobe.tutorial_question"_lang);

                        // Draw no button
                        const auto buttonSize = scaled({ 130, 50 });
                        ImGui::SetCursorPos(ImHexApi::System::getMainWindowSize() - ImVec2(buttonSize.x * 2 + 20, buttonSize.y + 10));
                        if (ImGuiExt::DimmedButton("hex.ui.common.no"_lang, buttonSize)) {
                            oobeDone = true;
                        }

                        // Draw yes button
                        ImGui::SetCursorPos(ImHexApi::System::getMainWindowSize() - ImVec2(buttonSize.x + 10, buttonSize.y + 10));
                        if (ImGuiExt::DimmedButton("hex.ui.common.yes"_lang, buttonSize)) {
                            tutorialEnabled = true;
                            oobeDone = true;
                        }
                        break;
                    }
                    default:
                        page = 0;
                }
            }
            ImGui::End();

            // Handle finishing the out of box experience
            if (oobeDone) {
                static Blend backgroundFadeOut(0.0F, 1.0F);
                windowAlpha = 1.0F - backgroundFadeOut;

                if (backgroundFadeOut >= 1.0F) {
                    if (tutorialEnabled) {
                        TutorialManager::startTutorial("hex.builtin.tutorial.introduction");
                    } else {
                        ContentRegistry::Settings::write<bool>("hex.builtin.setting.interface", "hex.builtin.setting.interface.achievement_popup", false);
                    }

                    TaskManager::doLater([] {
                        ImHexApi::System::setWindowResizable(true);
                        EventFrameBegin::unsubscribe(s_drawEvent);
                    });
                }
            }
        }

    }

    void setupOutOfBoxExperience() {
        // Don't show the out of box experience in the web version
        #if defined(OS_WEB)
            return;
        #endif

        // Check if there is a telemetry uuid
        s_uuid = ContentRegistry::Settings::read<std::string>("hex.builtin.setting.general", "hex.builtin.setting.general.uuid", "");

        if (s_uuid.empty()) {
            // Generate a new UUID
            s_uuid = wolv::hash::generateUUID();

            // Save UUID to settings
            ContentRegistry::Settings::write<std::string>("hex.builtin.setting.general", "hex.builtin.setting.general.uuid", s_uuid);
        }

        EventFirstLaunch::subscribe([] {
            ImHexApi::System::setWindowResizable(false);

            const auto imageTheme = ThemeManager::getImageTheme();
            s_imhexBanner    = ImGuiExt::Texture::fromSVG(romfs::get(fmt::format("assets/{}/banner.svg", imageTheme)).span<std::byte>(), 0, 0, ImGuiExt::Texture::Filter::Linear);
            s_compassTexture = ImGuiExt::Texture::fromImage(romfs::get("assets/common/compass.png").span<std::byte>(), ImGuiExt::Texture::Filter::Linear);
            s_globeTexture   = ImGuiExt::Texture::fromImage(romfs::get("assets/common/globe.png").span<std::byte>(), ImGuiExt::Texture::Filter::Linear);
            s_screenshotDescriptions = nlohmann::json::parse(romfs::get("assets/screenshot_descriptions.json").string());

            for (const auto &path : romfs::list("assets/screenshots")) {
                s_screenshots.emplace_back(path.filename(), ImGuiExt::Texture::fromImage(romfs::get(path).span<std::byte>(), ImGuiExt::Texture::Filter::Linear));
            }

            s_drawEvent = EventFrameBegin::subscribe(drawOutOfBoxExperience);
        });
    }

}
