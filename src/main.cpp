#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

static CCSprite* createSpriteWithFallback(const char* primary, const char* fallback) {
    if (auto spr = CCSprite::createWithSpriteFrameName(primary)) {
        return spr;
    }
    if (auto spr = CCSprite::createWithSpriteFrameName(fallback)) {
        return spr;
    }
    return CCSprite::create();
}

static void bindAlertButton2(FLAlertLayer* alert, CCObject* target, SEL_MenuHandler selector) {
    if (!alert) return;
    if (alert->m_button2) {
        if (auto item = typeinfo_cast<CCMenuItem*>(alert->m_button2->getParent())) {
            item->setTarget(target, selector);
            return;
        }
    }
    if (alert->m_buttonMenu && alert->m_buttonMenu->getChildrenCount() > 0) {
        if (auto item = typeinfo_cast<CCMenuItem*>(alert->m_buttonMenu->getChildren()->lastObject())) {
            item->setTarget(target, selector);
        }
    }
}

// Interactive Demon Rating popup for Win/Mac/iOS (where RateDemonLayer isn't bound in 2.2091 yet)
class DemonPopupController : public CCNode {
public:
    FLAlertLayer* m_alert = nullptr;
    CCLabelBMFont* m_diffLabel = nullptr;
    CCArray* m_demonBtns = nullptr;
    int m_selectedDemon = 3; // Default: Hard Demon

    static DemonPopupController* create(FLAlertLayer* alert) {
        auto ret = new DemonPopupController();
        if (ret && ret->init()) {
            ret->m_alert = alert;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    ~DemonPopupController() override {
        CC_SAFE_RELEASE(m_demonBtns);
    }

    bool init() override {
        if (!CCNode::init()) return false;
        m_demonBtns = CCArray::create();
        m_demonBtns->retain();
        return true;
    }

    void setupUI() {
        if (!m_alert || !m_alert->m_mainLayer || !m_alert->m_buttonMenu) return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        const char* frames[5] = {
            "diffIcon_07_btn_001.png", // Easy Demon
            "diffIcon_08_btn_001.png", // Medium Demon
            "diffIcon_06_btn_001.png", // Hard Demon
            "diffIcon_09_btn_001.png", // Insane Demon
            "diffIcon_10_btn_001.png"  // Extreme Demon
        };

        auto menu = CCMenu::create();
        menu->setPosition({winSize.width / 2.f, winSize.height / 2.f + 8.f});
        m_alert->m_mainLayer->addChild(menu, 10);

        float spacing = 58.f;
        float startX = -spacing * 2.f;

        for (int i = 0; i < 5; ++i) {
            auto spr = createSpriteWithFallback(frames[i], "diffIcon_06_btn_001.png");
            auto btn = CCMenuItemSpriteExtra::create(
                spr,
                this,
                menu_selector(DemonPopupController::onSelectDemon)
            );
            btn->setTag(i + 1);
            btn->setPosition({startX + i * spacing, 0.f});
            menu->addChild(btn);
            m_demonBtns->addObject(btn);
        }

        m_diffLabel = CCLabelBMFont::create("Hard Demon", "goldFont.fnt");
        m_diffLabel->setScale(0.75f);
        m_diffLabel->setPosition({winSize.width / 2.f, winSize.height / 2.f - 36.f});
        m_alert->m_mainLayer->addChild(m_diffLabel, 10);

        bindAlertButton2(m_alert, this, menu_selector(DemonPopupController::onSubmit));
        updateSelection();
    }

    void updateSelection() {
        const char* names[5] = {
            "Easy Demon",
            "Medium Demon",
            "Hard Demon",
            "Insane Demon",
            "Extreme Demon"
        };

        if (m_selectedDemon >= 1 && m_selectedDemon <= 5 && m_diffLabel) {
            m_diffLabel->setString(names[m_selectedDemon - 1]);
        }

        for (unsigned int i = 0; i < m_demonBtns->count(); ++i) {
            auto btn = static_cast<CCMenuItemSpriteExtra*>(m_demonBtns->objectAtIndex(i));
            bool selected = (btn->getTag() == m_selectedDemon);
            btn->setScale(selected ? 1.15f : 0.85f);
            if (auto spr = typeinfo_cast<CCSprite*>(btn->getNormalImage())) {
                spr->setColor(selected ? ccc3(255, 255, 255) : ccc3(145, 145, 145));
            }
        }
    }

    void onSelectDemon(CCObject* sender) {
        m_selectedDemon = sender->getTag();
        updateSelection();
    }

    void onSubmit(CCObject*) {
        const char* names[5] = {
            "Easy Demon",
            "Medium Demon",
            "Hard Demon",
            "Insane Demon",
            "Extreme Demon"
        };
        std::string msg = fmt::format("Rated {}!", names[m_selectedDemon - 1]);
        if (m_alert) {
            m_alert->removeFromParentAndCleanup(true);
        }
        Notification::create(msg, NotificationIcon::Success)->show();
    }
};

// Controller for the Elder Mod Delete confirmation popup
class DeletePopupController : public CCNode {
public:
    FLAlertLayer* m_alert = nullptr;

    static DeletePopupController* create(FLAlertLayer* alert) {
        auto ret = new DeletePopupController();
        if (ret && ret->init()) {
            ret->m_alert = alert;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void setup() {
        bindAlertButton2(m_alert, this, menu_selector(DeletePopupController::onConfirmDelete));
    }

    void onConfirmDelete(CCObject*) {
        if (m_alert) {
            m_alert->removeFromParentAndCleanup(true);
        }
        Notification::create("Level deleted!", NotificationIcon::Success)->show();
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

        // 1. LEFT SIDE MENU: Elder Mod Delete button (at the top of left-side-menu)
        if (auto leftMenu = this->getChildByID("left-side-menu")) {
            if (!leftMenu->getChildByID("elder-delete-btn")) {
                auto delSpr = createSpriteWithFallback("GJ_deleteBtn_001.png", "GJ_trashBtn_001.png");
                auto delBtn = CCMenuItemSpriteExtra::create(
                    delSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onElderDelete)
                );
                delBtn->setID("elder-delete-btn");
                delBtn->setZOrder(-10);
                leftMenu->addChild(delBtn);
                leftMenu->updateLayout();
            }
        }

        // 2. RIGHT SIDE MENU: Demon Rating + Green-Tinted Moderator Send/Rate button
        if (auto rightMenu = this->getChildByID("right-side-menu")) {
            if (!rightMenu->getChildByID("mod-demon-btn")) {
                auto demonSpr = createSpriteWithFallback("GJ_rateDiffBtn_001.png", "diffIcon_06_btn_001.png");
                auto demonBtn = CCMenuItemSpriteExtra::create(
                    demonSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onModDemon)
                );
                demonBtn->setID("mod-demon-btn");
                demonBtn->setZOrder(10);
                rightMenu->addChild(demonBtn);
            }

            if (!rightMenu->getChildByID("mod-send-btn")) {
                auto sendSpr = createSpriteWithFallback("GJ_starBtnMod_001.png", "GJ_starBtn_001.png");
                sendSpr->setColor(ccc3(120, 255, 120));
                auto sendBtn = CCMenuItemSpriteExtra::create(
                    sendSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onModSend)
                );
                sendBtn->setID("mod-send-btn");
                sendBtn->setZOrder(20);
                rightMenu->addChild(sendBtn);
            }

            rightMenu->updateLayout();
        }

        // 3. CENTER PLAY MENU: RobTop's mysterious 2nd Play button next to the main Play button
        if (auto playMenu = this->getChildByID("play-menu")) {
            if (!this->getChildByID("robtop-second-play-menu")) {
                CCPoint mainPlayPos = {0.f, 51.f};
                if (auto mainPlayBtn = playMenu->getChildByID("play-button")) {
                    mainPlayPos = mainPlayBtn->getPosition();
                } else if (playMenu->getChildrenCount() > 0) {
                    if (auto firstChild = static_cast<CCNode*>(playMenu->getChildren()->objectAtIndex(0))) {
                        mainPlayPos = firstChild->getPosition();
                    }
                }

                auto secondPlayMenu = CCMenu::create();
                secondPlayMenu->setID("robtop-second-play-menu");
                secondPlayMenu->setPosition(playMenu->getPosition());

                auto secondSpr = createSpriteWithFallback("GJ_playBtn2_001.png", "GJ_playBtn_001.png");
                secondSpr->setScale(0.68f);

                auto secondBtn = CCMenuItemSpriteExtra::create(
                    secondSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onRobTopSecondPlay)
                );
                secondBtn->setID("robtop-second-play-btn");
                secondBtn->setPosition({mainPlayPos.x + 92.f, mainPlayPos.y});

                secondPlayMenu->addChild(secondBtn);
                this->addChild(secondPlayMenu, playMenu->getZOrder());
            }
        }

        return true;
    }

