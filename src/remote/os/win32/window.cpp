/*
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */
#include "firebird.h"
#include <windows.h>
#include <stdio.h>
#include <shellapi.h>
#include <prsht.h>
#include <dbt.h>

#include "../jrd/common.h"
#include "../jrd/license.h"
#include "../jrd/isc.h"
#include "../jrd/file_params.h"
#include "../remote/remote_def.h"
#include "../remote/os/win32/window.rh"
#include "../remote/os/win32/property.rh"

#include "../jrd/svc_proto.h"
#include "../jrd/sch_proto.h"
#include "../common/thd.h"
#include "../jrd/thread_proto.h"
#include "../jrd/jrd_proto.h"
#include "../remote/os/win32/window_proto.h"
#include "../remote/os/win32/propty_proto.h"
#include "../jrd/gds_proto.h"

#include "../remote/os/win32/window.h"
#include "../jrd/isc_proto.h"

#define NO_PORT
#include "../remote/protocol.h"
#include "../remote/serve_proto.h"
#undef NO_PORT

#include "../common/config/config.h"


HWND hPSDlg = NULL;
static HINSTANCE hInstance = NULL;
static USHORT usServerFlags;

// Static functions to be called from this file only.
static void GetDriveLetter(ULONG, char pchBuf[DRV_STRINGLEN]);
#ifdef NOT_USED_OR_REPLACED
static char *MakeVersionString(char *, int, USHORT);
#endif
static BOOL CanEndServer(HWND, bool);

// Window Procedure
void WINDOW_shutdown(ULONG);
LRESULT CALLBACK WindowFunc(HWND, UINT, WPARAM, LPARAM);


int WINDOW_main( HINSTANCE hThisInst, int nWndMode, USHORT usServerFlagMask)
{
/******************************************************************************
 *
 *  W I N D O W _ m a i n
 *
 ******************************************************************************
 *
 *  Input:  hThisInst - Handle to the current instance
 *          nWndMode - The mode to be used in the call ShowWindow()
 *          usServerFlagMask - The Server Mask specifying various server flags
 *
 *  Return: (int) 0 if returning before entering message loop
 *          wParam if returning after WM_QUIT
 *
 *  Description: This function registers the main window class, creates the
 *               window and it also contains the message loop. This func. is a
 *               substitute for the regular WinMain() function.
 *****************************************************************************/
	HWND hWnd = NULL;
	hInstance = hThisInst;
	usServerFlags = usServerFlagMask;

/* initialize main window */

	WNDCLASS wcl;
	wcl.hInstance = hInstance;
	wcl.lpszClassName = szClassName;
	wcl.lpfnWndProc = WindowFunc;
	wcl.style = 0;
	wcl.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IBSVR));
	wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcl.lpszMenuName = NULL;
	wcl.cbClsExtra = 0;
	wcl.cbWndExtra = 0;

	wcl.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);

	if (!RegisterClass(&wcl)) {
		char szMsgString[MSG_STRINGLEN];
		LoadString(hInstance, IDS_REGERROR, szMsgString, MSG_STRINGLEN);
		if (usServerFlagMask & SRVR_non_service) {
		   	MessageBox(NULL, szMsgString, APP_LABEL, MB_OK);
		}
		gds__log(szMsgString);
		return 0;
	}

	hWnd = CreateWindowEx(0,
						  szClassName,
						  APP_NAME,
						  WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX,
						  CW_USEDEFAULT,
						  CW_USEDEFAULT,
						  APP_HSIZE,
						  APP_VSIZE, HWND_DESKTOP, NULL, hInstance, NULL);
#ifdef SERVER_SHUTDOWN
	SVC_shutdown_init(WINDOW_shutdown, (ULONG) hWnd);
#endif
// Do the proper ShowWindow depending on if the app is an icon on
// the desktop, or in the task bar.

	SendMessage(hWnd, WM_COMMAND, IDM_CANCEL, 0);
	UpdateWindow(hWnd);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (hPSDlg)				// If property sheet dialog is open
		{
			// Check if the message is property sheet dialog specific
			const BOOL bPSMsg = PropSheet_IsDialogMessage(hPSDlg, &msg);

			// Check if the property sheet dialog is still valid, if not destroy it
			if (!PropSheet_GetCurrentPageHwnd(hPSDlg)) {
				DestroyWindow(hPSDlg);
				hPSDlg = NULL;
			}
			if (bPSMsg) {
				continue;
			}
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}



