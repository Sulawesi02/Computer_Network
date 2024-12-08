#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include <time.h>
#include <fstream>
#include <thread>
#include <mutex>

#pragma comment(lib,"ws2_32.lib")

using namespace std;

#define SERVER_PORT 3410
#define CLIENT_PORT 3411
#define ROUTER_PORT 3412
#define BUFFER sizeof(Message)
#define TIMEOUT 100 //超时重传时间
#define MAX_RETURN_TIMES 5 //超时重传次数


SOCKADDR_IN routerAddr, clientAddr;
SOCKET socketClient;
int len = sizeof(SOCKADDR);
bool quit = false;
const int cwnd = 5;
int base = 1; // 窗口的起始序列号
int next_seq = 1; // 下一个待发送的序列号
clock_t send_time = 0;  // 报文发送起始时间
mutex seq_mutex; // 序列号的互斥锁
bool send_over = false; // 传输完毕

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
        // 清0校验和字段
        this->checksum = 0;

        // 计算数据长度并填充
        int dataLen = this->len;
        int paddingLen = (16 - (dataLen % 16)) % 16;

        // 使用动态内存分配
        char* paddedData = new char[dataLen + paddingLen];
        memcpy(paddedData, this->data, dataLen);
        memset(paddedData + dataLen, 0, paddingLen);

        // 进行16 - bit段反码求和
        u_short* buffer = (u_short*)this;
        int sum = 0;
        for (int i = 0; i < (sizeof(Message) + paddingLen) / 2; i++) {
            sum += buffer[i];
            if (sum > 0xFFFF) {
                sum = (sum & 0xFFFF) + (sum >> 16);
            }
        }

        // 计算结果取反写入校验和字段
        this->checksum = ~sum;

        // 释放动态分配的内存
        delete[] paddedData;
    }
    bool packetIncorrection() {
        // 计算数据长度并填充
        int dataLen = this->len;
        int paddingLen = (16 - (dataLen % 16)) % 16;

        // 使用动态内存分配
        char* paddedData = new char[dataLen + paddingLen];
        memcpy(paddedData, this->data, dataLen);
        memset(paddedData + dataLen, 0, paddingLen);

        // 进行16 - bit段反码求和
        u_short* buffer = (u_short*)this;
        int sum = 0;
        for (int i = 0; i < (sizeof(Message) + paddingLen) / 2; i++) {
            sum += buffer[i];
            if (sum > 0xFFFF) {
                sum = (sum & 0xFFFF) + (sum >> 16);
            }
        }

        // 如果计算结果为全为1则无差错；否则，有差错
        bool result = sum != 0xFFFF;

        // 释放动态分配的内存
        delete[] paddedData;

        return result;
    }
};

bool waitConnect() {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第一次握手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 接收第二次握手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "客户端接收到第二次握手消息！第二次握手成功!" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "第一次握手超时,客户端重新发送第一次握手消息" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第一次挥手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 接收第二次挥手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "客户端接收到第二次挥手消息！第二次挥手成功！" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "第一次挥手超时,客户端重新发送第一次挥手消息" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "客户端接收到第三次挥手消息！第三次挥手成功！" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "第二次挥手超时,客户端重新发送第二次挥手消息" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送第四次挥手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    return 1;
}


// 发送线程
void send_thread(SOCKET socketClient, sockaddr_in& routerAddr, ifstream& in,int filePtrLoc, int packetNum) {
    Message sendMsg;
    int count = 0;

    // 窗口内发送数据
    while (1) {
        if (send_over) {
            break;
        }
        if(next_seq <= packetNum && (next_seq - base) < cwnd){
            {
                cout << "发送线程准备加锁" << endl;
                unique_lock<mutex> lock(seq_mutex);// 加锁
                cout << "发送线程加锁成功" << endl;
                if (next_seq == packetNum) {
                    in.read(sendMsg.data, filePtrLoc);
                    sendMsg.len = filePtrLoc;
                    sendMsg.setEND(); // 文件结束标志
                    filePtrLoc = 0;
                }
                else {
                    in.read(sendMsg.data, 1024);// 读取文件数据
                    sendMsg.len = 1024;
                    filePtrLoc -= 1024;
                }

                // 发送数据包
                sendMsg.seq = next_seq;
                sendMsg.setChecksum();
                cout << "发送seq:" << next_seq << endl;
                cout << "base：" << base << endl;
                cout << "next_seq：" << next_seq << endl;
                cout << "len:" << sendMsg.len << endl;
                cout << "checksum：" << sendMsg.checksum << endl << endl;

                if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "发送数据包失败!" << endl;
                }

                if (base == next_seq) {
                    send_time = clock();
                }
                next_seq++;
                cout << "发送线程准备解锁" << endl;
            }
            cout << "发送线程解锁成功" << endl;

        }
        // 超时重传
        if(clock() - send_time > TIMEOUT){
            {
                cout << "发送线程准备加锁" << endl;
                unique_lock<mutex> lock(seq_mutex);// 加锁
                cout << "发送线程加锁成功" << endl;
                cout << "应答超时，重新发送未确认的数据包" << endl;

                for (int i = base; i < next_seq; i++) {
                    in.seekg((i - 1) * 1024, ios::beg);// 重置文件指针到正确的位置
                    if (i == packetNum) {
                        in.read(sendMsg.data, filePtrLoc);
                        sendMsg.len = filePtrLoc;
                        sendMsg.setEND(); // 文件结束标志
                    }
                    else {
                        in.read(sendMsg.data, 1024);// 读取文件数据
                        sendMsg.len = 1024;
                    }

                    // 发送数据包
                    sendMsg.seq = i;
                    sendMsg.setChecksum();
                    cout << "发送seq:" << i << endl;
                    cout << "base：" << base << endl;
                    cout << "next_seq：" << i << endl;
                    cout << "len:" << sendMsg.len << endl;
                    cout << "checksum：" << sendMsg.checksum << endl << endl;
                    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                        cout << "发送数据包失败!" << endl;
                    }
                    cout << "发送线程准备解锁" << endl;
                }
                cout << "发送线程解锁成功" << endl;
                send_time = clock();
            }
        }
    }
}