    void onModSend(CCObject*) {
        int levelID = static_cast<int>(m_level->m_levelID);
        if (auto layer = RateStarsLayer::create(levelID, m_level->isPlatformer(), true)) {
            layer->show();
        }
    }

    void onModDemon(CCObject*) {
#ifdef GEODE_IS_ANDROID
        int levelID = static_cast<int>(m_level->m_levelID);
        if (auto layer = RateDemonLayer::create(levelID)) {
            layer->show();
            return;
        }
#endif
        auto alert = FLAlertLayer::create(
            nullptr,
            "Rate Demon",
            "\n\n\n\n",
            "Cancel",
            "Submit",
            360.f
        );
        if (alert) {
            auto controller = DemonPopupController::create(alert);
            alert->m_mainLayer->addChild(controller);
            controller->setupUI();
            alert->show();
        }
    }

    void onElderDelete(CCObject*) {
        auto alert = FLAlertLayer::create(
            nullptr,
            "Delete Level",
            "Are you sure you want to <cr>delete</c> this level?",
            "NO",
            "YES",
            320.f
        );
        if (alert) {
            auto controller = DeletePopupController::create(alert);
            alert->m_mainLayer->addChild(controller);
            controller->setup();
            alert->show();
        }
    }

    void onRobTopSecondPlay(CCObject*) {
        Notification::create("RobTop Secret Play", NotificationIcon::Info)->show();
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
