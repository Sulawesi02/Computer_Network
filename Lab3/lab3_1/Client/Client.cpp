#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include <time.h>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")

using namespace std;

#define SERVER_PORT 3410
#define CLIENT_PORT 3411
#define ROUTER_PORT 3412
#define BUFFER sizeof(Message)

SOCKADDR_IN serverAddr, clientAddr;
SOCKET socketClient;
int len = sizeof(SOCKADDR);
const int RTO = 2 * CLOCKS_PER_SEC;//超时重传时间
bool quit = false;

class Message {
public:
    u_long flag;        // 伪首部
    u_short seq;        // 序列号
    u_short ack;        // 确认号
    u_long len;         // 数据部分长度
    u_long num;         // 数据包个数
    u_short checksum;   // 校验和
    char data[1024];    // 数据

    Message() { memset(this, 0, sizeof(Message)); }

    bool isSYN() { return this->flag & 1; }

    bool isACK() { return this->flag & 2; }

    bool isFIN() { return this->flag & 4; }

    bool isSTART() { return this->flag & 8; }

    bool isEND() { return this->flag & 16; }


    void setSYN() { this->flag |= 1; }

    void setACK() { this->flag |= 2; }

    void setFIN() { this->flag |= 4; }

    void setSTART() { this->flag |= 8; }

    void setEND() { this->flag |= 16; }

    void setChecksum() {
        int sum = 0;
        u_char* temp = (u_char*)this;
        for (int i = 0; i < 8; i++) {
            sum += (temp[i << 1] << 8) + temp[i << 1 | 1];
            while (sum >= 0x10000) {
                // 溢出
                int t = sum >> 16;  // 将最高位回滚添加至最低位
                sum += t;
            }
        }
        this->checksum = ~(u_short)sum;  // 按位取反，方便校验计算
    }

    bool packetCorruption() {
        int sum = 0;
        u_char* temp = (u_char*)this;
        for (int i = 0; i < 8; i++) {
            sum += (temp[i << 1] << 8) + temp[i << 1 | 1];
            while (sum >= 0x10000) {
                // 溢出
                int t = sum >> 16; // 计算方法与设置校验和相同
                sum += t;
            }
        }
        // 把计算出来的校验和和报文中该字段的值相加，如果等于0xffff，则校验成功
        if (checksum + (u_short)sum == 65535)
            return false;
        return true;
    }
};

bool waitConnect() {
    Sleep(100);

    Message sendMsg, recvMsg;
    clock_t start;

    // 设置套接字为非阻塞模式
    int mode = 1;
    ioctlsocket(socketClient, FIONBIO, (u_long FAR*) & mode);

    // 发送第一次握手消息
    cout << "尝试建立连接！客户端发送第一次握手消息" << endl;
    sendMsg.setSYN();
    sendMsg.seq = 2000;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第一次握手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 接收第二次握手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "客户端接收到第二次握手消息！第二次握手成功!" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "第一次握手超时,客户端重新发送第一次握手消息" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "客户端发送第一次握手消息失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // 发送第三次握手消息
    cout << "客户端发送第三次握手消息" << endl;
    sendMsg.setACK();
    sendMsg.seq = 2001;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第三次握手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    return 1;
}

bool closeConnect() {
    Message sendMsg, recvMsg;
    clock_t start;

    // 发送第一次挥手消息
    cout << "尝试关闭连接！客户端发送第一次挥手消息" << endl;
    sendMsg.setFIN();
    sendMsg.seq = 3000;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第一次挥手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 接收第二次挥手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "客户端接收到第二次挥手消息！第二次挥手成功！" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "第一次挥手超时,客户端重新发送第一次挥手消息" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "客户端发送第一次挥手消息失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // 接收第三次挥手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "客户端接收到第三次挥手消息！第三次挥手成功！" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "第二次挥手超时,客户端重新发送第二次挥手消息" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "客户端发送第二次挥手消息失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // 发送第四次挥手消息
    cout << "客户端发送第四次挥手消息！" << endl;
    sendMsg.setACK();
    sendMsg.seq = 3001;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第四次挥手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    return 1;
}

