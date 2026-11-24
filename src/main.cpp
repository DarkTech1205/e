#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace geode::prelude;

static bool g_verificationReplayRequested = false;

static CCSprite* createSpriteWithFallback(const char* primary, const char* fallback) {
    if (auto spr = CCSprite::createWithSpriteFrameName(primary)) {
        return spr;
    }
    if (auto spr = CCSprite::createWithSpriteFrameName(fallback)) {
        return spr;
    }
    return CCSprite::create();
}

static void bindAlertButton(ButtonSprite* btnSpr, FLAlertLayer* alert, int index, CCObject* target, SEL_MenuHandler selector) {
    if (!alert) return;
    if (btnSpr) {
        if (auto item = typeinfo_cast<CCMenuItem*>(btnSpr->getParent())) {
            item->setTarget(target, selector);
            return;
        }
    }
    if (alert->m_buttonMenu && static_cast<int>(alert->m_buttonMenu->getChildrenCount()) > index) {
        if (auto item = typeinfo_cast<CCMenuItem*>(alert->m_buttonMenu->getChildren()->objectAtIndex(index))) {
            item->setTarget(target, selector);
        }
    }
}

// Try calling GD's real built-in LevelFeatureLayer::create(int) if exported in the binary
static bool tryOpenNativeFeatureLayer(int levelID) {
    using CreateFeatureFn = FLAlertLayer* (*)(int);
    CreateFeatureFn fn = nullptr;
#ifdef _WIN32
    if (auto mod = GetModuleHandleA(nullptr)) {
        fn = reinterpret_cast<CreateFeatureFn>(GetProcAddress(mod, "?create@LevelFeatureLayer@@SAPEAV1@H@Z"));
        if (!fn) {
            fn = reinterpret_cast<CreateFeatureFn>(GetProcAddress(mod, "?create@LevelFeatureLayer@@SAPAV1@H@Z"));
        }
    }
#else
    fn = reinterpret_cast<CreateFeatureFn>(dlsym(RTLD_DEFAULT, "_ZN17LevelFeatureLayer6createEi"));
#endif
    if (fn) {
        if (auto layer = fn(levelID)) {
            layer->show();
            return true;
        }
    }
    return false;
}

// Recursively relabel "RATE STARS" -> "DEV: SET STARS" inside the real RateStarsLayer
static void relabelRateStarsToDev(CCNode* root) {
    if (!root) return;
    if (auto lbl = typeinfo_cast<CCLabelBMFont*>(root)) {
        std::string text = lbl->getString();
        if (text.find("RATE") != std::string::npos || text.find("Rate") != std::string::npos) {
            lbl->setString("DEV: SET STARS");
        }
    }
    if (auto children = root->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            relabelRateStarsToDev(static_cast<CCNode*>(children->objectAtIndex(i)));
        }
    }
}

// Last-resort recreation of RobTop's "SET FEATURED" popup
class SetFeaturedFallbackController : public CCNode {
public:
    FLAlertLayer* m_alert = nullptr;
    GJGameLevel* m_level = nullptr;
    CCLabelBMFont* m_scoreLabel = nullptr;
    ButtonSprite* m_epicBtnSpr = nullptr;
    int m_featuredScore = 24970;
    int m_epicMode = 1; // 0 = FEATURED, 1 = EPIC ONLY, 2 = LEGENDARY, 3 = MYTHIC

