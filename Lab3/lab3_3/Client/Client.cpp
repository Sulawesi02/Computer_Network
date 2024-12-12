#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include <map>
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
#define TIMEOUT 10000 //超时重传时间

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

SOCKADDR_IN routerAddr, clientAddr;
SOCKET socketClient;
int len = sizeof(SOCKADDR);
bool quit = false;
int rwnd = 20; // 接收窗口
int base = 1; // 窗口的起始序列号
int next_seq = 1; // 下一个待发送的序列号
mutex seq_mutex; // 序列号的互斥锁
bool send_over = false; // 传输完毕
map<int, Message> send_buffer; // 序列号及其报文的映射
map<int, clock_t> send_times;  // 序列号及其发送时间的映射

int cwnd = 1; // 拥塞窗口
int ssthresh = 16; // 慢启动阈值
int dup_ack_count = 0; // 冗余 ACK 计数器

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

    // 窗口内发送数据
    while (!send_over) {
        {
            unique_lock<mutex> lock(seq_mutex);// 加锁
            if (next_seq <= packetNum && (next_seq - base) < min(cwnd, rwnd)) {

                if (next_seq == packetNum) {
                    in.read(sendMsg.data, filePtrLoc);
                    sendMsg.len = filePtrLoc;
                    sendMsg.setEND();
                    filePtrLoc = 0;
                }
                else {
                    in.read(sendMsg.data, 1024);
                    sendMsg.len = 1024;
                    filePtrLoc -= 1024;
                }

                // 发送数据包
                sendMsg.seq = next_seq;
                sendMsg.setChecksum();
                if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "发送数据包失败!" << endl;
                }
                // 记录发送时间
                send_buffer[next_seq] = sendMsg;
                send_times[next_seq] = clock();

                cout << "发送seq:" << next_seq << endl;
                cout << "checksum:" << sendMsg.checksum << endl;
                cout << "ssthresh:" << ssthresh << endl;
                cout << "swnd:" << min(cwnd, rwnd) << endl;
                cout << "base:" << base << endl;

                next_seq++;

                cout << "next_seq:" << next_seq << endl;
                cout << "top:" << base + min(cwnd, rwnd) << endl << endl;
            }
            lock.unlock(); // 解锁
        }
        {
            unique_lock<mutex> lock(seq_mutex);// 加锁
            // 超时重传
            for (auto it = send_buffer.begin(); it != send_buffer.end(); it++) {
                if (clock() - send_times[it->first] > TIMEOUT) {
                    cout << "应答超时，重新发送已发送未确认的数据包" << endl;

                    Message sendMsg = it->second;
                    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                        cout << "重传数据包失败!" << endl;
                    }
                    // 重置计时器
                    send_times[it->first] = clock();

                    cout << "发送seq:" << it->first << endl;
                    cout << "checksum：" << sendMsg.checksum << endl;
                    cout << "ssthresh:" << ssthresh << endl;
                    cout << "swnd:" << min(cwnd, rwnd) << endl;
                    cout << "base：" << base << endl;
                    cout << "next_seq：" << next_seq << endl;
                    cout << "top：" << base + min(cwnd, rwnd) << endl << endl;

                    cout << "重新发送完毕" << endl;
                }
            }
            lock.unlock(); // 解锁
        }

    }
}

// 接收线程
void recv_thread(SOCKET socketClient, int packetNum) {
    Message recvMsg;
    clock_t start;
    int expected_ack = 2; // 期望收到的ack
    int prev_ack = 0; // 上一次收到的确认号

    while (!send_over) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            {
                unique_lock<mutex> lock(seq_mutex);// 加锁
                if (recvMsg.isACK() && !recvMsg.packetIncorrection()) {
                    if (recvMsg.ack > prev_ack) { // 新的 ack
                        cout << "接收新的ack:" << recvMsg.ack << endl;
                        dup_ack_count = 0; // 重置冗余 ACK 计数
                        if (recvMsg.ack >= expected_ack) {
                            // 移除已确认的数据包
                            auto it = send_buffer.begin();
                            while (it != send_buffer.end() && it->first < recvMsg.ack) {
                                it = send_buffer.erase(it); // 从发送缓冲区中删去对应报文
                                send_times.erase(recvMsg.ack); // 同时移除计时器
                            }
                            base = recvMsg.ack;

                            if (cwnd < ssthresh) {
                                // 慢启动
                                cout << "慢启动:" << endl;
                                cwnd *= 2;
                            }
                            else {
                                // 拥塞避免
                                cout << "拥塞避免:" << endl;
                                cwnd += 1;
                            }
                            expected_ack += min(cwnd, rwnd);
                        }
                        prev_ack = recvMsg.ack;

                        // 展示窗口情况
                        cout << "ssthresh:" << ssthresh << endl;
                        cout << "swnd:" << min(cwnd, rwnd) << endl;
                        cout << "base:" << base << endl;
                        cout << "next_seq:" << next_seq << endl;
                        cout << "top:" << base + min(cwnd, rwnd) << endl << endl;

                    }
                    else if (recvMsg.ack == prev_ack) { //冗余 ack
                        if (dup_ack_count < 3) {
                            dup_ack_count++;
                            cout << "接收到第" << dup_ack_count << "个冗余ack:" << recvMsg.ack << endl;
                            cwnd += 1;
                        }
                        if (dup_ack_count == 3) { // 收到第3个冗余 ACK
                            // 快重传
                            cout << "快重传:" << endl;
                            Message sendMsg = send_buffer.find(prev_ack)->second;
                            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                                cout << "重传数据包失败!" << endl;
                            }
                            cout << "发送seq:" << sendMsg.seq << endl;
                            cout << "checksum:" << sendMsg.checksum << endl;
                            cout << "ssthresh:" << ssthresh << endl << endl;

                            // 快恢复
                            cout << "快恢复:" << endl;
                            ssthresh = cwnd / 2;
                            cwnd = ssthresh + 3;

                            // 展示窗口情况
                            cout << "ssthresh:" << ssthresh << endl;
                            cout << "swnd:" << min(cwnd, rwnd) << endl;
                            cout << "base:" << base << endl;
                            cout << "next_seq:" << next_seq << endl;
                            cout << "top:" << base + min(cwnd, rwnd) << endl << endl;

                            dup_ack_count = INT_MAX;
                        }
                    }
                    // 如果所有文件传输结束
                    if (recvMsg.ack == packetNum + 1) {
                        cout << "文件传输完成！" << endl;
                        send_over = true;
                    }
                }
                lock.unlock(); // 解锁
            }
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

    cout << "请输入要发送的文件名:";
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
    cout << "checksum:" << sendMsg.checksum << endl;
    cout << "base:" << base << endl;
    cout << "next_seq:" << next_seq << endl;
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
            cout << "checksum:" << sendMsg.checksum << endl << endl;
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
    send_over = false; // 传输完毕
    cwnd = 1; // 拥塞窗口
    ssthresh = 16; // 慢启动阈值
    dup_ack_count = 0; // 冗余 ACK 计数器
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