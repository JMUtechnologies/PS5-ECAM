// SPDX-License-Identifier: MPL-2.0
// PS5CEMU-HAR: the Wii U's accounts for the launcher (emulator.h; docs/UI-REDESIGN.md, 6.13), as Cemu's
// Account settings make, rename and delete them (src/gui/wxgui/GeneralSettings2.cpp, OnAccountCreate
// and OnAccountDelete), so this file keeps Cemu's MPL-2.0 licence. Each account has its own saves of
// every game (mlc01/usr/save/<title>/user/<persistent ID>); the one a game starts on is Cemu's own
// setting (settings.xml, Account/PersistentId), read as the game starts. Deleting an account leaves
// its saves in the Wii U's storage, as Cemu does.

#include "emulator.h"
#include "lang.h"
#include "../ps5/log.h"

#include "Cafe/Account/Account.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"

#include <boost/nowide/convert.hpp>

namespace ps5emu
{
	namespace
	{
		std::string NameOf(const Account& account)
		{
			return boost::nowide::narrow(std::wstring(account.GetMiiName()));
		}

		void SetCurrent(uint32_t id)
		{
			GetConfig().account.m_persistent_id = id;
			GetConfigHandle().Save();
		}
	}

	std::vector<WiiuAccount> ListAccounts()
	{
		std::vector<WiiuAccount> accounts;
		for (const Account& account : Account::GetAccounts())
			accounts.push_back({account.GetPersistentId(), NameOf(account)});
		return accounts;
	}

	void ReloadAccounts()
	{
		Account::RefreshAccounts();
	}

	uint32_t CurrentAccount()
	{
		return Account::GetCurrentAccount().GetPersistentId();
	}

	void SetCurrentAccount(uint32_t id)
	{
		for (const Account& account : Account::GetAccounts())
			if (account.GetPersistentId() == id)
			{
				SetCurrent(id);
				ps5log::Line("[accounts] games start on {} ({:08x})", NameOf(account), id);
				return;
			}
	}

	uint32_t CreateAccount(const std::string& name, std::string& error)
	{
		if (!Account::HasFreeAccountSlots())
		{
			error = ps5lang::Tr("The Wii U has twelve accounts at most.");
			return 0;
		}
		const uint32_t id = Account::GetNextPersistentId();
		Account account(id, boost::nowide::widen(name.substr(0, 10)));
		if (const std::error_code ec = account.Save())
		{
			error = ec.message();
			return 0;
		}
		Account::RefreshAccounts();
		SetCurrent(id);
		ps5log::Line("[accounts] {} made ({:08x}); games start on it", name, id);
		return id;
	}

	bool RenameAccount(uint32_t id, const std::string& name)
	{
		for (const Account& existing : Account::GetAccounts())
			if (existing.GetPersistentId() == id)
			{
				Account account = existing;
				account.SetMiiName(boost::nowide::widen(name.substr(0, 10)));
				if (account.Save())
					return false;
				Account::RefreshAccounts();
				return true;
			}
		return false;
	}

	bool DeleteAccount(uint32_t id, std::string& error)
	{
		const auto& accounts = Account::GetAccounts();
		if (accounts.size() <= 1)
		{
			error = ps5lang::Tr("The last account stays.");
			return false;
		}
		if (id == CurrentAccount())
		{
			error = ps5lang::Tr("Games start on this one: choose another first.");
			return false;
		}
		for (const Account& account : accounts)
			if (account.GetPersistentId() == id)
			{
				// its file's folder, as Cemu deletes it; its saves stay where they are
				std::error_code ec;
				const std::string name = NameOf(account);
				fs::remove_all(account.GetFileName().parent_path(), ec);
				if (ec)
				{
					error = ec.message();
					return false;
				}
				Account::RefreshAccounts();
				ps5log::Line("[accounts] {} ({:08x}) deleted; its saves stay", name, id);
				return true;
			}
		error = ps5lang::Tr("There is no such account.");
		return false;
	}

	uint32_t AccountForUser(const std::string& user, const std::string& userName, std::map<std::string, std::string>& byUser)
	{
		// the account this PS5 user has had, while it is there
		if (const auto it = byUser.find(user); it != byUser.end())
		{
			const uint32_t id = (uint32_t)std::strtoul(it->second.c_str(), nullptr, 16);
			for (const Account& account : Account::GetAccounts())
				if (account.GetPersistentId() == id)
				{
					SetCurrent(id);
					return id;
				}
		}
		// else one of their own, named after them
		std::string error;
		const uint32_t id = CreateAccount(userName.empty() ? std::string("Player") : userName, error);
		if (!id)
		{
			ps5log::Line("[accounts] no account for PS5 user {}: {}", user, error);
			return CurrentAccount();
		}
		byUser[user] = fmt::format("{:08x}", id);
		return id;
	}
}