    static SetFeaturedFallbackController* create(FLAlertLayer* alert, GJGameLevel* level) {
        auto ret = new SetFeaturedFallbackController();
        if (ret && ret->init()) {
            ret->m_alert = alert;
            ret->m_level = level;
            if (level && level->m_featured > 0) {
                ret->m_featuredScore = level->m_featured;
            }
            if (level && level->m_isEpic > 0) {
                ret->m_epicMode = level->m_isEpic;
            }
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void setupUI() {
        if (!m_alert || !m_alert->m_mainLayer) return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({winSize.width / 2.f, winSize.height / 2.f});
        m_alert->m_mainLayer->addChild(menu, 10);

        // Center featured score number (e.g. 24970)
        m_scoreLabel = CCLabelBMFont::create(
            fmt::format("{}", m_featuredScore).c_str(),
            "bigFont.fnt"
        );
        m_scoreLabel->setScale(0.95f);
        m_scoreLabel->setPosition({winSize.width / 2.f, winSize.height / 2.f + 4.f});
        m_alert->m_mainLayer->addChild(m_scoreLabel, 10);

        // Left big green arrow (-100)
        auto bigLeftSpr = createSpriteWithFallback("edit_leftBtn2_001.png", "GJ_arrow_01_001.png");
        bigLeftSpr->setColor(ccc3(110, 255, 90));
        auto bigLeftBtn = CCMenuItemSpriteExtra::create(
            bigLeftSpr, this, menu_selector(SetFeaturedFallbackController::onDecBig)
        );
        bigLeftBtn->setPosition({-138.f, 4.f});
        menu->addChild(bigLeftBtn);

        // Left small cyan arrow (-1)
        auto smallLeftSpr = createSpriteWithFallback("edit_leftBtn_001.png", "GJ_arrow_02_001.png");
        smallLeftSpr->setColor(ccc3(90, 245, 255));
        auto smallLeftBtn = CCMenuItemSpriteExtra::create(
            smallLeftSpr, this, menu_selector(SetFeaturedFallbackController::onDecSmall)
        );
        smallLeftBtn->setPosition({-92.f, 4.f});
        menu->addChild(smallLeftBtn);

        // Right small cyan arrow (+1)
        auto smallRightSpr = createSpriteWithFallback("edit_rightBtn_001.png", "GJ_arrow_02_001.png");
        smallRightSpr->setColor(ccc3(90, 245, 255));
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("edit_rightBtn_001.png")) {
            smallRightSpr->setFlipX(true);
        }
        auto smallRightBtn = CCMenuItemSpriteExtra::create(
            smallRightSpr, this, menu_selector(SetFeaturedFallbackController::onIncSmall)
        );
        smallRightBtn->setPosition({92.f, 4.f});
        menu->addChild(smallRightBtn);

        // Right big green arrow (+100)
        auto bigRightSpr = createSpriteWithFallback("edit_rightBtn2_001.png", "GJ_arrow_01_001.png");
        bigRightSpr->setColor(ccc3(110, 255, 90));
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("edit_rightBtn2_001.png")) {
            bigRightSpr->setFlipX(true);
        }
        auto bigRightBtn = CCMenuItemSpriteExtra::create(
            bigRightSpr, this, menu_selector(SetFeaturedFallbackController::onIncBig)
        );
        bigRightBtn->setPosition({138.f, 4.f});
        menu->addChild(bigRightBtn);

        // Bottom-right "EPIC ONLY" button inside the dialog
        m_epicBtnSpr = ButtonSprite::create("EPIC\nONLY", 44, true, "goldFont.fnt", "GJ_button_01.png", 34.f, 0.45f);
        auto epicBtn = CCMenuItemSpriteExtra::create(
            m_epicBtnSpr, this, menu_selector(SetFeaturedFallbackController::onToggleEpic)
        );
        epicBtn->setPosition({145.f, -64.f});
        menu->addChild(epicBtn);
        updateEpicLabel();

        // Top-right green X close button on the corner of the box
        auto closeSpr = createSpriteWithFallback("GJ_closeBtn_001.png", "GJ_deleteBtn_001.png");
        closeSpr->setScale(0.85f);
        auto closeBtn = CCMenuItemSpriteExtra::create(
            closeSpr, this, menu_selector(SetFeaturedFallbackController::onClose)
        );
        closeBtn->setPosition({176.f, 86.f});
        menu->addChild(closeBtn);

        bindAlertButton(m_alert->m_button2, m_alert, 1, this, menu_selector(SetFeaturedFallbackController::onSubmit));
    }

    void updateScoreLabel() {
        if (m_featuredScore < 0) m_featuredScore = 0;
        if (m_scoreLabel) {
            m_scoreLabel->setString(fmt::format("{}", m_featuredScore).c_str());
        }
    }

    void updateEpicLabel() {
        if (!m_epicBtnSpr) return;
        const char* labels[4] = {
            "FEAT\nONLY",
            "EPIC\nONLY",
            "LEGD\nONLY",
            "MYTH\nONLY"
        };
        m_epicBtnSpr->setString(labels[m_epicMode % 4]);
    }

    void onDecBig(CCObject*) { m_featuredScore -= 100; updateScoreLabel(); }
    void onDecSmall(CCObject*) { m_featuredScore -= 1; updateScoreLabel(); }
    void onIncSmall(CCObject*) { m_featuredScore += 1; updateScoreLabel(); }
    void onIncBig(CCObject*) { m_featuredScore += 100; updateScoreLabel(); }

    void onToggleEpic(CCObject*) {
        m_epicMode = (m_epicMode + 1) % 4;
        updateEpicLabel();
    }

    void onClose(CCObject*) {
        if (m_alert) {
            m_alert->removeFromParentAndCleanup(true);
        }
    }

