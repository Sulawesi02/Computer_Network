#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <windows.h> // 用于控制台光标操作

#pragma comment(lib, "ws2_32.lib") // socket库

#define PORT 3410  // 监听端口
#define BUF_SIZE 1024  // 缓冲区大小

using namespace std;

int context_start_line = 6;// 聊天内容从第七行开始

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

        // 清除当前行
        COORD coord;
        coord.X = 0; // 光标位置的X坐标
        coord.Y = context_start_line; // 光标位置的Y坐标
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);

        // 输出接收到的消息
        cout << recvBuff << endl;

        context_start_line++;

        // 重新绘制用户提示
        cout << "用户[" << clientId << "]: ";
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
    char clientIdBuff[BUF_SIZE];
    ZeroMemory(clientIdBuff, BUF_SIZE);
    recv(socketClient, clientIdBuff, BUF_SIZE, 0);
    int clientId = atoi(strchr(clientIdBuff, ':') + 1); // 解析clientId

    // 创建线程来接收服务器消息
    thread recvThread(handleServer, socketClient, clientId);
    recvThread.detach();

    // 处理用户输入
    while (true) {
        ZeroMemory(sendBuff, BUF_SIZE);
        cout << "用户[" << clientId << "]: ";
        cout.flush(); // 确保输出立即显示
        cin.getline(sendBuff, BUF_SIZE);
        context_start_line++;

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
