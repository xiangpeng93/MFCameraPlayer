// Note: Most of the interesting code is in Player.cpp

#include "Common.h"
#include "Player.h"
#include "resource.h"
#include <new>
#include <iostream>


const wchar_t szTitle[] = L"BasicPlayback";
const wchar_t szWindowClass[] = L"MFBASICPLAYBACK";

BOOL        g_bRepaintClient = TRUE;            // Repaint the application client area?
CPlayer     *g_pPlayer = NULL;                  // Global player object.

// Note: After WM_CREATE is processed, g_pPlayer remains valid until the
// window is destroyed.

BOOL                CreateApplicationWindow(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

// Message handlers
LRESULT             OnCreateWindow(HWND hwnd);
void                OnPaint(HWND hwnd);
void                OnKeyPress(WPARAM key);
void                OnOpenFile(HWND parent);
void				OnOpenCamera(HWND parent);
void				OnGetCurrentPic(std::string &str);
void				exeCalc(std::string path);
void				exeCalc(std::wstring path);
char g_currentDir[MAX_PATH] = { 0 };
wchar_t g_wcurrentDir[MAX_PATH] = { 0 };
int initSocket()
{
#ifdef WIN32
	WSADATA wsaData;

	// WSAStartup为程序调用WinSock进行了初始化。  
	// 第一个参数指定了程序允许使用的WinSock规范的最高版本。  
	int wsaret = WSAStartup(0x101, &wsaData);

	// 如果成功，WSAStartup返回零。  
	// 如果失败，我们就退出。  
	if (wsaret != 0)
	{
		return WSAGetLastError();
	}
#else
	return 0;
#endif
	return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    MSG msg;

    ZeroMemory(&msg, sizeof(msg));

    // Perform application initialization.
    if (!CreateApplicationWindow(hInstance, nCmdShow))
    {
        return FALSE;
    }

    // main message pump for the application
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

	
    return 0;
}

BOOL CreateApplicationWindow(HINSTANCE hInst, int nCmdShow)
{
    HWND hwnd;
    WNDCLASSEX wcex = { 0 };

    // initialize the structure describing the window
    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.hCursor        = LoadCursor(hInst, MAKEINTRESOURCE(IDC_POINTER));
    wcex.hInstance      = hInst;
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_FILEMENU);
    wcex.lpszClassName  = szWindowClass;

    // register the class for the window
    if (RegisterClassEx(&wcex) == 0)
    {
        return FALSE;
    }

    // Create the application window.
	hwnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL);
    if (hwnd == 0)
    {
        return FALSE;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

	GetCurrentDirectoryA(MAX_PATH, g_currentDir);
	GetCurrentDirectory(MAX_PATH, g_wcurrentDir);
    return TRUE;
}


//
// Main application message handling procedure - called by Windows to pass window-related
// messages to the application.
//
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HRESULT hr = S_OK;

    switch (message)
    {
    case WM_CREATE:
        return OnCreateWindow(hwnd);

    case WM_COMMAND:
        if(LOWORD(wParam) == ID_FILE_EXIT)
        {
            DestroyWindow(hwnd);
        }
        else if(LOWORD(wParam) == ID_FILE_OPENFILE)
        {
            OnOpenFile(hwnd);
        }
		else if (LOWORD(wParam) == ID_FILE_OPENCAMERA)
		{
			OnOpenCamera(hwnd);
		}
		else if (LOWORD(wParam) == ID_MANUAL_GETCURRENTPIC)
		{
			std::string cmd;
			OnGetCurrentPic(cmd);
			exeCalc(cmd);
		}
		
        else if(LOWORD(wParam) == ID_CONTROL_PLAY)
        {
            if(g_pPlayer != NULL)
            {
                g_pPlayer->Play();
            }
        }
        else if(LOWORD(wParam) == ID_CONTROL_PAUSE)
        {
            if(g_pPlayer != NULL)
            {
                g_pPlayer->Pause();
            }
        }
        else
        {   
            return DefWindowProc(hwnd, message, wParam, lParam);
        }
        break;

    case WM_PAINT:
        OnPaint(hwnd);
        break;

    case WM_ERASEBKGND:
        // Suppress window erasing, to reduce flickering while the video is playing.
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_CHAR:
        OnKeyPress(wParam);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}


//  Handles the WM_CREATE message.
//
//  hwnd: Handle to the video clipping window. (For this sample, the
//        video window is just the main application window.)

