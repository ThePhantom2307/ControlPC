#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

// Constants
#define TIME_STRING_SIZE 100 // Size for formatted time string
#define MESSAGE_BUFFER_SIZE 1024 // Buffer size for messages
#define BUFFER_SIZE 10000 // Buffer size for messages
#define WM_TRAYICON (WM_USER + 1) // Custom Windows message for tray icon

// Global Variables
time_t currentTime; // Current time
struct tm *localTime; // Current time structure
char timeString[TIME_STRING_SIZE]; // Buffer for formatted time string
int udpPORT = 9998; // UDP port for discovery
int tcpPORT = 9999; // TCP port for server

NOTIFYICONDATA nid;  // Tray icon structure
BOOL discoveryRunning = FALSE;  // UDP Discovery status
BOOL serverRunning = FALSE;     // TCP Server status

// Global window handles
HWND hMainWindow = NULL; // Main window that holds the log
HWND hLogEdit = NULL;    // Edit control for log messages

HANDLE serverThreadHandle = NULL;      // Server thread handle
HANDLE discoveryThreadHandle = NULL;     // Discovery thread handle
HANDLE stopServerEvent = NULL;           // Event to signal stopping the server
HANDLE stopDiscoveryEvent = NULL;        // Event to signal stopping discovery

// Function Declarations
void getDatetime();
void sendUpdate(const char *format, ...);
void toggleLogVisibility();
void createTrayIcon(HWND hwnd);
void removeTrayIcon(HWND hwnd);
void showTrayMenu(HWND hwnd);
void stopServer();
void startServer();
void stopDiscovery();
void startDiscovery();
DWORD WINAPI udpDiscovery(LPVOID lpParam);
DWORD WINAPI server(LPVOID lpParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
HWND createHiddenWindow(HINSTANCE hInstance);

// Get current date and time
void getDatetime() {
    time(&currentTime);
    localTime = localtime(&currentTime);
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", localTime);
}

// Log a message to the log edit control
void sendUpdate(const char *format, ...) {
    char message[BUFFER_SIZE];
    getDatetime();

    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char fullMessage[BUFFER_SIZE];
    snprintf(fullMessage, sizeof(fullMessage), "%s: %s\n", timeString, message);

    // Append the message to the edit control if available
    if (hLogEdit) {
        int len = GetWindowTextLength(hLogEdit);
        SendMessage(hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)fullMessage);
    }
}

// Toggle the visibility of the log window (main window)
void toggleLogVisibility() {
    if (hMainWindow) {
        if (IsWindowVisible(hMainWindow))
            ShowWindow(hMainWindow, SW_HIDE);
        else
            ShowWindow(hMainWindow, SW_SHOW);
    }
}