    void onSubmit(CCObject*) {
        if (m_level) {
            m_level->m_featured = m_featuredScore;
            m_level->m_isEpic = m_epicMode;
        }
        std::string msg = fmt::format("Set Featured: {}", m_featuredScore);
        if (m_alert) {
            m_alert->removeFromParentAndCleanup(true);
        }
        Notification::create(msg, NotificationIcon::Success)->show();
    }
};

// Controller for the Server Delete confirmation popup
class ServerDeleteController : public CCNode {
public:
    FLAlertLayer* m_alert = nullptr;

    static ServerDeleteController* create(FLAlertLayer* alert) {
        auto ret = new ServerDeleteController();
        if (ret && ret->init()) {
            ret->m_alert = alert;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void setup() {
        bindAlertButton(m_alert->m_button2, m_alert, 1, this, menu_selector(ServerDeleteController::onConfirmDelete));
    }

    void onConfirmDelete(CCObject*) {
        if (m_alert) {
            m_alert->removeFromParentAndCleanup(true);
        }
        Notification::create("Level deleted from server!", NotificationIcon::Success)->show();
        CCDirector::sharedDirector()->popSceneWithTransition(
            0.5f, PopTransition::kPopTransitionFade
        );
    }
};

class $modify(SecretModButtonsLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) {
            return false;
        }

        // 1. LEFT SIDE MENU:
        //    Top: Green Star (Set Featured) -> Yellow Star (Mod Send) -> Server Delete (PC with red X)
        if (auto leftMenu = this->getChildByID("left-side-menu")) {
            // Top: Green Star button -> Opens "SET FEATURED"
            if (!leftMenu->getChildByID("robtop-set-featured-btn")) {
                auto featSpr = createSpriteWithFallback("GJ_starBtnMod_001.png", "GJ_starBtn_001.png");
                if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName("GJ_starBtnMod_001.png")) {
                    featSpr->setColor(ccc3(80, 255, 110));
                }
                auto featBtn = CCMenuItemSpriteExtra::create(
                    featSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onSetFeatured)
                );
                featBtn->setID("robtop-set-featured-btn");
                featBtn->setZOrder(-30);
                leftMenu->addChild(featBtn);
            }

