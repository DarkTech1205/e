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

class $modify(SecretModButtonsLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) {
            return false;
        }

        if (auto leftMenu = this->getChildByID("left-side-menu")) {
            // 1. Moderator Send / Rate button (Stars/Moons + Feature/Epic/Legendary/Mythic)
            if (!leftMenu->getChildByID("mod-send-btn")) {
                auto sendSpr = createSpriteWithFallback("GJ_starBtnMod_001.png", "GJ_starBtn_001.png");
                auto sendBtn = CCMenuItemSpriteExtra::create(
                    sendSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onModSend)
                );
                sendBtn->setID("mod-send-btn");
                leftMenu->addChild(sendBtn);
            }

            // 2. Moderator Demon Rating button
            if (!leftMenu->getChildByID("mod-demon-btn")) {
                auto demonSpr = createSpriteWithFallback("GJ_rateDiffBtn_001.png", "diffIcon_06_btn_001.png");
                auto demonBtn = CCMenuItemSpriteExtra::create(
                    demonSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onModDemon)
                );
                demonBtn->setID("mod-demon-btn");
                leftMenu->addChild(demonBtn);
            }

            // 3. Elder Moderator Delete Level button
            if (!leftMenu->getChildByID("elder-delete-btn")) {
                auto delSpr = createSpriteWithFallback("GJ_deleteBtn_001.png", "GJ_trashBtn_001.png");
                auto delBtn = CCMenuItemSpriteExtra::create(
                    delSpr,
                    this,
                    menu_selector(SecretModButtonsLayer::onElderDelete)
                );
                delBtn->setID("elder-delete-btn");
                leftMenu->addChild(delBtn);
            }

            leftMenu->updateLayout();
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
        FLAlertLayer::create(
            "Mod Demon Vote",
            "Moderator Demon rating button unlocked! (RateDemonLayer bindings are not yet mapped for Win/Mac/iOS in GD 2.2091, and RobTop's servers verify mod permissions on submit.)",
            "OK"
        )->show();
    }

    void onElderDelete(CCObject*) {
        FLAlertLayer::create(
            "Elder Mod Delete",
            "This is the hidden Elder Mod delete button. (RobTop's servers verify account permissions on submit, so online levels won't actually be deleted.)",
            "OK"
        )->show();
    }
};
