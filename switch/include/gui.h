// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_GUI_H
#define CHIAKI_GUI_H

#include <map>
#include <thread>
#include <fmt/format.h>

#include <borealis.hpp>
#include "discoverymanager.h"
#include "host.h"
#include "io.h"
#include "settings.h"
#include "switch.h"
#include "views/enter_pin_view.h"
#include "views/ps_remote_play.h"

namespace brls
{

class List : public ScrollingFrame
{
	private:
		Box *content_box = nullptr;
		const float row_spacing = 10.0f;

	public:
		List()
			: ScrollingFrame()
		{
			Style style = getStyle();
			const float horizontal_inset = 8.0f;
			setGrow(1.0f);
			setScrollingBehavior(ScrollingBehavior::NATURAL);

			content_box = new Box(Axis::COLUMN);
			content_box->setGrow(1.0f);
			// Keep list rows clear of each other without crowding the bottom area.
			float padding_top = style["brls/tab_details/padding_top"] + 16.0f;
			float padding_bottom = style["brls/tab_details/padding_bottom"] + 16.0f;
			content_box->setPadding(
				padding_top,
				style["brls/tab_details/padding_right"] + horizontal_inset,
				padding_bottom,
				style["brls/tab_details/padding_left"] + horizontal_inset);
			setContentView(content_box);
		}

		void addView(View *view) override
		{
			if(content_box)
			{
				// Add clear separation between rows to avoid visual crowding/overlap.
				view->setMarginBottom(row_spacing);
				content_box->addView(view);
			}
		}

		void removeView(View *view, bool free = true) override
		{
			if(content_box)
				content_box->removeView(view, free);
		}

		void clearViews(bool free = true) override
		{
			if(content_box)
				content_box->clearViews(free);
		}
};

class ListItem : public DetailCell
{
	private:
		GenericEvent click_event;

	public:
		explicit ListItem(const std::string &title, const std::string &value = "")
		{
			setText(title);
			setDetailText(value);
			registerClickAction([this](View *view) {
				click_event.fire(view);
				return true;
			});
		}

		GenericEvent *getClickEvent()
		{
			return &click_event;
		}

		void setValue(const std::string &value)
		{
			setDetailText(value);
		}
};

class InputListItem : public InputCell
{
	private:
		GenericEvent click_event;

	public:
		InputListItem(
			const std::string &title,
			const std::string &value,
			const std::string &placeholder = "",
			const std::string &hint = "",
			int maxInputLength = 32,
			int kbdDisableBitmask = 0)
		{
			init(title, value, [this](std::string text) {
				(void)text;
				click_event.fire(this);
			}, placeholder, hint, maxInputLength, kbdDisableBitmask);
		}

		GenericEvent *getClickEvent()
		{
			return &click_event;
		}
};

class SelectListItem : public SelectorCell
{
	private:
		Event<int> value_selected_event;

	public:
		SelectListItem(const std::string &title, std::vector<std::string> options, int selected = 0)
		{
			init(title, options, selected, [this](int value) {
				value_selected_event.fire(value);
			});
		}

		Event<int> *getValueSelectedEvent()
		{
			return &value_selected_event;
		}

		int getSelectedValue() const
		{
			return const_cast<SelectListItem *>(this)->getSelection();
		}
};

} // namespace brls

class HostInterface : public brls::List
{
	private:
		IO *io;
		Host *host;
		Settings *settings;
		ChiakiLog *log = nullptr;
		bool connected = false;
		DiscoveryManager *discoverymanager = nullptr;

		brls::ListItem *connectButton = nullptr;
		std::string discoveryCbKey;
		size_t statusPollId = 0;
		bool statusPollActive = false;

		void UpdateConnectButton();
		void ScheduleStatusPoll();

	public:
		HostInterface(Host *host, DiscoveryManager *dm = nullptr);
		~HostInterface();

		static void Register(Host *host, std::function<void()> success_cb = nullptr);
		void Register();
		void Wakeup(brls::View *view);
		void Connect(brls::View *view);
		void ConnectSession();
		void Disconnect();
		void Stream();
		void EnterPin(bool isError);
		void CloseStream(ChiakiQuitEvent *quit);
};

class MainApplication
{
	private:
		Settings *settings;
		ChiakiLog *log;
		DiscoveryManager *discoverymanager;
		IO *io;
		brls::TabFrame *rootFrame;

		bool BuildConfigurationMenu(brls::List *, Host *host = nullptr);
		void BuildAddHostConfigurationMenu(brls::List *);

	public:
		MainApplication(DiscoveryManager *discoverymanager);
		~MainApplication();
		bool Load();
};

#endif // CHIAKI_GUI_H