LRESULT OnCreateWindow(HWND hwnd)
{   
    HRESULT hr = S_OK;

    // Initialize the player object.
    g_pPlayer = new (std::nothrow) CPlayer(hwnd, &hr);

    // if player creation failed, 
    if(FAILED(hr))
    {
        delete g_pPlayer;
        g_pPlayer = NULL;
    }

    return 0;
}




//
//  Description: Handles WM_PAINT messages.  This has special behavior in order to handle cases where the
// video is paused and resized.
//
void OnPaint(HWND hwnd)
{
	long Style = ::GetWindowLong(hwnd, GWL_STYLE) | WS_POPUP;
	::SetWindowLong(hwnd, GWL_STYLE, Style);

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    if (g_pPlayer && g_pPlayer->HasVideo())
    {
        // We have a player with an active topology and a video renderer that can paint the
        // window surface - ask the videor renderer (through the player) to redraw the surface.
        g_pPlayer->Repaint();
    }
    else
    {
        // The player topology hasn't been activated, which means there is no video renderer that 
        // repaint the surface.  This means we must do it ourselves.
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH) COLOR_WINDOW);
    }
    EndPaint(hwnd, &ps);
}

void OnKeyPress(WPARAM key)
{
    if (key == VK_SPACE)
    {
        // Space key toggles between running and paused
        if (g_pPlayer->GetState() == PlayerState_Started)
        {
            g_pPlayer->Pause();
        }
        else if (g_pPlayer->GetState() == PlayerState_Paused)
        {
            g_pPlayer->Play();
        }
    }
}

void exeCalc(std::string path)
{
	std::string commond = g_currentDir;
	commond += "\\calc.exe  \"";
	commond += path;
	commond += "\"";

	system(commond.c_str());
}

void exeCalc(std::wstring path)
{
	std::wstring commond = g_wcurrentDir;
	commond += L"\\calc.exe \"";
	commond += path;
	commond += L"\"";
	_wsystem(commond.c_str());
}

void OnOpenCamera(HWND parent)
{
	if (g_pPlayer != NULL)
	{
		g_pPlayer->OpenURL(NULL);
	}
}

void OnGetCurrentPic(std::string &str)
{
	int iResult = 0;
	SOCKET ConnectSocket;
	ConnectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ConnectSocket == INVALID_SOCKET) {
		wprintf(L"socket function failed with error: %ld\n", WSAGetLastError());
		return;
	}

	//----------------------  
	// The sockaddr_in structure specifies the address family,  
	// IP address, and port of the server to be connected to.  
	sockaddr_in clientService;
	clientService.sin_family = AF_INET;
	clientService.sin_addr.s_addr = inet_addr("127.0.0.1");
	clientService.sin_port = htons(8999);

	//----------------------  
	// Connect to server.  
	iResult = connect(ConnectSocket, (SOCKADDR *)& clientService, sizeof (clientService));
	if (iResult == SOCKET_ERROR) {
		wprintf(L"connect function failed with error: %ld\n", WSAGetLastError());
		iResult = closesocket(ConnectSocket);
		if (iResult == SOCKET_ERROR)
			wprintf(L"closesocket function failed with error: %ld\n", WSAGetLastError());
		return;
	}
	Sleep(100);

	iResult = send(ConnectSocket, "capture", strlen("capture"), 0);

	Sleep(100);
	char temp[512] = { 0 };
	int ret = recv(ConnectSocket, temp, sizeof(temp), 0);
	str = temp;

	wprintf(L"Connected to server.\n");

	iResult = closesocket(ConnectSocket);
	if (iResult == SOCKET_ERROR) {
		wprintf(L"closesocket function failed with error: %ld\n", WSAGetLastError());
		return;
	}
	return;
}

void OnOpenFile(HWND parent)
{
    OPENFILENAME ofn;       // common dialog box structure
    WCHAR  szFile[260];       // buffer for file name
//    HWND hwnd;              // owner window

    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFile = szFile;
    // Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
    // use the contents of szFile to initialize itself.
    ofn.lpstrFile[0] = '\0\0';
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"BMP\0*.bmp\0JPG\0*.JPG\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    // Display the Open dialog box. 

    if (GetOpenFileName(&ofn)==TRUE) 
    {
		exeCalc(ofn.lpstrFile);
    }
}
