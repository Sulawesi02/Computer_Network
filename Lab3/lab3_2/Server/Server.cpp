#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include <time.h>
#include <fstream>
#include <map>

#pragma comment(lib,"ws2_32.lib")

using namespace std;

#define SERVER_PORT 3410
#define CLIENT_PORT 3411
#define ROUTER_PORT 3412
#define BUFFER sizeof(Message)
#define TIMEOUT 1000 //超时重传时间

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
        this->checksum = 0;// 清0校验和字段
        int dataLen = this->len;// 数据部分长度
        int paddingLen = (16 - (dataLen % 16)) % 16;// 数据部分需要填0的长度
        char* paddedData = new char[dataLen + paddingLen];// 填充后数据的总长度

        memcpy(paddedData, this->data, dataLen);
        memset(paddedData + dataLen, 0, paddingLen);

        // 分段求和，并处理溢出
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

SOCKADDR_IN serverAddr, routerAddr;
SOCKET socketServer;
int len = sizeof(SOCKADDR);
bool quit = false;
const int cwnd = 5; // 窗口大小
map<int, Message> recv_buffer; // 序列号及其报文的映射

bool waitConnect() {
    Message sendMsg, recvMsg;
    clock_t start;

    // 接收第一次握手消息
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && !recvMsg.packetIncorrection()) {
                cout << "服务器端接收到第一次握手消息！第一次握手成功!" << endl;
                break;
            }
        }
    }

    // 设置套接字为非阻塞模式
    int mode = 1;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    // 发送第二次握手消息
    cout << "服务器端发送第二次握手消息！" << endl;
    sendMsg.setSYN();
    sendMsg.setACK();
    sendMsg.seq = 1000;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "服务器端发送第二次握手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 接收第三次握手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "服务器端接收到第三次握手消息！第三次握手成功！" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "第二次握手超时,服务器端重新发送第二次握手消息" << endl;
            if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "服务器端发送第二次握手消息失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // 设置套接字为阻塞模式
    mode = 0;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    return 1;
}

bool closeConnect(Message recvMsg) {
    Message sendMsg;
    clock_t start;

    /*
        第一次挥手在recv_file函数里面处理
    */

    // 设置套接字为非阻塞模式
    int mode = 1;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    // 发送第二次挥手消息
    cout << "服务器端发送第二次挥手消息！" << endl;
    sendMsg.setACK();
    sendMsg.seq = 4000;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "服务器端发送第二次挥手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 发送第三次挥手消息
    cout << "服务器端发送第三次挥手消息！" << endl;
    sendMsg.setFIN();
    sendMsg.setACK();
    sendMsg.seq = 5000;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "服务器端发送第三次挥手消息失败!" << endl;
        cout << "当前网络状态不佳，请稍后再试" << endl;
        return 0;
    }

    // 接收第四次挥手消息，超时重传
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "服务器端接收到第四次挥手消息！第四次挥手成功！" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "第三次挥手超时,服务器端重新发送第三次挥手消息" << endl;
            if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "服务器端发送第三次挥手消息失败!" << endl;
                cout << "当前网络状态不佳，请稍后再试" << endl;
                return 0;
            }
            start = clock();
        }
    }
    return 0;
}

