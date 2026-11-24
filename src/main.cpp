#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cmath>

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

// Last-resort recreation of RobTop's "SET FEATURED" popup (matches stream screenshot)
class SetFeaturedFallbackController : public CCNode {
public:
    FLAlertLayer* m_alert = nullptr;
    GJGameLevel* m_level = nullptr;
    CCLabelBMFont* m_scoreLabel = nullptr;
    ButtonSprite* m_epicBtnSpr = nullptr;
    int m_featuredScore = 24970;
    int m_epicMode = 1; // 0 = FEAT ONLY, 1 = EPIC ONLY, 2 = LEGD ONLY, 3 = MYTH ONLY

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

        // 1. LEFT SIDE MENU (exact order from RobTop's stream screenshot):
        //    Top: Green-tinted Moderator Star (Set Featured)
        //    Second: Untinted Moderator Star (Mod Send)
        //    Third: Server Delete (PC with red X)
        if (auto leftMenu = this->getChildByID("left-side-menu")) {
            // Top: Same texture as normal moderator button, tinted green -> Opens "SET FEATURED"
            if (!leftMenu->getChildByID("robtop-set-featured-btn")) {
                auto featSpr = createSpriteWithFallback("GJ_starBtnMod_001.png", "GJ_starBtn_001.png");
                featSpr->setColor(ccc3(95, 255, 130));
                auto featBtn = CCMenuItemSpriteExtra::create(
                    featSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onSetFeatured)
                );
                featBtn->setID("robtop-set-featured-btn");
                featBtn->setZOrder(-30);
                leftMenu->addChild(featBtn);
            }

            // Second: Normal Moderator button (untinted GJ_starBtnMod_001.png) -> Opens Moderator Send
            if (!leftMenu->getChildByID("robtop-mod-send-btn")) {
                auto sendSpr = createSpriteWithFallback("GJ_starBtnMod_001.png", "GJ_starBtn_001.png");
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

        // 3. CENTER PLAY MENU: RobTop's small, translucent, cyan-tinted 2nd Play button
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
                replaySpr->setScale(0.42f);
                replaySpr->setColor(ccc3(95, 210, 255));
                replaySpr->setCascadeOpacityEnabled(true);
                replaySpr->setOpacity(145);

                auto replayBtn = CCMenuItemSpriteExtra::create(
                    replaySpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onVerificationReplayPlay)
                );
                replayBtn->setID("robtop-replay-play-btn");
                replayBtn->setPosition({mainPlayPos.x + 74.f, mainPlayPos.y});

                replayPlayMenu->addChild(replayBtn);
                this->addChild(replayPlayMenu, playMenu->getZOrder());
            }
        }

        return true;
    }

    // Green-tinted Moderator Star (top-left) -> "SET FEATURED"
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

    // Untinted Moderator Star (second on left) -> Moderator Send / Suggest
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

    // 2nd Play button -> Launches Verification Replay autoplay
    void onVerificationReplayPlay(CCObject*) {
        g_verificationReplayRequested = true;
        Notification::create("Verification Replay Active", NotificationIcon::Info)->show();

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
        bool m_holdingInput = false;
        float m_lastPlayerY = 0.f;
        float m_pulseTimer = 0.f;
    };

    void setBotInput(bool down) {
        if (m_fields->m_holdingInput == down) return;
        m_fields->m_holdingInput = down;
        if (auto dispatcher = CCDirector::sharedDirector()->getKeyboardDispatcher()) {
            dispatcher->dispatchKeyboardMSG(enumKeyCodes::KEY_Space, down, false);
        }
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        bool active = g_verificationReplayRequested;
        g_verificationReplayRequested = false;

        if (!PlayLayer::init(level, dontCreateObjects)) {
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

            if (m_player1) {
                m_fields->m_lastPlayerY = m_player1->getPositionY();
            }

            this->schedule(schedule_selector(VerificationReplayPlayLayer::updateVerificationReplay));
        }

        return true;
    }

    void updateVerificationReplay(float dt) {
        if (!m_fields->m_isRobTopReplay || !m_player1) return;

        CCPoint pPos = m_player1->getPosition();
        float vy = (dt > 0.0001f) ? (pPos.y - m_fields->m_lastPlayerY) / dt : 0.f;
        m_fields->m_lastPlayerY = pPos.y;
        m_fields->m_pulseTimer += dt;

        bool hazardAhead = false;
        bool ceilingThreat = false;
        bool orbAhead = false;

        if (m_objects) {
            unsigned int count = m_objects->count();
            for (unsigned int i = 0; i < count; ++i) {
                auto obj = static_cast<CCNode*>(m_objects->objectAtIndex(i));
                if (!obj || !obj->isVisible()) continue;

                CCPoint oPos = obj->getPosition();
                float dx = oPos.x - pPos.x;

                if (dx < -8.f || dx > 110.f) continue;

                float dy = oPos.y - pPos.y;
                CCSize sz = obj->getContentSize();
                float halfH = std::max(14.f, sz.height * std::abs(obj->getScaleY()) * 0.5f);

                // Interactive ring/orb near player path
                if (dx >= -4.f && dx <= 48.f && std::abs(dy) <= 34.f && halfH >= 15.f && halfH <= 26.f) {
                    orbAhead = true;
                }

                // Floor / mid-level obstacle directly in front of player
                if (dx >= 0.f && dx <= 95.f && dy >= -22.f && dy <= (halfH + 16.f)) {
                    hazardAhead = true;
                }

                // Ceiling obstacle right above player
                if (dx >= -6.f && dx <= 85.f && dy > 24.f && dy <= 76.f) {
                    ceilingThreat = true;
                }
            }
        }

        bool wantHold = false;
        if (ceilingThreat && !hazardAhead) {
            wantHold = false;
        } else if (orbAhead) {
            wantHold = (std::fmod(m_fields->m_pulseTimer, 0.14f) < 0.07f);
        } else if (hazardAhead) {
            if (vy > 320.f) {
                wantHold = (std::fmod(m_fields->m_pulseTimer, 0.16f) < 0.08f);
            } else {
                wantHold = true;
            }
        }

        setBotInput(wantHold);
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (m_fields->m_isRobTopReplay) {
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void onExit() {
        setBotInput(false);
        PlayLayer::onExit();
    }
};