void send_file() {
    Message sendMsg, recvMsg;
    clock_t start, end;
    char filePath[20];
    ifstream in;
    int filePtrLoc;
    int dataAmount;
    int packetNum;
    int checksum;

    cout << "请输入要发送的文件名：";
    memset(filePath, 0, 20);
    string temp;
    cin >> temp;
    string inputPath = "./input/" + temp;

    if (temp == "quit") {
        closeConnect();
        quit = true;
        return;
    }
    else if (temp == "1.jpg" || temp == "2.jpg" || temp == "3.jpg" || temp == "helloworld.txt") {
        strcpy_s(filePath, sizeof(filePath), temp.c_str());
        in.open(inputPath, ifstream::in | ios::binary);// 以读取模式、二进制方式打开文件
        in.seekg(0, ios_base::end);// 将文件流指针移动到文件的末尾
        dataAmount = in.tellg();//文件大小（以字节为单位） 
        filePtrLoc = dataAmount;
        packetNum = filePtrLoc / 1024 + 1;//数据包数量
        in.seekg(0, ios_base::beg);// 将文件流指针移回文件的开头
        cout << "文件" << temp << "有" << packetNum << "个数据包" << endl;
    }
    else {
        cout << "文件不存在，请重新输入您要传输的文件名！" << endl;
        return;
    }

    // 发送第一个包，内容是文件名
    cout << "客户端发送文件名" << endl;
    memcpy(sendMsg.data, filePath, strlen(filePath));
    sendMsg.setSTART();
    sendMsg.seq = 0;
    sendMsg.len = strlen(filePath);
    sendMsg.num = packetNum;
    sendMsg.setChecksum();
    cout << "checksum：" << sendMsg.checksum << endl;
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送文件名失败!" << endl;
        return;
    }

    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "客户端发送文件名成功!" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "应答超时，客户端重新发送文件名" << endl;
            sendMsg.setChecksum();
            cout << "checksum：" << sendMsg.checksum << endl << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "客户端发送文件名失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return;
            }
            start = clock();
        }
    }

    // 开始发送文件内容
    cout << "客户端开始发送文件内容！" << endl;
    int seq = 1;
    start = clock();
    for (int i = 0; i < packetNum; i++) {
        if (i == packetNum - 1) {
            in.read(sendMsg.data, filePtrLoc);
            sendMsg.seq = seq;
            sendMsg.len = filePtrLoc;
            sendMsg.setEND(); // 文件结束标志
            filePtrLoc = 0;
        }
        else {
            in.read(sendMsg.data, 1024);// 读取文件数据
            sendMsg.seq = seq;
            sendMsg.len = 1024;
            filePtrLoc -= 1024;
        }

        // 发送数据包
        sendMsg.seq = seq;
        sendMsg.setChecksum();
        cout << "checksum：" << sendMsg.checksum << endl << endl;
        cout << "发送seq为" << seq << "的数据包" << endl;
        if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
            cout << "发送数据包失败!" << endl;
        }

        // 设置套接字为非阻塞模式
        int mode = 1;
        ioctlsocket(socketClient, FIONBIO, (u_long FAR*) & mode);
        int count = 0;

        clock_t c = clock();
        while (1) {
            // 尝试接收ack
            if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len)) {
                if (recvMsg.isACK() && recvMsg.ack == seq && !recvMsg.packetCorruption()) {
                    break;
                }
            }

            // 检查是否超时
            if (clock() - c > RTO) {
                cout << "应答超时，重新发送数据包" << endl;
                if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "发送数据包失败!" << endl;
                }
                count++;
                cout << "尝试重新发送seq为" << seq << "的数据包第" << count << "次，最多5次" << endl;
                if (count >= 5) {
                    cout << "尝试次数超过5次，退出发送" << endl;
                    return;
                }
                c = clock();
            }
            count = 0;
            // 为了避免CPU占用率过高，添加延迟
            Sleep(2);
        }
        seq++;
    }
    end = clock();
    cout << "成功发送文件！" << endl;

    double TotalTime = (double)(end - start) / CLOCKS_PER_SEC;
    cout << "传输总时间: " << TotalTime << "s" << endl;
    cout << "吞吐率: " << (double)dataAmount / TotalTime << " bytes/s" << endl << endl;

    // 关闭文件并准备发送下一个文件
    in.close();
    in.clear();
}


int main() {
    WSAData wsd;

    // 初始化Socket库，协商使用的Socket版本
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        cout << "初始化Socket库失败！" << endl;
        return 1;
    }
    cout << "初始化Socket库成功！" << endl;

    // 创建客户器socket
    socketClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketClient == INVALID_SOCKET) {
        cout << "创建客户端socket失败！" << endl;
        WSACleanup(); // 释放socket库资源
        return 1;
    }
    cout << "创建客户端socket成功！" << endl;

    // 给客户端绑定地址和端口
    clientAddr.sin_family = AF_INET; // IPv4地址族
    inet_pton(AF_INET, "127.0.0.1", &clientAddr.sin_addr.s_addr); // 客户端的IP地址
    clientAddr.sin_port = htons(CLIENT_PORT); // 客户端的端口号

    // 给ROUTER绑定地址和端口
    serverAddr.sin_family = AF_INET; // IPv4地址族
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr.s_addr); // ROUTER的IP地址
    serverAddr.sin_port = htons(ROUTER_PORT); // ROUTER的端口号

    if (bind(socketClient, (LPSOCKADDR)&clientAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
        cout << "绑定客户端地址和端口失败！" << endl;
        closesocket(socketClient); // 关闭客户端
        WSACleanup(); // 释放socket库资源
        return -1;
    }
    cout << "绑定客户端地址和端口成功！" << endl;
    cout << "-----------------------------------------------" << endl;

    cout << "客户端等待连接" << endl;
    if (waitConnect()) {
        cout << "连接成功！" << endl;
    }
    else {
        cout << "连接失败！" << endl;
        return 1;
    }
    cout << "-----------------------------------------------" << endl;
    cout << "输入“quit”关闭连接！" << endl;

    while (!quit) {
        send_file();
    }

    closesocket(socketClient);
    WSACleanup();
    system("pause");
    return 0;
}