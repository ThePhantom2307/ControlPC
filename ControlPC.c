#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9999
#define BUFFER_SIZE 1024
#define WM_SYSTRAY (WM_USER + 1)

SOCKET serverSocket = INVALID_SOCKET;
SOCKET clientSocket = INVALID_SOCKET;
volatile BOOL server_running = FALSE;
NOTIFYICONDATA nid;
HWND hwnd;
HINSTANCE hInstance;
HANDLE serverThread = NULL;
BOOL consoleVisible = FALSE;
HWND consoleWnd = NULL;

void cleanup_and_exit();
void send_response(const char* response_message);
void shutdown_system();
void restart_system();
void sleep_system();
void lock_system();
void open_spotify();
void open_youtube();
void message_handler();
void start_server_thread();
void stop_server();
void setup_tray_icon();
void update_tray_tooltip();
void toggle_console();
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void server_thread_func(void* arg) {
    struct sockaddr_in serverAddr, clientAddr;
    int addrlen = sizeof(clientAddr);

    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        MessageBox(hwnd, "Socket creation failed", "Error", MB_OK | MB_ICONERROR);
        _endthread();
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        MessageBox(hwnd, "Bind failed", "Error", MB_OK | MB_ICONERROR);
        closesocket(serverSocket);
        _endthread();
    }

    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        MessageBox(hwnd, "Listen failed", "Error", MB_OK | MB_ICONERROR);
        closesocket(serverSocket);
        _endthread();
    }

    server_running = TRUE;
    update_tray_tooltip();

    while (server_running) {
        if ((clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrlen)) == INVALID_SOCKET) {
            if (WSAGetLastError() == WSAEINTR) {
                continue;
            }
            MessageBox(hwnd, "Accept failed", "Error", MB_OK | MB_ICONERROR);
            continue;
        }
        message_handler();
        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    server_running = FALSE;
    _endthread();
}

void start_server_thread() {
    if (!server_running) {
        serverThread = (HANDLE)_beginthread(server_thread_func, 0, NULL);
    }
}

void stop_server() {
    server_running = FALSE;

    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    if (serverThread != NULL) {
        WaitForSingleObject(serverThread, INFINITE);
        CloseHandle(serverThread);
        serverThread = NULL;
    }

    update_tray_tooltip();
}

void cleanup_and_exit() {
    if (server_running) {
        stop_server();
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);

    WSACleanup();
    PostQuitMessage(0);
}

void send_response(const char* response_message) {
    if (clientSocket != INVALID_SOCKET) {
        send(clientSocket, response_message, strlen(response_message), 0);
    }
}

void shutdown_system() {
    if (system("shutdown /s /t 1") != 0) {
        send_response("SHUTDOWN FAILED");
    } else {
        send_response("EXECUTED");
    }
}

void restart_system() {
    if (system("shutdown /r /t 1") != 0) {
        send_response("RESTART FAILED");
    } else {
        send_response("EXECUTED");
    }
}

void sleep_system() {
    if (system("rundll32.exe powrprof.dll,SetSuspendState 0,1,0") != 0) {
        send_response("SLEEP FAILED");
    } else {
        send_response("EXECUTED");
    }
}

void lock_system() {
    if (system("rundll32.exe user32.dll,LockWorkStation") != 0) {
        send_response("LOCK FAILED");
    } else {
        send_response("EXECUTED");
    }
}

void open_application(char* app) {
    char command[256];
    sprintf(command, "start %s", app);
    if (system(command) != 0) {
        send_response("START FAILED");
    } else {
        send_response("EXECUTED");
    }
}

void message_handler() {
    char buffer[BUFFER_SIZE] = {0};

    int recvSize = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
    if (recvSize > 0) {
        buffer[recvSize] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        char* command = strtok(buffer, "|");
        printf("Message from client: %s\n", buffer);

        if (strcmp(command, "SHUTDOWN") == 0) {
            shutdown_system();
        } else if (strcmp(command, "RESTART") == 0) {
            restart_system();
        } else if (strcmp(command, "SLEEP") == 0) {
            sleep_system();
        } else if (strcmp(command, "LOCK") == 0) {
            lock_system();
        } else if (strcmp(command, "START") == 0) {
            command = strtok(NULL, "|");
            open_application(command);
        } else {
            send_response("INVALID COMMAND");
        }
    } else if (recvSize == 0) {
        printf("Client disconnected\n");
    } else {
        printf("Receive failed: %d\n", WSAGetLastError());
    }

    memset(buffer, 0, BUFFER_SIZE);
}

void setup_tray_icon() {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_SYSTRAY;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    update_tray_tooltip();
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void update_tray_tooltip() {
    if (server_running) {
        lstrcpy(nid.szTip, TEXT("ControlPC Running"));
    } else {
        lstrcpy(nid.szTip, TEXT("ControlPC Stopped"));
    }
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void toggle_console() {
    if (consoleWnd) {
        if (consoleVisible) {
            ShowWindow(consoleWnd, SW_HIDE);
            consoleVisible = FALSE;
        } else {
            ShowWindow(consoleWnd, SW_SHOW);
            consoleVisible = TRUE;
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_SYSTRAY) {
        if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();

            if (server_running) {
                AppendMenu(hMenu, MF_STRING, 1, TEXT("Stop Server"));
            } else {
                AppendMenu(hMenu, MF_STRING, 2, TEXT("Start Server"));
            }

            if (consoleVisible) {
                AppendMenu(hMenu, MF_STRING, 4, TEXT("Hide Console"));
            } else {
                AppendMenu(hMenu, MF_STRING, 5, TEXT("Show Console"));
            }

            AppendMenu(hMenu, MF_STRING, 3, TEXT("Exit"));

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
    } else if (uMsg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {
            stop_server();
        } else if (LOWORD(wParam) == 2) {
            start_server_thread();
        } else if (LOWORD(wParam) == 3) {
            cleanup_and_exit();
        } else if (LOWORD(wParam) == 4 || LOWORD(wParam) == 5) {
            toggle_console();
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstanceParam, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    consoleWnd = GetConsoleWindow();
    ShowWindow(consoleWnd, SW_HIDE);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstanceParam;
    wc.lpszClassName = "ServerAppClass";

    RegisterClass(&wc);

    hwnd = CreateWindow("ServerAppClass", "Server App", 0, 0, 0, 0, 0, NULL, NULL, hInstanceParam, NULL);
    hInstance = hInstanceParam;

    WSADATA wsaData;
    MSG msg;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBox(hwnd, "WSAStartup failed", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    setup_tray_icon();
    start_server_thread();

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    cleanup_and_exit();
    return 0;
}