LRESULT CALLBACK WindowFunc(HWND hWnd,
							UINT message, WPARAM wParam, LPARAM lParam)
{
/******************************************************************************
 *
 *  W i n d o w F u n c
 *
 ******************************************************************************
 *
 *  Input:  hWnd - Handle to the window
 *          message - Message ID
 *          wParam - WPARAM parameter for the message
 *          lParam - LPARAM parameter for the message
 *
 *  Return: FALSE indicates that the message has not been handled
 *          TRUE indicates the message has been handled 
 *
 *  Description: This is main window procedure for the Firebird server. This
 *               traps all the Firebird significant messages and processes
 *               them.
 *****************************************************************************/
	static ULONG ulLastMask = 0L;
	static BOOL bInTaskBar = FALSE;
	static bool bStartup = false;

	ULONG ulInUseMask = 0L;
	PDEV_BROADCAST_VOLUME pdbcv;
	char szDrives[DRV_STRINGLEN];
	ULONG num_att = 0;
	ULONG num_dbs = 0;

	switch (message) {
	case WM_QUERYENDSESSION:
		/* If we are running as a non-service server, then query the user
		 * to determine if we should end the session.  Otherwise, assume that
		 * the server is a service  and could be servicing remote clients and
		 * therefore should not be shut down.  
		 */
		if (usServerFlags & SRVR_non_service) {
			return CanEndServer(hWnd, true);
		}

		return TRUE;

	case WM_CLOSE:
		/* If we are running as a non-service server, then query the user
		 * to determine if we should end the session.  Otherwise, assume that
		 * the server is a service  and could be servicing remote clients and
		 * therefore should not be shut down.  The DestroyWindow() will destroy
		 * the hidden window created by the server for IPC.  This should get
		 * destroyed when the user session ends. 
		 */
		if (usServerFlags & SRVR_non_service) {
			if (CanEndServer(hWnd, false)) {
				if (GetPriorityClass(GetCurrentProcess()) !=
					NORMAL_PRIORITY_CLASS)
				{
					SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
				}
#ifdef DEBUG_GDS_ALLOC
				//gds_alloc_report(ALLOC_verbose, "from server", 0);
				char fn[] = __FILE__;
				fn[strlen(fn) - 19] = 0; // all remote files
				gds_alloc_report(ALLOC_verbose, fn, 0);
#endif
				JRD_shutdown_all(false);
				SRVR_shutdown();
				DestroyWindow(hWnd);
			}
		}
		break;

	case WM_COMMAND:
		switch (wParam) {
		case IDM_CANCEL:
			if ((usServerFlags & SRVR_non_service)
				&& (!(usServerFlags & SRVR_no_icon))) 
			{
				ShowWindow(hWnd, bInTaskBar ? SW_HIDE : SW_MINIMIZE);
			}
			else
				ShowWindow(hWnd, SW_HIDE);
			return TRUE;

		case IDM_OPENPOPUP:
			{
				char szMsgString[MSG_STRINGLEN];

				// The SetForegroundWindow() has to be called because our window
				// does not become the Foreground one (inspite of clicking on
				//the icon).  This is so because the icon is painted on the task
				//bar and is not the same as a minimized window.
				SetForegroundWindow(hWnd);

				HMENU hPopup = CreatePopupMenu();
				LoadString(hInstance, IDS_SHUTDOWN, szMsgString,
						   MSG_STRINGLEN);
				AppendMenu(hPopup, MF_STRING, IDM_SHUTDOWN, szMsgString);
				LoadString(hInstance, IDS_PROPERTIES, szMsgString,
						   MSG_STRINGLEN);
				AppendMenu(hPopup, MF_STRING, IDM_PROPERTIES, szMsgString);
				SetMenuDefaultItem(hPopup, IDM_PROPERTIES, FALSE);

				POINT curPos;
				GetCursorPos(&curPos);
				TrackPopupMenu(hPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
							   curPos.x, curPos.y, 0, hWnd, NULL);
				DestroyMenu(hPopup);
				return TRUE;
			}

		case IDM_SHUTDOWN:
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			return TRUE;

		case IDM_PROPERTIES:
			if (!hPSDlg)
				hPSDlg = DisplayProperties(hWnd, hInstance, usServerFlags);
			else
				SetForegroundWindow(hPSDlg);
			return TRUE;

		case IDM_GUARDED:
			{
				/* Since we are going to be guarded, we do not need to
				 * show the server icon.  The guardian will show its own. */
				NOTIFYICONDATA nid;
				nid.cbSize = sizeof(NOTIFYICONDATA);
				nid.hWnd = hWnd;
				nid.uID = IDI_IBSVR;
				nid.uFlags = 0;
				Shell_NotifyIcon(NIM_DELETE, &nid);
			}
			return TRUE;
		}
		break;

	case ON_NOTIFYICON:
		if (bStartup) {
			SendMessage(hWnd, WM_COMMAND, 0, 0);
			return TRUE;
		}
		switch (lParam) {
		case WM_LBUTTONDOWN:
			break;

		case WM_LBUTTONDBLCLK:
			PostMessage(hWnd, WM_COMMAND, (WPARAM) IDM_PROPERTIES, 0);
			break;

		case WM_RBUTTONUP:
			// The TrackPopupMenu() is inconsistant if called from here? 
			// This is the only way I could make it work.
			PostMessage(hWnd, WM_COMMAND, (WPARAM) IDM_OPENPOPUP, 0);
			break;
		}
		break;

	case WM_CREATE:
		if ((usServerFlags & SRVR_non_service)
			&& (!(usServerFlags & SRVR_no_icon))) 
		{
			HICON hIcon = (HICON) LoadImage(hInstance,
									  MAKEINTRESOURCE(IDI_IBSVR_SMALL),
									  IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);

			NOTIFYICONDATA nid;
			nid.cbSize = sizeof(NOTIFYICONDATA);
			nid.hWnd = hWnd;
			nid.uID = IDI_IBSVR;
			nid.uFlags = NIF_TIP | NIF_ICON | NIF_MESSAGE;
			nid.uCallbackMessage = ON_NOTIFYICON;
			nid.hIcon = hIcon;
			lstrcpy(nid.szTip, GDS_VERSION);

			// This will be true in the explorer interface 
			bInTaskBar = Shell_NotifyIcon(NIM_ADD, &nid);

			if (hIcon)
				DestroyIcon(hIcon);

			// This will be true in the Program Manager interface.
			if (!bInTaskBar) {
				char szMsgString[MSG_STRINGLEN];

				HMENU hSysMenu = GetSystemMenu(hWnd, FALSE);
				DeleteMenu(hSysMenu, SC_RESTORE, MF_BYCOMMAND);
				AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
				LoadString(hInstance, IDS_SHUTDOWN, szMsgString,
						   MSG_STRINGLEN);
				AppendMenu(hSysMenu, MF_STRING, IDM_SHUTDOWN, szMsgString);
				LoadString(hInstance, IDS_PROPERTIES, szMsgString,
						   MSG_STRINGLEN);
				AppendMenu(hSysMenu, MF_STRING, IDM_PROPERTIES, szMsgString);
				DestroyMenu(hSysMenu);
			}
		}
		break;

	case WM_QUERYOPEN:
		if (!bInTaskBar)
			return FALSE;
		return DefWindowProc(hWnd, message, wParam, lParam);
	case WM_SYSCOMMAND:
		if (!bInTaskBar)
			switch (wParam) {
			case SC_RESTORE:
				return TRUE;

			case IDM_SHUTDOWN:
				PostMessage(hWnd, WM_CLOSE, 0, 0);
				return TRUE;

			case IDM_PROPERTIES:
				if (!hPSDlg)
					hPSDlg =
						DisplayProperties(hWnd, hInstance, usServerFlags);
				else
					SetFocus(hPSDlg);
				return TRUE;

			}
		return DefWindowProc(hWnd, message, wParam, lParam);

	case WM_DESTROY:
		if (bInTaskBar) {
			NOTIFYICONDATA nid;

			nid.cbSize = sizeof(NOTIFYICONDATA);
			nid.hWnd = hWnd;
			nid.uID = IDI_IBSVR;
			nid.uFlags = 0;

			Shell_NotifyIcon(NIM_DELETE, &nid);
		}

		PostQuitMessage(0);
		break;

	case WM_DEVICECHANGE:
		pdbcv = (PDEV_BROADCAST_VOLUME) lParam;
		JRD_num_attachments(reinterpret_cast<char*>(&ulInUseMask),
							sizeof(ULONG), JRD_info_drivemask, &num_att,
							&num_dbs);

		switch (wParam) {

		case DBT_DEVICEARRIVAL:
			return TRUE;

		case DBT_DEVICEQUERYREMOVE:
			if (CHECK_VOLUME(pdbcv) && (ulLastMask != pdbcv->dbcv_unitmask))
				if (CHECK_USAGE(pdbcv)) {
					char tmp[TMP_STRINGLEN];
					char* p = tmp;
					int len = LoadString(hInstance, IDS_PNP1, p, TMP_STRINGLEN);
					p += len;
					*p++ = '\r';
					*p++ = '\n';

					len =
						LoadString(hInstance, IDS_PNP2, p,
								   p - tmp + TMP_STRINGLEN);
					p += len;
					*p++ = '\r';
					*p++ = '\n';
					len =
						LoadString(hInstance, IDS_PNP3, p,
								   p - tmp + TMP_STRINGLEN);
					ulLastMask = pdbcv->dbcv_unitmask;
					GetDriveLetter(pdbcv->dbcv_unitmask, szDrives);
					if (MessageBox(hWnd, tmp,
								   szDrives,
								   MB_OKCANCEL | MB_ICONHAND) == IDCANCEL)
					{
						return FALSE;
					}

					JRD_shutdown_all(false);
					SRVR_shutdown();
					PostMessage(hWnd, WM_DESTROY, 0, 0);
					return TRUE;
				}
			/* Fall through to MOVEPENDING if we receive a QUERYDEVICE for the
			   * same device twice.  This will occur if we say yes to the removal
			   * of a controller.  The OS will prompt for the removal of all 
			   * devices connected to that controller.  If you respond no, the
			   * OS will prompt you again, and then remove the device anyway...
			 */
		case DBT_DEVICEREMOVEPENDING:
			if (CHECK_VOLUME(pdbcv) && CHECK_USAGE(pdbcv)) {
				char tmp[TMP_STRINGLEN];
				char* p = tmp;
				int len = LoadString(hInstance, IDS_PNP1, p, TMP_STRINGLEN);
				p += len;
				*p++ = '\r';
				*p++ = '\n';
				len = LoadString(hInstance,
								 IDS_PNP2, p, TMP_STRINGLEN - (p - tmp));
				GetDriveLetter(pdbcv->dbcv_unitmask, szDrives);
				MessageBox(hWnd, tmp, szDrives, MB_OK | MB_ICONHAND);
				JRD_shutdown_all(false);
				SRVR_shutdown();
				PostMessage(hWnd, WM_DESTROY, 0, 0);
			}
			return TRUE;

		case DBT_DEVICEQUERYREMOVEFAILED:
			return TRUE;

		case DBT_DEVICEREMOVECOMPLETE:
			return TRUE;

		case DBT_DEVICETYPESPECIFIC:
			return TRUE;

		case DBT_CONFIGCHANGED:
			return TRUE;
		}
		return TRUE;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return FALSE;
}


void WINDOW_shutdown(ULONG hWnd)
{
/******************************************************************************
 *
 *  W I N D O W _ s h u t d o w n
 *
 ******************************************************************************
 *
 *  Input:  hWnd - Handle to the window
 *
 *  Return: none
 *
 *  Description: This is a callback function which is called at shutdown time.
 *               This function post the WM_DESTROY message in appl. queue.
 *****************************************************************************/
	PostMessage(reinterpret_cast < HWND > (hWnd), WM_DESTROY, 0, 0);
}


// Hmm, maybe some Pascal programmer doing the declaration?
static void GetDriveLetter(ULONG ulDriveMask, char pchBuf[DRV_STRINGLEN])
{
/******************************************************************************
 *
 *  G e t D r i v e L e t t e r
 *
 ******************************************************************************
 *
 *  Input:  ulDriveMask - Bit flag mask encoding the drive letters
 *          pchBuf - Buffer to be filled
 *
 *  Return: Buffer containing the drive letters
 *
 *  Description: This function checks for the drives in Drive mask and returns
 *               the buffer filled with the drive letters which are on.
 *****************************************************************************/
	char chDrive = 'A';
	char* p = pchBuf;

	while (ulDriveMask) {
		if (ulDriveMask & 1)
			*p++ = chDrive;
		chDrive++;
		ulDriveMask >>= 1;
	}
	*p = '\0';
}


BOOL CanEndServer(HWND hWnd, bool bSysExit)
{
/******************************************************************************
 *
 *  C a n E n d S e r v e r
 *
 ******************************************************************************
 *
 *  Input:  hWnd     - Handle to main application window.
*	    bSysExit - Flag indicating if the system is going down. UNUSED.
 *
 *  Return: FALSE if user does not want to end the server session
 *          TRUE if user confirmed on server session termination
 *
 *  Description: This function displays a message mox and queries the user if
 *               the server can be shutdown.
 *****************************************************************************/
	ULONG usNumAtt;
	ULONG usNumDbs;
	JRD_num_attachments(NULL, 0, 0, &usNumAtt, &usNumDbs);

	char szMsgString[MSG_STRINGLEN];
	sprintf(szMsgString, "%u ", usNumAtt);

	if (!usNumAtt)				/* IF 0 CONNECTIONS, JUST SHUTDOWN */
		return TRUE;

	LoadString(hInstance, IDS_QUIT, szMsgString + strlen(szMsgString),
			   MSG_STRINGLEN - strlen(szMsgString));
	return (MessageBox(hWnd, szMsgString, APP_LABEL,
					   MB_ICONQUESTION | MB_OKCANCEL) == IDOK);
}