// Create the tray icon
void createTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;

    HICON hIcon = (HICON)LoadImage(NULL, "controlpc_logo.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (hIcon) {
        nid.hIcon = hIcon;
    } else {
        MessageBox(NULL, "Failed to load icon.", "Error", MB_OK | MB_ICONERROR);
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    strcpy(nid.szTip, "ControlPC Running");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// Remove the tray icon
void removeTrayIcon(HWND hwnd) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// Show tray menu with options to start/stop server, toggle discovery, toggle log window, and exit.
void showTrayMenu(HWND hwnd) {
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    HMENU hMenu = CreatePopupMenu();

    AppendMenu(hMenu, MF_STRING, 1004, serverRunning ? "Stop Server" : "Start Server");
    AppendMenu(hMenu, MF_STRING, 1003, discoveryRunning ? "Disable Discovery" : "Enable Discovery");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, 1002, IsWindowVisible(hMainWindow) ? "Hide Log" : "Show Log");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, 1001, "Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursorPos.x, cursorPos.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// Stop the server (non-blocking)
void stopServer() {
    if (serverRunning) {
        sendUpdate("[INFO] Stopping the server.");
        SetEvent(stopServerEvent);
        serverRunning = FALSE;
        sendUpdate("[INFO] Server stopped.");
    }
}

// Start the server
void startServer() {
    if (!serverRunning) {
        sendUpdate("[INFO] Starting the server.");
        ResetEvent(stopServerEvent);
        serverThreadHandle = CreateThread(NULL, 0, server, NULL, 0, NULL);
        serverRunning = TRUE;
        sendUpdate("[INFO] Server started.");
    }
}

// Stop UDP discovery (non-blocking)
void stopDiscovery() {
    if (discoveryRunning) {
        sendUpdate("[INFO] Stopping UDP discovery.");
        SetEvent(stopDiscoveryEvent);
        discoveryRunning = FALSE;
        sendUpdate("[INFO] UDP discovery stopped.");
    }
}

// Start UDP discovery
void startDiscovery() {
    if (!discoveryRunning) {
        sendUpdate("[INFO] Starting UDP discovery.");
        ResetEvent(stopDiscoveryEvent);
        discoveryThreadHandle = CreateThread(NULL, 0, udpDiscovery, NULL, 0, NULL);
        discoveryRunning = TRUE;
        sendUpdate("[INFO] UDP discovery started.");
        sendUpdate("[WARNING] If you are connected on a public modem/router, disable UDP discovery after the client connection.");
    }
}

// Handle incoming commands and perform the appropriate actions
void handleMessage(SOCKET clientSocket, const char *clientIP, const char *message) {
    if (strncmp(message, "START|", 6) == 0) {
        const char *command = message + 6;
        sendUpdate("[INFO] Client %s requested to start: %s", clientIP, command);
        char fullCommand[256];
        snprintf(fullCommand, sizeof(fullCommand), "start %s", command);
        system(fullCommand);
    } else if (strcmp(message, "SHUTDOWN") == 0) {
        sendUpdate("[INFO] Shutting down the computer.");
        system("shutdown /s /t 5");
    } else if (strcmp(message, "RESTART") == 0) {
        sendUpdate("[INFO] Restarting the computer.");
        system("shutdown /r /t 5");
    } else if (strcmp(message, "SLEEP") == 0) {
        sendUpdate("[INFO] Putting the computer to sleep in 5 seconds.");
        Sleep(5000);
        system("rundll32.exe powrprof.dll,SetSuspendState 0,1,0");
    } else if (strcmp(message, "LOCK") == 0) {
        sendUpdate("[INFO] Locking the computer.");
        system("rundll32.exe user32.dll,LockWorkStation");
    } else {
        sendUpdate("[INFO] Invalid command received: %s", message);
    }
}

// UDP discovery thread function
DWORD WINAPI udpDiscovery(LPVOID lpParam) {
    WSADATA wsaData;
    SOCKET discoverySocket;
    struct sockaddr_in serverAddress, clientAddress;
    char buffer[BUFFER_SIZE];
    char replyMessage[BUFFER_SIZE];
    char clientIP[INET_ADDRSTRLEN];
    char computerName[BUFFER_SIZE];
    int clientAddrLen, recvLen;
    int computerNameLen = sizeof(computerName);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        sendUpdate("[ERROR] Winsock initialization failed.");
        return 1;
    }

    discoverySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (discoverySocket == INVALID_SOCKET) {
        sendUpdate("[ERROR] UDP Socket creation failed.");
        WSACleanup();
        return 1;
    }

    int optval = 1;
    setsockopt(discoverySocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(udpPORT);

    if (bind(discoverySocket, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        sendUpdate("[ERROR] UDP Socket binding failed.");
        closesocket(discoverySocket);
        WSACleanup();
        return 1;
    }

    if (gethostname(computerName, computerNameLen) == SOCKET_ERROR) {
        sendUpdate("[ERROR] Could not get computer's name.");
        closesocket(discoverySocket);
        WSACleanup();
        return 1;
    }

    while (WaitForSingleObject(stopDiscoveryEvent, 0) != WAIT_OBJECT_0) {
        clientAddrLen = sizeof(clientAddress);
        memset(buffer, 0, BUFFER_SIZE);

        recvLen = recvfrom(discoverySocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &clientAddress, &clientAddrLen);
        if (recvLen == SOCKET_ERROR) {
            sendUpdate("[ERROR] Message receiving failed.");
            break;
        }

        buffer[recvLen] = '\0';
        strcpy(clientIP, inet_ntoa(clientAddress.sin_addr));
        snprintf(replyMessage, sizeof(replyMessage), "DISCOVER_CONTROLPC_REPLY|%s", computerName);
        sendto(discoverySocket, replyMessage, strlen(replyMessage), 0, (struct sockaddr *) &clientAddress, clientAddrLen);
    }
    
    closesocket(discoverySocket);
    WSACleanup();
    return 0;
}

// TCP server thread function
DWORD WINAPI server(LPVOID lpParam) {
    WSADATA wsaData;
    SOCKET serverSocket, clientSocket;
    struct sockaddr_in serverAddress, clientAddress;
    int clientAddrLen, recvLen;
    char buffer[MESSAGE_BUFFER_SIZE];
    char clientIP[INET_ADDRSTRLEN];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        sendUpdate("[ERROR] Winsock initialization failed for TCP server.");
        return 1;
    }

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        sendUpdate("[ERROR] TCP Socket creation failed.");
        WSACleanup();
        return 1;
    }

    int optval = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(tcpPORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        sendUpdate("[ERROR] TCP Socket binding failed.");
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        sendUpdate("[ERROR] Listening for TCP connections failed.");
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    while (WaitForSingleObject(stopServerEvent, 0) != WAIT_OBJECT_0) {
        clientAddrLen = sizeof(clientAddress);
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            sendUpdate("[ERROR] Accepting client connection failed.");
            break;
        }

        strcpy(clientIP, inet_ntoa(clientAddress.sin_addr));
        sendUpdate("[INFO] Client connected: %s", clientIP);

        while (WaitForSingleObject(stopServerEvent, 0) != WAIT_OBJECT_0) {
            memset(buffer, 0, MESSAGE_BUFFER_SIZE);
            recvLen = recv(clientSocket, buffer, MESSAGE_BUFFER_SIZE, 0);
            if (recvLen == SOCKET_ERROR || recvLen == 0) {
                sendUpdate("[INFO] Client %s disconnected.", clientIP);
                closesocket(clientSocket);
                break;
            }

            buffer[recvLen] = '\0';
            sendUpdate("[Message] Client %s: %s", clientIP, buffer);
            handleMessage(clientSocket, clientIP, buffer);
        }
    }

    sendUpdate("[INFO] TCP Server thread stopping...");
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}