            // Second: Yellow Star button -> Opens Moderator Send / Rate
            if (!leftMenu->getChildByID("robtop-mod-send-btn")) {
                auto sendSpr = createSpriteWithFallback("GJ_starBtn_001.png", "GJ_starBtnMod_001.png");
                auto sendBtn = CCMenuItemSpriteExtra::create(
                    sendSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onModSend)
                );
                sendBtn->setID("robtop-mod-send-btn");
                sendBtn->setZOrder(-20);
                leftMenu->addChild(sendBtn);
            }

            // Third: Server Delete button (PC/Server tower with red X)
            if (!leftMenu->getChildByID("robtop-server-delete-btn")) {
                auto delSpr = createSpriteWithFallback("GJ_deleteServerBtn_001.png", "GJ_deleteBtn_001.png");
                auto delBtn = CCMenuItemSpriteExtra::create(
                    delSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onServerDelete)
                );
                delBtn->setID("robtop-server-delete-btn");
                delBtn->setZOrder(-10);
                leftMenu->addChild(delBtn);
            }

            leftMenu->updateLayout();
        }

        // 2. BOTTOM-RIGHT CORNER (bottom of right-side-menu):
        //    Yellow Star button -> Opens built-in RateStarsLayer ("DEV: SET STARS")
        if (auto rightMenu = this->getChildByID("right-side-menu")) {
            if (!rightMenu->getChildByID("robtop-dev-stars-btn")) {
                auto devStarsSpr = createSpriteWithFallback("GJ_starBtn_001.png", "GJ_starBtnMod_001.png");
                auto devStarsBtn = CCMenuItemSpriteExtra::create(
                    devStarsSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onDevSetStars)
                );
                devStarsBtn->setID("robtop-dev-stars-btn");
                devStarsBtn->setZOrder(50);
                rightMenu->addChild(devStarsBtn);
                rightMenu->updateLayout();
            }
        }

        // 3. CENTER PLAY MENU: RobTop's 2nd Play button (Verification Replay playback)
        if (auto playMenu = this->getChildByID("play-menu")) {
            if (!this->getChildByID("robtop-replay-play-menu")) {
                CCPoint mainPlayPos = {0.f, 51.f};
                if (auto mainPlayBtn = playMenu->getChildByID("play-button")) {
                    mainPlayPos = mainPlayBtn->getPosition();
                } else if (playMenu->getChildrenCount() > 0) {
                    if (auto firstChild = static_cast<CCNode*>(playMenu->getChildren()->objectAtIndex(0))) {
                        mainPlayPos = firstChild->getPosition();
                    }
                }

                auto replayPlayMenu = CCMenu::create();
                replayPlayMenu->setID("robtop-replay-play-menu");
                replayPlayMenu->setPosition(playMenu->getPosition());

                auto replaySpr = createSpriteWithFallback("GJ_playBtn2_001.png", "GJ_playBtn_001.png");
                replaySpr->setScale(0.68f);

                auto replayBtn = CCMenuItemSpriteExtra::create(
                    replaySpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onVerificationReplayPlay)
                );
                replayBtn->setID("robtop-replay-play-btn");
                replayBtn->setPosition({mainPlayPos.x + 92.f, mainPlayPos.y});

                replayPlayMenu->addChild(replayBtn);
                this->addChild(replayPlayMenu, playMenu->getZOrder());
            }
        }

        return true;
    }

    // Green Star (top-left) -> "SET FEATURED"
    void onSetFeatured(CCObject*) {
        int levelID = static_cast<int>(m_level->m_levelID);
        if (tryOpenNativeFeatureLayer(levelID)) {
            return;
        }

        auto alert = FLAlertLayer::create(
            nullptr,
            "SET FEATURED",
            "\n\n\n\n",
            "CANCEL",
            "SUBMIT",
            360.f
        );
        if (alert) {
            auto controller = SetFeaturedFallbackController::create(alert, m_level);
            alert->m_mainLayer->addChild(controller);
            controller->setupUI();
            alert->show();
        }
    }

    // Yellow Star (second on left) -> Moderator Send / Suggest
    void onModSend(CCObject*) {
        int levelID = static_cast<int>(m_level->m_levelID);
        if (auto layer = RateStarsLayer::create(levelID, m_level->isPlatformer(), true)) {
            layer->show();
        }
    }

    // Bottom-Right Yellow Star -> Real built-in RateStarsLayer ("DEV: SET STARS" 1-10 grid)
    void onDevSetStars(CCObject*) {
        int levelID = static_cast<int>(m_level->m_levelID);
        if (auto layer = RateStarsLayer::create(levelID, m_level->isPlatformer(), false)) {
            relabelRateStarsToDev(layer);
            layer->show();
        }
    }

    // Server Delete (PC with red X on left)
    void onServerDelete(CCObject*) {
        auto alert = FLAlertLayer::create(
            nullptr,
            "Delete Level",
            "Are you sure you want to <cr>delete</c> this level from the server?",
            "NO",
            "YES",
            330.f
        );
        if (alert) {
            auto controller = ServerDeleteController::create(alert);
            alert->m_mainLayer->addChild(controller);
            controller->setup();
            alert->show();
        }
    }

    // 2nd Play button -> Launches Verification Replay playback
    void onVerificationReplayPlay(CCObject*) {
        g_verificationReplayRequested = true;
        bool hasRecordedInputs = !std::string(m_level->m_recordedInputs).empty();
        if (hasRecordedInputs) {
            Notification::create("Playing Creator Verification Replay", NotificationIcon::Info)->show();
        } else {
            Notification::create("Verification Replay Mode (No server replay data; Noclip active)", NotificationIcon::Info)->show();
        }

        if (auto playMenu = this->getChildByID("play-menu")) {
            if (auto mainPlay = typeinfo_cast<CCMenuItem*>(playMenu->getChildByID("play-button"))) {
                mainPlay->activate();
                return;
            }
            if (playMenu->getChildrenCount() > 0) {
                if (auto firstItem = typeinfo_cast<CCMenuItem*>(playMenu->getChildren()->objectAtIndex(0))) {
                    firstItem->activate();
                }
            }
        }
    }
};

class $modify(VerificationReplayPlayLayer, PlayLayer) {
    struct Fields {
        bool m_isRobTopReplay = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        bool active = g_verificationReplayRequested;
        g_verificationReplayRequested = false;

        bool hasReplayData = level && !std::string(level->m_recordedInputs).empty();
        bool finalUseReplay = useReplay || (active && hasReplayData);

        if (!PlayLayer::init(level, finalUseReplay, dontCreateObjects)) {
            return false;
        }

        m_fields->m_isRobTopReplay = active;

        if (active) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto badge = CCLabelBMFont::create("VERIFICATION REPLAY", "bigFont.fnt");
            badge->setScale(0.35f);
            badge->setOpacity(120);
            badge->setPosition({winSize.width / 2.f, winSize.height - 14.f});
            badge->setID("robtop-verification-replay-badge");
            this->addChild(badge, 100);
        }

        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (m_fields->m_isRobTopReplay) {
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }
};
