#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <map>

#pragma comment(lib, "ws2_32.lib") // 导入Socket库

#define PORT 3410 // 监听端口
#define BUF_SIZE 1024 // 缓冲区大小
#define MAX_CLIENTS 5 // 最大客户端数量

using namespace std;

map<int, SOCKET> accepts; // 用户ID作为键，连接套接字作为键值
int user_id; // 用户ID
bool isFull = false; // 是否已达连接上限

// 处理客户端通信（接收客户端消息并处理）
void handleAccept(int clientId, SOCKET acceptSocket) {
	char recvBuff[BUF_SIZE];// 接收缓冲区

	while (true) {
		ZeroMemory(recvBuff, BUF_SIZE);// 清空缓冲区，避免读取到残留数据
		//客户端关闭程序时，也会关闭与服务器的套接字连接。服务器检测到连接断开，recv()函数会收到一个返回值 0 或者负数，
		int recvBytes = recv(acceptSocket, recvBuff, BUF_SIZE, 0);
		string message = recvBuff;

		if (recvBytes <= 0 || message == "exit" || message == "quit") {
			// 客户端请求退出
			// 三种方式：直接关闭窗口，或者发送"exit" "quit"
			string exitMessage = "用户[" + to_string(clientId) + "]退出聊天！";
			cout << exitMessage << endl;// 打印用户退出聊天信息
			closesocket(acceptSocket);// 关闭该连接套接字
			accepts.erase(clientId);// 移除用户ID-连接套接字键值对

			// 广播退出消息给其他客户端
			for (const auto& pair : accepts) {
				send(pair.second, exitMessage.c_str(), exitMessage.length(), 0);
			}
			isFull = false;
			return;
		}
		else {
			string chatMessage = "用户[" + to_string(clientId) + "]: " + message;
			cout << chatMessage << endl;// 打印用户聊天信息
			// 广播消息给所有客户端
			for (const auto& pair : accepts) {
				if (pair.first != clientId) {
					send(pair.second, chatMessage.c_str(), chatMessage.length(), 0);
				}
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
	cout << "-----------------------------------------------" << endl;

	// 创建服务器socket
	socketServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 采用IPv4地址族、流式套接字以及TCP协议 
	if (socketServer == INVALID_SOCKET) {
		cout << "创建服务器端socket失败！" << endl;
		WSACleanup();// 释放socket库资源
		return 1;
	}
	cout << "创建服务器端socket成功！" << endl;
	cout << "-----------------------------------------------" << endl;

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
	cout << "-----------------------------------------------" << endl;

	// 使服务器socket处于监听状态，准备接受客户端的连接请求
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
		SOCKET acceptSocket = accept(socketServer, (SOCKADDR*)&clientAddr, &addrLen);//连接套接字，用于与客户端通信
		if (acceptSocket == INVALID_SOCKET) {
			cout << "客户端连接失败！" << endl;
			continue;
		}

		user_id = 1;// 从1开始寻找第一个可用的ID
		while (accepts.find(user_id) != accepts.end()) {
			++user_id;
		}

		if (user_id > MAX_CLIENTS) {
			// 为了解决聊天室人满了之后，客户一发送消息，服务器就打印"当前聊天室人数已到上限！"
			if (!isFull) { // 只有当 isFull 之前为 false 时才打印
				isFull = true;
				cout << "当前聊天室人数已到上限！" << endl;
			}
			continue;
		}

		// 分配用户ID，并记录连接套接字
		accepts[user_id] = acceptSocket;

		// 发送用户ID给客户端
		send(acceptSocket, (char*)&user_id, sizeof(user_id), 0);

		string enterMessage = "用户[" + to_string(user_id) + "]加入聊天！";
		cout << enterMessage << endl;
		// 广播进入消息给所有客户端
		for (const auto& pair : accepts) {
			send(pair.second, enterMessage.c_str(), enterMessage.length(), 0);
		}

		// 创建线程让服务器接收并处理消息
		thread acceptThread(handleAccept, user_id, acceptSocket);
		acceptThread.detach();// 分离线程，让它自己运行
	}

	closesocket(socketServer);// 关闭服务器
	WSACleanup();// 释放socket库资源
	return 0;
}