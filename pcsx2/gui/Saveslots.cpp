/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "App.h"
#include "AppSaveStates.h"
#include "ConsoleLogger.h"
#include "MainFrame.h"

#include "Common.h"

#include "GS.h"
#include "Elfheader.h"
#include "Saveslots.h"

// --------------------------------------------------------------------------------------
//  Saveslot Section
// --------------------------------------------------------------------------------------

static int StatesC = 0;
static const int StateSlotsCount = 10;
Saveslot saveslot_cache[10];

// FIXME : Use of the IsSavingOrLoading flag is mostly a hack until we implement a
// complete thread to manage queuing savestate tasks, and zipping states to disk.  --air
static std::atomic<bool> IsSavingOrLoading(false);

class SysExecEvent_ClearSavingLoadingFlag : public SysExecEvent
{
public:
	wxString GetEventName() const { return L"ClearSavingLoadingFlag"; }

	virtual ~SysExecEvent_ClearSavingLoadingFlag() = default;
	SysExecEvent_ClearSavingLoadingFlag() { }
	SysExecEvent_ClearSavingLoadingFlag *Clone() const { return new SysExecEvent_ClearSavingLoadingFlag(); }

protected:
	void InvokeEvent()
	{
		IsSavingOrLoading = false;
		UI_UpdateSysControls();
	}
};

void Sstates_updateLoadBackupMenuItem(bool isBeforeSave = false);

void States_FreezeCurrentSlot()
{
	// FIXME : Use of the IsSavingOrLoading flag is mostly a hack until we implement a
	// complete thread to manage queuing savestate tasks, and zipping states to disk.  --air
	if (!SysHasValidState())
	{
		Console.WriteLn("Save state: Aborting (VM is not active).");
		return;
	}

	if (wxGetApp().HasPendingSaves() || IsSavingOrLoading.exchange(true))
	{
		Console.WriteLn("Load or save action is already pending.");
		return;
	}
	Sstates_updateLoadBackupMenuItem(true);

	GSchangeSaveState(StatesC, SaveStateBase::GetFilename(StatesC).ToUTF8());
	StateCopy_SaveToSlot(StatesC);

	// Hack: Update the saveslot saying it's filled *right now* because it's still writing the file and we don't have a timestamp.
	saveslot_cache[StatesC].empty = false;
	saveslot_cache[StatesC].updated = wxDateTime::Now();
	saveslot_cache[StatesC].crc = ElfCRC;

	GetSysExecutorThread().PostIdleEvent(SysExecEvent_ClearSavingLoadingFlag());
}

void _States_DefrostCurrentSlot(bool isFromBackup)
{
	if (!SysHasValidState())
	{
		Console.WriteLn("Load state: Aborting (VM is not active).");
		return;
	}

	if (IsSavingOrLoading.exchange(true))
	{
		Console.WriteLn("Load or save action is already pending.");
		return;
	}

	GSchangeSaveState(StatesC, SaveStateBase::GetFilename(StatesC).ToUTF8());
	StateCopy_LoadFromSlot(StatesC, isFromBackup);

	GetSysExecutorThread().PostIdleEvent(SysExecEvent_ClearSavingLoadingFlag());

	Sstates_updateLoadBackupMenuItem();
}

void States_DefrostCurrentSlot()
{
	_States_DefrostCurrentSlot(false);
}

void States_DefrostCurrentSlotBackup()
{
	_States_DefrostCurrentSlot(true);
}

// It doesn't seem like this is working at the moment, in that the menu item always seems disabled in tests, but it also doesn't seem like it was working previously...
void Sstates_updateLoadBackupMenuItem(bool isBeforeSave)
{
	wxString file = SaveStateBase::GetFilename(StatesC);

	if (!(isBeforeSave && g_Conf->EmuOptions.BackupSavestate))
	{
		file = file + L".backup";
	}

	sMainFrame.EnableMenuItem(MenuId_State_LoadBackup, wxFileExists(file));
	sMainFrame.SetMenuItemLabel(MenuId_State_LoadBackup, wxsFormat(L"%s %d", _("Backup"), StatesC));
}

static void OnSlotChanged()
{
	OSDlog(Color_StrongGreen, true, " > Selected savestate slot %d", StatesC);

	if (GSchangeSaveState != NULL)
		GSchangeSaveState(StatesC, SaveStateBase::GetFilename(StatesC).utf8_str());

	Sstates_updateLoadBackupMenuItem();
}

int States_GetCurrentSlot()
{
	return StatesC;
}

void States_SetCurrentSlot(int slot)
{
	StatesC = std::min(std::max(slot, 0), StateSlotsCount);
	OnSlotChanged();
}

void States_CycleSlotForward()
{
	StatesC = (StatesC + 1) % StateSlotsCount;
	OnSlotChanged();
}

void States_CycleSlotBackward()
{
	StatesC = (StatesC + StateSlotsCount - 1) % StateSlotsCount;
	OnSlotChanged();
}
