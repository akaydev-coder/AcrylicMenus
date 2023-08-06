#include "pch.h"
#include "MenuHandler.h"
#include "MenuManager.h"
#include "ThemeHelper.h"
#include "AcrylicHelper.h"
#include "WindowHelper.h"
#include "SystemHelper.h"
#include "SettingsHelper.h"
#include "AppearanceConfiguration.h"

#define MN_BUTTONDOWN     0x1ED
#define MN_BUTTONUP       0x1EF
#define WM_REDRAWBORDER   WM_APP + 13

#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWA_BORDER_COLOR 34

#define DWMWCP_ROUNDSMALL 3

using namespace AcrylicMenus;

thread_local HWND g_hWndPrimary = NULL;

void CALLBACK MenuHandler::WinEventProc(
	HWINEVENTHOOK hWinEventHook,
	DWORD dwEvent,
	HWND hWnd,
	LONG idObject,
	LONG idChild,
	DWORD dwEventThread,
	DWORD dwmsEventTime
)
{
	if (!hWnd || !IsWindow(hWnd))
	{
		return;
	}

	switch (dwEvent)
	{
	case EVENT_OBJECT_CREATE:
		{
			if (SystemHelper::IsTransparencyEnabled() && ThemeHelper::IsPopupMenu(hWnd))
			{
				if (!g_hWndPrimary)
				{
					g_hWndPrimary = hWnd;
				}

				SetWindowSubclass(hWnd, SubclassProc, 0, 0);
				MenuManager::SetCurrentMenu(hWnd);
			}
			break;
		}
	case EVENT_OBJECT_SHOW:
		{
			if (g_hWndPrimary && SystemHelper::IsTransparencyEnabled() && ThemeHelper::IsPopupMenu(hWnd))
			{
				if (SystemHelper::g_bIsWindows11)
				{
					if (WIN11_SET_SMALL_CORNERS)
					{
						DWORD dwCornerPreference = DWMWCP_ROUNDSMALL;
						DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &dwCornerPreference, sizeof(DWORD));
					}
					if (WIN11_SET_POPUP_BORDERS)
					{
						COLORREF dwColorBorder = MenuManager::g_bIsDarkMode ? WIN11_POPUP_BORDER_DARK : WIN11_POPUP_BORDER_LIGHT;
						DwmSetWindowAttribute(hWnd, DWMWA_BORDER_COLOR, &dwColorBorder, sizeof(COLORREF));
					}
				}
			}
			break;
		}
	case EVENT_OBJECT_DESTROY:
		{
			if (hWnd == g_hWndPrimary)
			{
				MenuManager::SetCurrentMenu(NULL);
				g_hWndPrimary = NULL;
			}
			break;
		}
	default:
		{
			break;
		}
	}
}

static constexpr int nonClientMarginSize{ 3 };
static constexpr int systemOutlineSize{ 1 };
LRESULT CALLBACK MenuHandler::SubclassProc(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	bool handled = false;
	LRESULT result{ 0 };

	switch (uMsg)
	{
#if (MENU_REDRAW_BORDER == TRUE)
	///
	/// Windows 10 has ugly white context menu borders
	/// As the borders are in the non-client area,
	/// we can't hook the painting event and need
	/// to redraw them manually
	/// 
	/// This is enabled only for light mode menus,
	/// because it is noticeable, due to low contrast
	/// between menu background color and original white border,
	/// but this is very noticeable in dark mode
	/// 
	/// On Windows 11, we change borders color
	/// natively using DwmSetWindowAttribute API
	///
	case WM_PRINT:
		{
			handled = true;

			POINT pt;

			HDC wndDC = (HDC)wParam;
			SaveDC(wndDC);

			RECT rcPaint;
			GetClipBox(wndDC, &rcPaint);
			FillRect(wndDC, &rcPaint, GetStockBrush(BLACK_BRUSH));

			SetViewportOrgEx(wndDC, nonClientMarginSize, nonClientMarginSize, &pt);
			result = DefSubclassProc(hWnd, WM_PRINTCLIENT, wParam, lParam);

			SetViewportOrgEx(wndDC, pt.x, pt.y, nullptr);
			
			RestoreDC(wndDC, -1);
		}
		break;
	case WM_NCPAINT:
		{
			handled = true;

			HDC wndDC = GetWindowDC(hWnd);

			if (wParam != NULLREGION && wParam != ERROR)
			{
				SelectClipRgn(wndDC, reinterpret_cast<HRGN>(wParam));
			}

			RECT rcPaint;
			GetClipBox(wndDC, &rcPaint);
			FillRect(wndDC, &rcPaint, GetStockBrush(BLACK_BRUSH));

			ReleaseDC(hWnd, wndDC);
		}
		break;
#endif
	case MN_BUTTONUP:
		{
			// We need to prevent the system default menu fade out animation
			// and begin a re-implemented one
			// 
			// Windows does not show animation if the selection was done
			// with keyboard (i.e. Enter)

			int pvParam;
			SystemParametersInfoW(SPI_GETSELECTIONFADE, 0, &pvParam, 0);
			if (!pvParam)
			{
				// Fade out animation is disabled system-wide
				break;
			}

			HMENU hMenu = (HMENU)SendMessage(hWnd, MN_GETHMENU, 0, 0);
			int iPosition = (int)wParam;

			MENUITEMINFO mii;
			ZeroMemory(&mii, sizeof(MENUITEMINFO));
			mii.cbSize = sizeof(MENUITEMINFO);
			mii.fMask = MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;

			GetMenuItemInfo(hMenu, iPosition, TRUE, &mii);

			// Animation will be shown if a menu item is selected
			// 
			// We will ignore cases where user clicked on non-clickable item, i.e.:
			//  a) separator
			//  b) disabled item
			//  c) item that has submenu

			if (!(mii.fType & MFT_SEPARATOR) && !(mii.fState & MFS_DISABLED) && !mii.hSubMenu)
			{
				MENUBARINFO pmbi;
				ZeroMemory(&pmbi, sizeof(MENUBARINFO));   // memset(&info, 0, sizeof(MENUBARINFO));
				pmbi.cbSize = sizeof(MENUBARINFO);
				ZeroMemory(&pmbi.rcBar, 0, sizeof(RECT));

				GetMenuBarInfo(hWnd, OBJID_CLIENT, wParam + 1, &pmbi);

				WindowHelper::BeginMenuFadeOutAnimation(pmbi);

				SystemParametersInfoW(SPI_SETSELECTIONFADE, 0, FALSE, 0);
				LRESULT lResult = DefSubclassProc(hWnd, uMsg, wParam, lParam);
				SystemParametersInfoW(SPI_SETSELECTIONFADE, 0, (PVOID)TRUE, 0);

				return lResult;
			}
		}
		break;
	}

	if (!handled)
	{
		result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	return result;
}