// 接收线程
void recv_thread(SOCKET socketClient, int packetNum) {
    Message recvMsg;
    clock_t start;

    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            cout << "接收线程准备加锁" << endl;
            unique_lock<mutex> lock(seq_mutex);// 加锁
            cout << "接收线程加锁成功" << endl;
            {
                if (recvMsg.isACK() && !recvMsg.packetIncorrection()) {
                    cout << "接收ack：" << recvMsg.ack << endl;
                    cout << "base：" << base << endl;

                    if (recvMsg.ack <= base + cwnd) {
                        base = recvMsg.ack;
                        if (recvMsg.ack % cwnd == 1) {
                            cout << "更新超时时间！" << endl;
                            send_time = clock();
                        }
                        else {
                            cout << endl << "发生丢包，不更新超时时间！" << endl << endl;
                        }
                    }
                    // 展示窗口情况
                    cout << "base=接收ack=" << base << endl;
                    cout << "next_seq：" << next_seq << endl;
                    cout << "窗口内已发送但未收到ack的包：" << next_seq - base << endl;
                    cout << "窗口内未发送的包：" << cwnd - (next_seq - base) << endl << endl;
                    // 如果所有文件传输结束
                    if (recvMsg.ack == packetNum + 1) {
                        cout << "文件传输完成！" << endl;
                        send_over = true;
                        break;
                    }
                }
                cout << "接收线程准备解锁" << endl;
            }
            cout << "接收线程解锁成功" << endl;
        }
    }
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
    cout << "发送seq:" << sendMsg.seq << endl;
    cout << "base：" << base << endl;
    cout << "next_seq：" << next_seq << endl;
    cout << "len:" << sendMsg.len << endl;
    cout << "checksum：" << sendMsg.checksum << endl;
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "客户端发送文件名失败!" << endl;
        return;
    }

    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "客户端发送文件名成功!" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "应答超时，客户端重新发送文件名" << endl;
            cout << "checksum：" << sendMsg.checksum << endl << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "客户端发送文件名失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return;
            }
            start = clock();
        }
    }
    
    // 开始发送文件内容
    cout << "客户端开始发送文件内容！" << endl << endl;
    start = clock();

    // 创建发送和接收线程
    thread sender(send_thread, socketClient, ref(routerAddr), ref(in), filePtrLoc, packetNum);
    thread receiver(recv_thread, socketClient, packetNum);

    sender.join(); // 等待发送线程完成
    receiver.join(); // 等待接收线程完成

    end = clock();
    cout << "成功发送文件！" << endl;

    double TotalTime = (double)(end - start) / CLOCKS_PER_SEC;
    cout << "传输总时间: " << TotalTime << "s" << endl;
    cout << "吞吐率: " << (double)dataAmount / TotalTime << " bytes/s" << endl << endl;

    // 关闭文件并准备发送下一个文件
    in.close();
    in.clear();

    base = 1; // 窗口的起始序列号
    next_seq = 1; // 下一个待发送的序列号
    send_time = 0;  // 报文发送起始时间
    send_over = false; // 传输完毕
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
    routerAddr.sin_family = AF_INET; // IPv4地址族
    inet_pton(AF_INET, "127.0.0.1", &routerAddr.sin_addr.s_addr); // ROUTER的IP地址
    routerAddr.sin_port = htons(ROUTER_PORT); // ROUTER的端口号

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