/*
 * PROJECT:     ReactOS Applications Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Functions to parse command-line flags and process them
 * COPYRIGHT:   Copyright 2017 Alexander Shaposhnikov (sanchaez@reactos.org)
 *              Copyright 2020 He Yang                (1160386205@qq.com)
 */
#include "rapps.h"

#include "unattended.h"

#include "winmain.h"

#include <setupapi.h>

#include <conutils.h>

BOOL MatchCmdOption(LPWSTR argvOption, LPCWSTR szOptToMacth)
{
    WCHAR FirstCharList[] = { L'-', L'/' };

    for (UINT i = 0; i < _countof(FirstCharList); i++)
    {
        if (argvOption[0] == FirstCharList[i])
        {
            if (StrCmpIW(argvOption + 1, szOptToMacth) == 0)
            {
                return TRUE;
            }
            else
            {
                return FALSE;
            }
        }
    }
    return FALSE;
}

BOOL HandleInstallCommand(LPWSTR szCommand, int argcLeft, LPWSTR * argvLeft)
{
    if (argcLeft == 0)
    {
        ConInitStdStreams(); // Initialize the Console Standard Streams
        ConResMsgPrintf(StdOut, NULL, IDS_CMD_NEED_PACKAGE_NAME, szCommand);
        return FALSE;
    }
    FreeConsole();

    ATL::CSimpleArray<ATL::CStringW> PkgNameList;

    for (int i = 0; i < argcLeft; i++)
    {
        PkgNameList.Add(argvLeft[i]);
    }

    CAvailableApps apps;
    apps.UpdateAppsDB();
    apps.Enum(ENUM_ALL_AVAILABLE, NULL, NULL);

    ATL::CSimpleArray<CAvailableApplicationInfo> arrAppInfo = apps.FindAppsByPkgNameList(PkgNameList);
    if (arrAppInfo.GetSize() > 0)
    {
        DownloadListOfApplications(arrAppInfo, TRUE);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

BOOL HandleSetupCommand(LPWSTR szCommand, int argcLeft, LPWSTR * argvLeft)
{
    if (argcLeft != 1)
    {
        ConInitStdStreams(); // Initialize the Console Standard Streams
        ConResMsgPrintf(StdOut, NULL, IDS_CMD_NEED_FILE_NAME, szCommand);
        return FALSE;
    }
    FreeConsole();

    ATL::CSimpleArray<ATL::CStringW> PkgNameList;
    HINF InfHandle = SetupOpenInfFileW(argvLeft[0], NULL, INF_STYLE_WIN4, NULL);
    if (InfHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    INFCONTEXT Context;
    if (SetupFindFirstLineW(InfHandle, L"RAPPS", L"Install", &Context))
    {
        WCHAR szPkgName[MAX_PATH];
        do
        {
            if (SetupGetStringFieldW(&Context, 1, szPkgName, _countof(szPkgName), NULL))
            {
                PkgNameList.Add(szPkgName);
            }
        } while (SetupFindNextLine(&Context, &Context));
    }
    SetupCloseInfFile(InfHandle);

    CAvailableApps apps;
    apps.UpdateAppsDB();
    apps.Enum(ENUM_ALL_AVAILABLE, NULL, NULL);

    ATL::CSimpleArray<CAvailableApplicationInfo> arrAppInfo = apps.FindAppsByPkgNameList(PkgNameList);
    if (arrAppInfo.GetSize() > 0)
    {
        DownloadListOfApplications(arrAppInfo, TRUE);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

BOOL CALLBACK CmdFindAppEnum(CAvailableApplicationInfo *Info, BOOL bInitialCheckState, PVOID param)
{
    LPCWSTR lpszSearch = (LPCWSTR)param;
    if (!SearchPatternMatch(Info->m_szName.GetString(), lpszSearch) &&
        !SearchPatternMatch(Info->m_szDesc.GetString(), lpszSearch))
    {
        return TRUE;
    }

    ConPrintf(StdOut, (LPWSTR)L"%s (%s)\n", (LPCWSTR)(Info->m_szName), (LPCWSTR)(Info->m_szPkgName));
    return TRUE;
}

BOOL HandleFindCommand(LPWSTR szCommand, int argcLeft, LPWSTR *argvLeft)
{
    if (argcLeft < 1)
    {
        ConResMsgPrintf(StdOut, NULL, IDS_CMD_NEED_PARAMS, szCommand);
        return FALSE;
    }

    CAvailableApps apps;
    apps.UpdateAppsDB();

    for (int i = 0; i < argcLeft; i++)
    {
        ConResMsgPrintf(StdOut, NULL, IDS_CMD_FIND_RESULT_FOR, argvLeft[i]);
        apps.Enum(ENUM_ALL_AVAILABLE, CmdFindAppEnum, argvLeft[i]);
        ConPrintf(StdOut, (LPWSTR)L"\n");
    }

    return TRUE;
}

BOOL HandleInfoCommand(LPWSTR szCommand, int argcLeft, LPWSTR *argvLeft)
{
    if (argcLeft < 1)
    {
        ConResMsgPrintf(StdOut, NULL, IDS_CMD_NEED_PARAMS, szCommand);
        return FALSE;
    }

    CAvailableApps apps;
    apps.UpdateAppsDB();
    apps.Enum(ENUM_ALL_AVAILABLE, NULL, NULL);

    for (int i = 0; i < argcLeft; i++)
    {
        CAvailableApplicationInfo *AppInfo = apps.FindAppByPkgName(argvLeft[i]);
        if (!AppInfo)
        {
            ConResMsgPrintf(StdOut, NULL, IDS_CMD_PACKAGE_NOT_FOUND, argvLeft[i]);
        }
        else
        {
            ConResMsgPrintf(StdOut, NULL, IDS_CMD_PACKAGE_INFO, argvLeft[i]);
            // TODO: code about extracting information from CAvailableApplicationInfo (in appview.cpp, class CAppRichEdit)
            // is in a mess. It should be refactored, and should not placed in class CAppRichEdit.
            // and the code here should reused that code after refactor.

            ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szName);

            if (AppInfo->m_szVersion)
            {
                ConResPrintf(StdOut, IDS_AINFO_VERSION);
                ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szVersion);
            }

            if (AppInfo->m_szLicense)
            {
                ConResPrintf(StdOut, IDS_AINFO_LICENSE);
                ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szLicense);
            }

            if (AppInfo->m_szSize)
            {
                ConResPrintf(StdOut, IDS_AINFO_SIZE);
                ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szSize);
            }

            if (AppInfo->m_szUrlSite)
            {
                ConResPrintf(StdOut, IDS_AINFO_URLSITE);
                ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szUrlSite);
            }

            if (AppInfo->m_szDesc)
            {
                ConResPrintf(StdOut, IDS_AINFO_DESCRIPTION);
                ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szDesc);
            }

            if (AppInfo->m_szUrlDownload)
            {
                ConResPrintf(StdOut, IDS_AINFO_URLDOWNLOAD);
                ConPuts(StdOut, (LPWSTR)(LPCWSTR)AppInfo->m_szUrlDownload);
            }

            ConPrintf(StdOut, (LPWSTR)L"\n");
        }
        ConPrintf(StdOut, (LPWSTR)L"\n");
    }
    return TRUE;
}

