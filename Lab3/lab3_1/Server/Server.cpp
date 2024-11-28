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
SOCKET socketServer;
int len = sizeof(SOCKADDR);
const int RTO = 2 * CLOCKS_PER_SEC;//��ʱ�ش�ʱ��
bool quit = false;

class Message {
public:
    u_long flag;        // α�ײ�
    u_short seq;        // ���к�
    u_short ack;        // ȷ�Ϻ�
    u_long len;         // ���ݲ��ֳ���
    u_long num;         // ���ݰ�����
    u_short checksum;   // У���
    char data[1024];    // ����

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
                // ���
                int t = sum >> 16;  // �����λ�ع���������λ
                sum += t;
            }
        }
        this->checksum = ~(u_short)sum;  // ��λȡ��������У�����
    }

    bool packetCorruption() {
        int sum = 0;
        u_char* temp = (u_char*)this;
        for (int i = 0; i < 8; i++) {
            sum += (temp[i << 1] << 8) + temp[i << 1 | 1];
            while (sum >= 0x10000) {
                // ���
                int t = sum >> 16; // ���㷽��������У�����ͬ
                sum += t;
            }
        }
        // �Ѽ��������У��ͺͱ����и��ֶε�ֵ��ӣ��������0xffff����У��ɹ�
        if (checksum + (u_short)sum == 65535)
            return false;
        return true;
    }
};

bool waitConnect() {
    Message sendMsg, recvMsg;
    clock_t start;

    // ���յ�һ��������Ϣ
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && !recvMsg.packetCorruption()) {
                cout << "�������˽��յ���һ��������Ϣ����һ�����ֳɹ�!" << endl;
                break;
            }
        }
    }

    // �����׽���Ϊ������ģʽ
    int mode = 1;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    // ���͵ڶ���������Ϣ
    cout << "�������˷��͵ڶ���������Ϣ��" << endl;
    sendMsg.setSYN();
    sendMsg.setACK();
    sendMsg.seq = 1000;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�������˷��͵ڶ���������Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յ�����������Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "�������˽��յ�������������Ϣ�����������ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "�ڶ������ֳ�ʱ,�����������·��͵ڶ���������Ϣ" << endl;
            if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�������˷��͵ڶ���������Ϣʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // �����׽���Ϊ����ģʽ
    mode = 0;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    return 1;
}

bool closeConnect(Message recvMsg) {
    Message sendMsg;
    clock_t start;

    /*
        ��һ�λ�����recv_file�������洦��
    */

    // �����׽���Ϊ������ģʽ
    int mode = 1;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    // ���͵ڶ��λ�����Ϣ
    cout << "�������˷��͵ڶ��λ�����Ϣ��" << endl;
    sendMsg.setACK();
    sendMsg.seq = 4000;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�������˷��͵ڶ��λ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���͵����λ�����Ϣ
    cout << "�������˷��͵����λ�����Ϣ��" << endl;
    sendMsg.setFIN();
    sendMsg.setACK();
    sendMsg.seq = 5000;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�������˷��͵����λ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յ��Ĵλ�����Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "�������˽��յ����Ĵλ�����Ϣ�����Ĵλ��ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "�����λ��ֳ�ʱ,�����������·��͵����λ�����Ϣ" << endl;
            if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�������˷��͵����λ�����Ϣʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return 0;
            }
            start = clock();
        }
    }
    return 0;
}