// Tray icon and window message handler
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                showTrayMenu(hwnd);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case 1001:  // Exit
                    removeTrayIcon(hwnd);
                    stopServer();
                    stopDiscovery();
                    PostQuitMessage(0);
                    break;
                case 1002:  // Toggle Log visibility
                    toggleLogVisibility();
                    break;
                case 1003:  // Enable/Disable Discovery
                    discoveryRunning ? stopDiscovery() : startDiscovery();
                    break;
                case 1004:  // Start/Stop Server
                    serverRunning ? stopServer() : startServer();
                    break;
            }
            return 0;
        case WM_DESTROY:
            removeTrayIcon(hwnd);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Create the main window (hidden by default) with an edit control for logs
HWND createHiddenWindow(HINSTANCE hInstance) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ServerTrayClass";

    RegisterClass(&wc);
    HWND hwnd = CreateWindow("ServerTrayClass", "ControlPC Log", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
                             NULL, NULL, hInstance, NULL);
    
    hLogEdit = CreateWindow("EDIT", NULL, 
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            10, 10, 460, 240, hwnd, NULL, hInstance, NULL);
    return hwnd;
}

// Icon creation thread function
DWORD WINAPI createIconThread(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;
    createTrayIcon(hwnd);
    return 0;
}

// Main program
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    stopServerEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    stopDiscoveryEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    hMainWindow = createHiddenWindow(hInstance);
    ShowWindow(hMainWindow, SW_HIDE);

    CreateThread(NULL, 0, createIconThread, hMainWindow, 0, NULL);

    startServer();
    startDiscovery();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CloseHandle(stopServerEvent);
    CloseHandle(stopDiscoveryEvent);

    return 0;
}