BOOL HandleHelpCommand(LPWSTR szCommand, int argcLeft, LPWSTR * argvLeft)
{
    if (argcLeft != 0)
    {
        return FALSE;
    }

    ConPrintf(StdOut, (LPWSTR)L"\n");
    ConResPuts(StdOut, IDS_APPTITLE);
    ConPrintf(StdOut, (LPWSTR)L"\n\n");

    ConResPuts(StdOut, IDS_CMD_USAGE);
    ConPrintf(StdOut, (LPWSTR)L"%ls\n", UsageString);
    return TRUE;
}

BOOL ParseCmdAndExecute(LPWSTR lpCmdLine, BOOL bIsFirstLaunch, int nCmdShow)
{
    INT argc;
    LPWSTR *argv = CommandLineToArgvW(lpCmdLine, &argc);

    if (!argv)
    {
        return FALSE;
    }

    if (argc == 1) // RAPPS is launched without options
    {
        // Close the console, and open MainWindow
        FreeConsole();


        // Check for if rapps MainWindow is already launched in another process
        HANDLE hMutex;

        hMutex = CreateMutexW(NULL, FALSE, szWindowClass);
        if ((!hMutex) || (GetLastError() == ERROR_ALREADY_EXISTS))
        {
            /* If already started, it is found its window */
            HWND hWindow = FindWindowW(szWindowClass, NULL);

            /* Activate window */
            ShowWindow(hWindow, SW_SHOWNORMAL);
            SetForegroundWindow(hWindow);
            return FALSE;
        }

        if (SettingsInfo.bUpdateAtStart || bIsFirstLaunch)
            CAvailableApps::ForceUpdateAppsDB();

        MainWindowLoop(nCmdShow);

        if (hMutex)
            CloseHandle(hMutex);

        return TRUE;
    }
    else if (MatchCmdOption(argv[1], CMD_KEY_INSTALL))
    {
        return HandleInstallCommand(argv[1], argc - 2, argv + 2);
    }
    else if (MatchCmdOption(argv[1], CMD_KEY_SETUP))
    {
        return HandleSetupCommand(argv[1], argc - 2, argv + 2);
    }
    

    ConInitStdStreams(); // Initialize the Console Standard Streams

    if (MatchCmdOption(argv[1], CMD_KEY_FIND))
    {
        return HandleFindCommand(argv[1], argc - 2, argv + 2);
    }
    else if (MatchCmdOption(argv[1], CMD_KEY_INFO))
    {
        return HandleInfoCommand(argv[1], argc - 2, argv + 2);
    }
    else if (MatchCmdOption(argv[1], CMD_KEY_HELP))
    {
        return HandleHelpCommand(argv[1], argc - 2, argv + 2);
    }
    else
    {
        // unrecognized/invalid options
        ConResPuts(StdOut, IDS_CMD_INVALID_OPTION);
        return FALSE;
    }
}
