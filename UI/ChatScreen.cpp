#include <ctype.h>
#include "ppsspp_config.h"

#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/proAdhoc.h"
#include "UI/ChatScreen.h"

void ChatMenu::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto n = GetI18NCategory("Networking");
	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT,400));
	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(SDL)
	chatEdit_ = bottom->Add(new TextEdit("", n->T("Chat Here"), new LinearLayoutParams(1.0)));
#if defined(USING_WIN_UI)
	// freeze the ui when using ctrl + C hotkey need workaround
	if (g_Config.bBypassOSKWithKeyboard && !g_Config.bFullScreen) {
		System_InputBoxGetString(n->T("Chat"), n->T("Chat Here"), [](bool result, const std::string &value) {
			if (result) {
				sendChat(value);
			}
		});
	}
#endif
	chatEdit_->OnEnter.Handle(this, &ChatMenu::OnSubmit);

#elif PPSSPP_PLATFORM(ANDROID)
	bottom->Add(new Button(n->T("Chat Here"),new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->OnClick.Handle(this, &ChatMenu::OnSubmit);
	bottom->Add(new Button(n->T("Send")))->OnClick.Handle(this, &ChatMenu::OnSubmit);
#endif

	if (g_Config.bEnableQuickChat) {
		LinearLayout *quickChat = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		quickChat->Add(new Button("1", new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatMenu::OnQuickChat1);
		quickChat->Add(new Button("2", new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatMenu::OnQuickChat2);
		quickChat->Add(new Button("3", new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatMenu::OnQuickChat3);
		quickChat->Add(new Button("4", new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatMenu::OnQuickChat4);
		quickChat->Add(new Button("5", new LinearLayoutParams(1.0)))->OnClick.Handle(this, &ChatMenu::OnQuickChat5);
	}
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);
	parent->Add(outer);
}

void ChatMenu::CreateViews() {
	using namespace UI;

	auto n = GetI18NCategory("Networking");
	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	float yres = screenManager()->getUIContext()->GetBounds().h;

	switch (g_Config.iChatScreenPosition) {
	// the chat screen size is still static 280x240 need a dynamic size based on device resolution 
	case 0:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, 280, NONE, NONE, 240, true));
		break;
	case 1:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, dc.GetBounds().centerX(), NONE, NONE, 240, true));
		break;
	case 2:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, NONE, NONE, 280, 240, true));
		break;
	case 3:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, 280, 240, NONE, NONE, true));
		break;
	case 4:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, dc.GetBounds().centerX(), 240, NONE, NONE, true));
		break;
	case 5:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(PopupWidth(), FillVertical() ? yres - 30 : WRAP_CONTENT, NONE, 240, 280, NONE, true));
		break;
	default:
		box_ = nullptr;
		break;
	}

	if (box_) {
		root_->Add(box_);
		box_->SetBG(UI::Drawable(0x99303030));
		box_->SetHasDropShadow(false);

		View *title = new PopupHeader(n->T("Chat"));
		box_->Add(title);

		CreatePopupContents(box_);
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(SDL)
		UI::EnableFocusMovement(true);
		root_->SetDefaultFocusView(box_);
		box_->SubviewFocused(chatEdit_);
		root_->SetFocus();
#else
		//root_->SetDefaultFocusView(box_);
		//box_->SubviewFocused(scroll_);
		//root_->SetFocus();
#endif
	}
	chatScreenVisible = true;
	newChat = 0;

	UpdateChat();
}

void ChatMenu::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
}

UI::EventReturn ChatMenu::OnSubmit(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(SDL)
	std::string chat = chatEdit_->GetText();
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	sendChat(chat);
#elif PPSSPP_PLATFORM(ANDROID)
	auto n = GetI18NCategory("Networking");
	System_InputBoxGetString(n->T("Chat"), "", [](bool result, const std::string &value) {
		sendChat(value);
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn ChatMenu::OnQuickChat1(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat0);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatMenu::OnQuickChat2(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat1);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatMenu::OnQuickChat3(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat2);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatMenu::OnQuickChat4(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat3);
	return UI::EVENT_DONE;
}

UI::EventReturn ChatMenu::OnQuickChat5(UI::EventParams &e) {
	sendChat(g_Config.sQuickChat4);
	return UI::EVENT_DONE;
}

void ChatMenu::UpdateChat() {
	using namespace UI;
	if (chatVert_ != nullptr) {
		chatVert_->Clear(); //read Access violation is proadhoc.cpp use NULL_->Clear() pointer?
		std::vector<std::string> chatLog = getChatLog();
		for (auto i : chatLog) {
			uint32_t namecolor = 0x29B6F6;
			uint32_t textcolor = 0xFFFFFF;
			uint32_t infocolor = 0xFDD835;

			std::string name = g_Config.sNickName.c_str();
			std::string displayname = i.substr(0, i.find(':'));
			std::string chattext = i.substr(displayname.length());
			
			if (name.substr(0, 8) == displayname) {
				namecolor = 0xE53935;
			}

			if (i.length() <= displayname.length() || i[displayname.length()] != ':') {
				TextView *v = chatVert_->Add(new TextView(i, ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
				v->SetTextColor(0xFF000000 | infocolor);
			} else {
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
				line->SetSpacing(0.0f);
				TextView *nameView = line->Add(new TextView(displayname, ALIGN_LEFT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f)));
				nameView->SetTextColor(0xFF000000 | namecolor);

				TextView *chatView = line->Add(new TextView(chattext, ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
				chatView->SetTextColor(0xFF000000 | textcolor);
			}
		}
		toBottom_ = true;
	}
}

bool ChatMenu::touch(const TouchInput &touch) {
	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		return UIDialogScreen::touch(touch);
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y)){
		screenManager()->finishDialog(this, DR_BACK);
	}

	return UIDialogScreen::touch(touch);
}

void ChatMenu::update() {
	PopupScreen::update();
	if (scroll_ && toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}

	if (updateChatScreen) {
		UpdateChat();
		updateChatScreen = false;
	}
}

ChatMenu::~ChatMenu() {
	chatScreenVisible = false;
}
