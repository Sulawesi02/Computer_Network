#include<iostream>
#include <WinSock2.h>
#include<ws2tcpip.h>
#include<string>
#include<thread>
#include<vector>
#include<map>

#pragma comment(lib, "ws2_32.lib") // 导入Socket库

#define PORT 3410  // 监听端口
#define BUF_SIZE 1024  // 缓冲区大小

using namespace std;

map<int, SOCKET> clients; // 存储客户端套接字，使用用户ID作为键
int user_id = 1;  // 用于生成用户ID

// 处理客户端通信
void handleClient(int clientId, SOCKET acceptSocket) {
	char recvBuff[BUF_SIZE];// 接收缓冲区

	// 发送clientId给客户端
	string clientIdMessage = "clientId:" + to_string(clientId);
	send(acceptSocket, clientIdMessage.c_str(), clientIdMessage.length(), 0);

	while (true) {
		ZeroMemory(recvBuff, BUF_SIZE);//  清空缓冲区，避免读取到残留数据
		int recvBytes = recv(acceptSocket, recvBuff, BUF_SIZE, 0);
		
		string message = recvBuff;

		// 客户端请求退出
		if (recvBytes <= 0 || message == "exit" || message == "quit") {
			cout << "用户[" << clientId << "]退出聊天！" << endl;
			closesocket(acceptSocket);
			
			// 从clients中移除客户端
			clients.erase(clientId);

			// 广播退出消息给其他客户端
			string exitMessage = "用户[" + to_string(clientId) + "]退出聊天！";
			for (const auto& pair : clients) {
				send(pair.second, exitMessage.c_str(), exitMessage.length(), 0);
			}
			return; // 直接返回，不再关闭socket
		}

		cout << "用户[" << clientId << "]: " << message << endl;
		// 广播消息给所有客户端
		string fullMessage = "用户[" + to_string(clientId) + "]: " + message;
		for (const auto& pair : clients) {
			if (pair.first != clientId) {
				send(pair.second, fullMessage.c_str(), fullMessage.length(), 0);
			}
		}
	}
}

int main() {
	WSAData wsd;
	SOCKET socketServer;
	SOCKADDR_IN serverAddr;

	// 初始化Socket库，协商使用的Socket版本
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		cout << "初始化Socket库失败！" << endl;
		return 1;
	}
	cout << "初始化Socket库成功！" << endl;
	cout << "===============================================" << endl;

	// 创建服务器端socket
	socketServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 采用IPv4地址族、流式套接字以及TCP协议 
	if (socketServer == INVALID_SOCKET) {
		cout << "创建服务器端socket失败！" << endl;
		WSACleanup();// 释放socket库资源
		return 1;
	}
	cout << "创建服务器端socket成功！" << endl;
	cout << "===============================================" << endl;

	// 给服务器绑定地址和端口
	serverAddr.sin_family = AF_INET;// IPv4地址族
	serverAddr.sin_addr.s_addr = INADDR_ANY;// 服务器可以接受任何IP地址的连接请求
	serverAddr.sin_port = htons(PORT);// 服务器的监听端口号

	if (bind(socketServer, (LPSOCKADDR)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
		cout << "绑定服务器地址和端口失败！" << endl;
		closesocket(socketServer);// 关闭服务器端
		WSACleanup();// 释放socket库资源
		return -1;
	}
	cout << "绑定服务器地址和端口成功！" << endl;
	cout << "===============================================" << endl;

	// 使服务器的socket处于监听状态，准备接受客户端的连接请求
	if (listen(socketServer, SOMAXCONN) == SOCKET_ERROR) {
		cout << "监听客户端连接失败！" << endl;
		closesocket(socketServer);
		WSACleanup();
		return -1;
	}
	cout << "监听客户端连接成功！" << endl;
	cout << "===============================================" << endl;

	// 循环接收客户端的连接请求
	while (true) {
		SOCKADDR_IN clientAddr;
		int addrLen = sizeof(clientAddr);

		// 接受客户端的连接请求
		SOCKET acceptSocket = accept(socketServer, (SOCKADDR*)&clientAddr, &addrLen);
		
		if (acceptSocket == INVALID_SOCKET) {
			cout << "客户端连接失败！" << endl;
			continue;
		}

		// 分配用户ID，并记录客户端Socket
		int clientId = user_id++;
		clients[clientId] = acceptSocket;

		cout << "用户[" << clientId << "]加入聊天！" << endl;

		// 为每个客户端创建一个线程来处理消息
		thread clientThread(handleClient, clientId, acceptSocket);
		clientThread.detach();// 分离线程，让它自己运行
	}

	closesocket(socketServer);// 关闭服务器
	WSACleanup();// 释放socket库资源
	return 0;
}