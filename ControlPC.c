#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>  // For _beginthread and _endthread
#include <openssl/aes.h>
#include <openssl/rand.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9999
#define BUFFER_SIZE 1024
#define WM_SYSTRAY (WM_USER + 1)

// Global variables
SOCKET serverSocket = INVALID_SOCKET;
SOCKET clientSocket = INVALID_SOCKET;
volatile BOOL server_running = FALSE;
NOTIFYICONDATA nid;
HWND hwnd;
HINSTANCE hInstance;
HANDLE serverThread = NULL;  // Server thread handle
BOOL consoleVisible = FALSE;  // Console visibility state (hidden by default)
HWND consoleWnd = NULL;

// Function prototypes
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

// Server function running in a separate thread
void server_thread_func(void* arg) {
    struct sockaddr_in serverAddr, clientAddr;
    int addrlen = sizeof(clientAddr);

    // Create a socket
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        MessageBox(hwnd, "Socket creation failed", "Error", MB_OK | MB_ICONERROR);
        _endthread();
    }

    // Prepare the sockaddr_in structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // Bind the socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        MessageBox(hwnd, "Bind failed", "Error", MB_OK | MB_ICONERROR);
        closesocket(serverSocket);
        _endthread();
    }

    // Listen for incoming connections
    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        MessageBox(hwnd, "Listen failed", "Error", MB_OK | MB_ICONERROR);
        closesocket(serverSocket);
        _endthread();
    }

    server_running = TRUE;  // Mark the server as running
    update_tray_tooltip();  // Update the tray tooltip

    // Accept connections and handle them
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
    _endthread();  // End the thread when the server loop is done
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
        WaitForSingleObject(serverThread, INFINITE);  // Wait for the thread to finish
        CloseHandle(serverThread);
        serverThread = NULL;
    }

    update_tray_tooltip();
}

void cleanup_and_exit() {
    if (server_running) {
        stop_server();  // Ensure the server is stopped before exit
    }

    // Remove the system tray icon
    Shell_NotifyIcon(NIM_DELETE, &nid);

    WSACleanup();
    PostQuitMessage(0);  // Exit the application
}

void send_response(const char* response_message) {
    if (clientSocket != INVALID_SOCKET) {
        send(clientSocket, response_message, strlen(response_message), 0);
    }
}

void shutdown_system() {
    if (system("shutdown /s /t 1") != 0) {
        send_response("SHUTDOWN FAILED");
    }
}

void restart_system() {
    if (system("shutdown /r /t 1") != 0) {
        send_response("RESTART FAILED");
    }
}

void sleep_system() {
    if (system("rundll32.exe powrprof.dll,SetSuspendState 0,1,0") != 0) {
        send_response("SLEEP FAILED");
    }
}

void lock_system() {
    if (system("rundll32.exe user32.dll,LockWorkStation") != 0) {
        send_response("LOCK FAILED");
    }
}

void open_spotify() {
    if (system("start spotify") != 0) {
        send_response("SPOTIFY FAILED");
    }
}

void open_youtube() {
    if (system("start https://www.youtube.com") != 0) {
        send_response("YOUTUBE FAILED");
    }
}

void open_instagram() {
    if (system("start https://www.instagram.com") != 0) {
        send_response("INSTAGRAM FAILED");
    }
}

void message_handler() {
    char buffer[BUFFER_SIZE] = {0};

    int recvSize = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
    if (recvSize > 0) {
        buffer[recvSize] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        printf("Message from client: %s\n", buffer);

        if (strcmp(buffer, "SHUTDOWN") == 0) {
            shutdown_system();
        } else if (strcmp(buffer, "RESTART") == 0) {
            restart_system();
        } else if (strcmp(buffer, "SLEEP") == 0) {
            sleep_system();
        } else if (strcmp(buffer, "LOCK") == 0) {
            lock_system();
        } else if (strcmp(buffer, "OPEN SPOTIFY") == 0) {
            open_spotify();
        } else if (strcmp(buffer, "OPEN YOUTUBE") == 0) {
            open_youtube();
        } else if (strcmp(buffer, "OPEN INSTAGRAM") == 0){
            open_instagram();
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
    update_tray_tooltip();  // Set the initial tooltip
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void update_tray_tooltip() {
    if (server_running) {
        lstrcpy(nid.szTip, TEXT("ControlPC Running"));
    } else {
        lstrcpy(nid.szTip, TEXT("ControlPC Stopped"));
    }
    Shell_NotifyIcon(NIM_MODIFY, &nid);  // Update the tray icon tooltip
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
            stop_server();  // Stop the server when "Stop Server" is clicked
        } else if (LOWORD(wParam) == 2) {
            start_server_thread();  // Start the server when "Start Server" is clicked
        } else if (LOWORD(wParam) == 3) {
            cleanup_and_exit();  // Exit the application
        } else if (LOWORD(wParam) == 4 || LOWORD(wParam) == 5) {
            toggle_console();  // Toggle console visibility
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
    start_server_thread();  // Start the server automatically

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    cleanup_and_exit();
    return 0;
}