void recv_file() {
    cout << "���������ڵȴ������ļ���......" << endl;
    Message recvMsg, sendMsg;
    clock_t start, end;
    char filePath[20];
    string outputPath;
    ofstream out;
    int dataAmount = 0;
    int packetNum;

    // �����ļ���
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, &len) != SOCKET_ERROR) {
            // ���յ�һ�λ�����Ϣ
            if (recvMsg.isFIN() && !recvMsg.packetCorruption()) {
                cout << "�ͻ���׼���Ͽ����ӣ��������ģʽ��" << endl;
                cout << "�������˽��յ���һ�λ�����Ϣ����һ�λ��ֳɹ�!" << endl;
                closeConnect(recvMsg);
                quit = true;
                return;
            }
            if (recvMsg.isSTART() && !recvMsg.packetCorruption()) {
                ZeroMemory(filePath, 20);
                memcpy(filePath, recvMsg.data, recvMsg.len);
                outputPath = "./output/" + string(filePath);
                out.open(outputPath, ios::out | ios::binary);//��д��ģʽ��������ģʽ���ļ�
                cout << "�ļ���Ϊ��" << filePath << endl;
                cout << "checksum��" << recvMsg.checksum << endl << endl;

                if (!out.is_open()) {
                    cout << "�ļ���ʧ�ܣ�����" << endl;
                    exit(1);
                }

                packetNum = recvMsg.num;
                cout << "�ļ�" << filePath << "��" << packetNum << "�����ݰ�" << endl;

                // ����ack���ͻ���
                sendMsg.setACK();
                sendMsg.ack = recvMsg.seq + 1;
                sendMsg.setChecksum();
                if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "�������˷���ack����ʧ��!" << endl;
                    cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                    return;
                }
                break;
            }
        }
    }

    // �����׽���Ϊ����ģʽ
    int mode = 0;
    ioctlsocket(socketServer, FIONBIO, (u_long FAR*) & mode);

    // ��ʼ�����ļ�����
    cout << "�������˿�ʼ�����ļ����ݣ�" << endl;
    int expectedSeq = 1;
    start = clock();
    for (int i = 0; i < packetNum; i++) {
        while (1) {
            if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, &len) != SOCKET_ERROR) {
                // ������к��Ƿ���ȷ
                if (recvMsg.seq == expectedSeq && !recvMsg.packetCorruption()) {
                    out.write(recvMsg.data, recvMsg.len);// д�����ݵ��ļ�
                    dataAmount += recvMsg.len;
                    out.close();

                    // ����ack���ͻ���
                    cout << "�յ�seqΪ" << recvMsg.seq << "�����ݰ�" << endl;
                    cout << "checksum��" << recvMsg.checksum << endl << endl;
                    sendMsg.setACK();
                    sendMsg.ack = recvMsg.seq;
                    sendMsg.setChecksum();
                    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                        cout << "�������˷���ack����ʧ��!" << endl;
                        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                        return;
                    }
                    expectedSeq++;
                }
                // ����ļ������Ƿ����
                if (recvMsg.isEND() && !recvMsg.packetCorruption()) {
                    end = clock();
                    cout << "�����ļ��ɹ���" << endl;
                    out.close();
                    out.clear();


                    double TotalTime = (double)(end - start) / CLOCKS_PER_SEC;
                    cout << "������ʱ��" << TotalTime << "s" << endl;
                    cout << "������" << (double)dataAmount / TotalTime << " bytes/s" << endl << endl;

                    return;
                }

            }
        }
    }
}

int main() {
    WSAData wsd;

    // ��ʼ��Socket�⣬Э��ʹ�õ�Socket�汾
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        cout << "��ʼ��Socket��ʧ�ܣ�" << endl;
        return 1;
    }
    cout << "��ʼ��Socket��ɹ���" << endl;


    // ����������socket
    socketServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketServer == INVALID_SOCKET) {
        cout << "������������socketʧ�ܣ�" << endl;
        WSACleanup(); // �ͷ�socket����Դ
        return 1;
    }
    cout << "������������socket�ɹ���" << endl;

    // ���������󶨵�ַ�Ͷ˿�
    serverAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr.s_addr); // ��������IP��ַ
    serverAddr.sin_port = htons(SERVER_PORT); // �������ļ����˿ں�

    // ��ROUTER�󶨵�ַ�Ͷ˿�
    clientAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &clientAddr.sin_addr.s_addr); // ROUTER��IP��ַ
    clientAddr.sin_port = htons(ROUTER_PORT); // ROUTER�Ķ˿ں�

    if (bind(socketServer, (LPSOCKADDR)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
        cout << "�󶨷�������ַ�Ͷ˿�ʧ�ܣ�" << endl;
        closesocket(socketServer); // �رշ�������
        WSACleanup(); // �ͷ�socket����Դ
        return -1;
    }
    cout << "�󶨷�������ַ�Ͷ˿ڳɹ���" << endl;
    cout << "-----------------------------------------------" << endl;

    cout << "�������ȴ�����" << endl;
    if (waitConnect()) {
        cout << "���ӳɹ���" << endl;
    }
    else {
        cout << "����ʧ�ܣ�" << endl;
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