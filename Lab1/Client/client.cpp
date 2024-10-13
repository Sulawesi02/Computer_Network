#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include<string>
#include <thread>
#include <windows.h> // 用于控制台光标操作

#pragma comment(lib, "ws2_32.lib") // socket库

#define PORT 3410  // 监听端口
#define BUF_SIZE 1024  // 缓冲区大小

using namespace std;

string userInput; // 保存当前用户输入的内容

// 处理服务器通信
void handleServer(SOCKET serverSocket, int clientId) {
    char recvBuff[BUF_SIZE];

    while (true) {
        ZeroMemory(recvBuff, BUF_SIZE);
        int recvBytes = recv(serverSocket, recvBuff, BUF_SIZE, 0);
        if (recvBytes <= 0) {
            cout << "与服务器断开连接" << endl;
            break;
        }

        // 保存当前光标位置
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        COORD cursorPos = csbi.dwCursorPosition;  // 获取当前光标位置

        // 读取提示词到光标之间的内容并存储到 userInput 中
        string prompt = "用户[" + to_string(clientId) + "]: ";// 提示词
        COORD promptStartPos = cursorPos;// 用户输入的起始位置(提示词末尾)
        promptStartPos.X = prompt.length(); 
        int remainingChars = cursorPos.X - promptStartPos.X;// 用户输入的长度（从提示词末尾到光标之间的字符数）
        char* inputBuffer = new char[remainingChars + 1]; // 存储用户输入的内容
        DWORD charsRead;// 用于存储实际读取的字符数
        ReadConsoleOutputCharacterA(GetStdHandle(STD_OUTPUT_HANDLE), inputBuffer, remainingChars, promptStartPos, &charsRead);// 读取控制台上的字符并存储在 inputBuffer 中
        inputBuffer[remainingChars] = '\0';  // 确保以 '\0' 终止字符串
        userInput = inputBuffer; // 将当前光标后的内容存入 userInput
        delete[] inputBuffer; // 释放内存

        // 清除当前行
        COORD startPos = cursorPos;
        startPos.X = 0;
        DWORD charsWritten;
        FillConsoleOutputCharacter(GetStdHandle(STD_OUTPUT_HANDLE), ' ', cursorPos.X, startPos, &charsWritten);
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), startPos); // 恢复光标位置

        // 打印接收到的消息
        string message = recvBuff;
        cout << message << endl;

        // 重新绘制用户输入
        cout << "用户[" << clientId << "]: " << userInput;
        cout.flush();
    }
}

int main() {
    WSAData wsd; // 初始化WinSock库
    SOCKET socketClient;
    SOCKADDR_IN serverAddr;
    char sendBuff[BUF_SIZE];

    // 初始化Socket DLL
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        cout << "初始化Socket DLL失败！" << endl;
        return 1;
    }
    cout << "初始化Socket DLL成功！" << endl;
    cout << "===============================================" << endl;

    // 创建客户端socket
    socketClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 采用IPv4地址族、流式套接字以及TCP协议 
    if (socketClient == INVALID_SOCKET) {
        cout << "创建客户端socket失败！" << endl;
        WSACleanup(); // 释放socket库资源
        return 1;
    }
    cout << "创建客户端socket成功！" << endl;
    cout << "===============================================" << endl;

    // 向服务器的socket发起连接请求
    serverAddr.sin_family = AF_INET; // IPv4地址族
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr.s_addr);
    serverAddr.sin_port = htons(PORT); // 服务器的监听端口号
    int addrLen = sizeof(serverAddr);
    if (connect(socketClient, (LPSOCKADDR)&serverAddr, addrLen) == SOCKET_ERROR) {
        cout << "连接服务器失败！" << endl;
        WSACleanup(); // 释放socket库资源
        return -1;
    }
    cout << "连接服务器成功！" << endl;
    cout << "===============================================" << endl;

    // 接收并保存clientId
    int clientId;
    recv(socketClient, (char*)&clientId, sizeof(clientId), 0);

    // 创建线程来接收服务器消息
    thread recvThread(handleServer, socketClient, clientId);
    recvThread.detach();

    // 处理用户输入
    while (true) {
        ZeroMemory(sendBuff, BUF_SIZE);
        cout << "用户[" << clientId << "]: ";
        cout.flush(); // 确保输出立即显示
        cin.getline(sendBuff, BUF_SIZE);

        if (send(socketClient, sendBuff, strlen(sendBuff), 0) == SOCKET_ERROR) {
            cout << "发送消息失败！" << endl;
            closesocket(socketClient); // 关闭客户端
            WSACleanup(); // 释放socket库资源
            return -1;
        }

        // 发送退出命令到服务器
        if (string(sendBuff) == "exit" || string(sendBuff) == "quit") {
            break;
        }
    }

    // 关闭客户端
    closesocket(socketClient);
    WSACleanup();
    return 0;
}