void recv_file() {
    cout << "服务器正在等待接收文件中......" << endl;
    Message recvMsg, sendMsg;
    clock_t start, end;
    char filePath[20];
    string outputPath;
    ofstream out;
    int dataAmount = 0;
    int packetNum;

    // 接收文件名
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            // 接收第一次挥手信息
            if (recvMsg.isFIN() && !recvMsg.packetIncorrection()) {
                cout << "客户端准备断开连接！进入挥手模式！" << endl;
                cout << "服务器端接收到第一次挥手消息！第一次挥手成功!" << endl;
                closeConnect(recvMsg);
                quit = true;
                return;
            }
            if (recvMsg.isSTART() && !recvMsg.packetIncorrection()) {
                ZeroMemory(filePath, 20);
                memcpy(filePath, recvMsg.data, recvMsg.len);
                outputPath = "./output/" + string(filePath);
                out.open(outputPath, ios::out | ios::binary);//以写入模式、二进制模式打开文件
                cout << "文件名为：" << filePath << endl;
                cout << "接收到的seq:" << recvMsg.seq << endl;
                cout << "checksum：" << recvMsg.checksum << endl;

                if (!out.is_open()) {
                    cout << "文件打开失败！！！" << endl;
                    exit(1);
                }

                packetNum = recvMsg.num;
                cout << "文件" << filePath << "有" << packetNum << "个数据包" << endl;

                // 发送ack给客户端
                sendMsg.setACK();
                sendMsg.ack = recvMsg.seq + 1;
                sendMsg.setChecksum();
                if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "服务器端发送ack报文失败!" << endl;
                    cout << "当前网络状态不佳，请稍后再试" << endl;
                    return;
                }
                break;
            }
        }
    }

    // 设置套接字为阻塞模式
    int mode = 0;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    // 开始接收文件内容
    cout << "服务器端开始接收文件内容！" << endl << endl;
    int expected_seq = 1;
    bool first_recv_incorrect_seq = true;
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            // 如果校验和错误
            if (recvMsg.packetIncorrection()) {
                cout << "checksum错误！" << endl;
                cout << "checksum：" << recvMsg.checksum << endl;
                continue;
            }

            // 第一个if是为了处理超时重传的善后工作
            // 例如客户端发送seq12345，seq3丢失
            // 接收seq12，加入接收缓冲区
            // 接收seq4，发送ack3给客户端，加入接收缓冲区
            // 接收seq5，加入接收缓冲区
            // 接收缓冲区:{seq1：pack1，seq2：pack2，seq4：pack4，seq5：pack5}
            // seq3超时重传，重新发送已发送未确认的seq345
            // 接收seq3，加入接收缓冲区
            // 此时接收缓冲区已满，写入文件
            // 更新expected_seq为seq6
            // 接收seq4，小于seq6，丢弃
            // 接收seq5，小于seq6，丢弃
            if (recvMsg.seq < expected_seq) {// 去重
                continue;
            }
            else {
                // 如果接收到的是期望接收到的seq
                if (recvMsg.seq == expected_seq) {
                    first_recv_incorrect_seq = true;
                    expected_seq++;
                }
                // 如果接收到的不是期望接收到的seq
                else{
                    if (first_recv_incorrect_seq) {
                        // 发送累计确认的ack给客户端
                        cout << "期望接收seq:" << expected_seq << endl;

                        sendMsg.setACK();
                        sendMsg.ack = expected_seq;
                        sendMsg.setChecksum();
                        cout << "发送累计确认ack:" << sendMsg.ack << endl << endl;
                        if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                            cout << "服务器端发送ack报文失败!" << endl;
                            cout << "当前网络状态不佳，请稍后再试" << endl;
                            return;
                        }
                    }
                    first_recv_incorrect_seq = false;
                }
                // 存储接收到的数据包
                recv_buffer[recvMsg.seq] = recvMsg;
                cout << "接收seq:" << recvMsg.seq << endl;
                cout << "len:" << recvMsg.len << endl;
                cout << "checksum：" << recvMsg.checksum << endl << endl;
            }

            // 判断接收缓冲区的seq是否按序排列且最后一个seq的标志位是isEND
            bool isSeqInOrder = true;
            int prevSeq = 0;
            for (auto it = recv_buffer.begin(); it != recv_buffer.end(); ++it) {
                if (prevSeq != 0 && it->first != prevSeq + 1) {
                    isSeqInOrder = false;
                    break;
                }
                prevSeq = it->first;
            }

            // 如果接收缓冲区满了或者接收到最后一组数据包
            if (isSeqInOrder && (recv_buffer.size() >= cwnd || recv_buffer.rbegin()->first == packetNum)) {
                cout << "接收缓冲区写入文件" << endl << endl;
                // 以追加模式打开文件，并写入文件
                ofstream out(outputPath, ios::app | std::ios::binary);
                // 遍历接收缓冲区，按序列号从小到大顺序将数据包写入文件
                for (auto it = recv_buffer.begin(); it != recv_buffer.end(); ++it) {
                    const Message& bufferRecvMsg = it->second;
                    out.write(bufferRecvMsg.data, bufferRecvMsg.len);// 写入数据到文件
                    dataAmount += bufferRecvMsg.len;
                }
                out.close();

                // 发送ack给客户端
                sendMsg.setACK();
                sendMsg.ack = recv_buffer.rbegin()->first + 1;
                sendMsg.setChecksum();
                cout << "发送ack:" << sendMsg.ack << endl << endl;
                if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "服务器端发送ack报文失败!" << endl;
                    cout << "当前网络状态不佳，请稍后再试" << endl;
                    return;
                }

                // 检查文件传输是否结束
                if (recv_buffer.rbegin()->first == packetNum) {
                    end = clock();
                    cout << "接收文件成功！" << endl;
                    out.close();
                    out.clear();

                    double TotalTime = (double)(end - start) / CLOCKS_PER_SEC;
                    cout << "传输总时间" << TotalTime << "s" << endl;
                    cout << "吞吐率" << (double)dataAmount / TotalTime << " bytes/s" << endl << endl;

                    return;
                }
                recv_buffer.clear(); // 清空接收缓冲区

                expected_seq = sendMsg.ack;
            }

        }
    }
}

int main() {
    WSAData wsd;

    // 初始化Socket库，协商使用的Socket版本
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        cout << "初始化Socket库失败！" << endl;
        return 1;
    }
    cout << "初始化Socket库成功！" << endl;


    // 创建服务器socket
    socketServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketServer == INVALID_SOCKET) {
        cout << "创建服务器端socket失败！" << endl;
        WSACleanup(); // 释放socket库资源
        return 1;
    }
    cout << "创建服务器端socket成功！" << endl;

    // 给服务器绑定地址和端口
    serverAddr.sin_family = AF_INET; // IPv4地址族
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr.s_addr); // 服务器的IP地址
    serverAddr.sin_port = htons(SERVER_PORT); // 服务器的监听端口号

    // 给ROUTER绑定地址和端口
    routerAddr.sin_family = AF_INET; // IPv4地址族
    inet_pton(AF_INET, "127.0.0.1", &routerAddr.sin_addr.s_addr); // ROUTER的IP地址
    routerAddr.sin_port = htons(ROUTER_PORT); // ROUTER的端口号

    if (bind(socketServer, (LPSOCKADDR)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
        cout << "绑定服务器地址和端口失败！" << endl;
        closesocket(socketServer); // 关闭服务器端
        WSACleanup(); // 释放socket库资源
        return -1;
    }
    cout << "绑定服务器地址和端口成功！" << endl;
    cout << "-----------------------------------------------" << endl;

    cout << "服务器等待连接" << endl;
    if (waitConnect()) {
        cout << "连接成功！" << endl;
    }
    else {
        cout << "连接失败！" << endl;
        return 1;
    }
    cout << "-----------------------------------------------" << endl;

    while (!quit) {
        recv_file();
    }

    closesocket(socketServer);
    WSACleanup();
    system("pause");
    return 0;
}