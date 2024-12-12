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
#define TIMEOUT 1000 //��ʱ�ش�ʱ��

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
        this->checksum = 0;// ��0У����ֶ�
        int dataLen = this->len;// ���ݲ��ֳ���
        int paddingLen = (16 - (dataLen % 16)) % 16;// ���ݲ�����Ҫ��0�ĳ���
        char* paddedData = new char[dataLen + paddingLen];// �������ݵ��ܳ���

        memcpy(paddedData, this->data, dataLen);
        memset(paddedData + dataLen, 0, paddingLen);

        // �ֶ���ͣ����������
        u_short* buffer = (u_short*)this;
        int sum = 0;
        for (int i = 0; i < (sizeof(Message) + paddingLen) / 2; i++) {
            sum += buffer[i];
            if (sum > 0xFFFF) {
                sum = (sum & 0xFFFF) + (sum >> 16);
            }
        }

        // ������ȡ��д��У����ֶ�
        this->checksum = ~sum;

        // �ͷŶ�̬������ڴ�
        delete[] paddedData;
    }
    bool packetIncorrection() {
        // �������ݳ��Ȳ����
        int dataLen = this->len;
        int paddingLen = (16 - (dataLen % 16)) % 16;

        // ʹ�ö�̬�ڴ����
        char* paddedData = new char[dataLen + paddingLen];
        memcpy(paddedData, this->data, dataLen);
        memset(paddedData + dataLen, 0, paddingLen);

        // ����16 - bit�η������
        u_short* buffer = (u_short*)this;
        int sum = 0;
        for (int i = 0; i < (sizeof(Message) + paddingLen) / 2; i++) {
            sum += buffer[i];
            if (sum > 0xFFFF) {
                sum = (sum & 0xFFFF) + (sum >> 16);
            }
        }

        // ���������ΪȫΪ1���޲�������в��
        bool result = sum != 0xFFFF;

        // �ͷŶ�̬������ڴ�
        delete[] paddedData;

        return result;
    }
};

SOCKADDR_IN serverAddr, routerAddr;
SOCKET socketServer;
int len = sizeof(SOCKADDR);
bool quit = false;
map<int, Message> recv_buffer; // ���кż��䱨�ĵ�ӳ��

bool waitConnect() {
    Message sendMsg, recvMsg;
    clock_t start;

    // ���յ�һ��������Ϣ
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && !recvMsg.packetIncorrection()) {
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
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�������˷��͵ڶ���������Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յ�����������Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "�������˽��յ�������������Ϣ�����������ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "�ڶ������ֳ�ʱ,�����������·��͵ڶ���������Ϣ" << endl;
            if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�������˷��͵����λ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յ��Ĵλ�����Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "�������˽��յ����Ĵλ�����Ϣ�����Ĵλ��ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "�����λ��ֳ�ʱ,�����������·��͵����λ�����Ϣ" << endl;
            if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            // ���յ�һ�λ�����Ϣ
            if (recvMsg.isFIN() && !recvMsg.packetIncorrection()) {
                cout << "�ͻ���׼���Ͽ����ӣ��������ģʽ��" << endl;
                cout << "�������˽��յ���һ�λ�����Ϣ����һ�λ��ֳɹ�!" << endl;
                closeConnect(recvMsg);
                quit = true;
                return;
            }
            if (recvMsg.isSTART() && !recvMsg.packetIncorrection()) {
                ZeroMemory(filePath, 20);
                memcpy(filePath, recvMsg.data, recvMsg.len);
                outputPath = "./output/" + string(filePath);
                out.open(outputPath, ios::out | ios::binary);//��д��ģʽ��������ģʽ���ļ�
                cout << "�ļ���Ϊ:" << filePath << endl;
                cout << "���յ���seq:" << recvMsg.seq << endl;
                cout << "checksum:" << recvMsg.checksum << endl;

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
                if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    cout << "�������˿�ʼ�����ļ����ݣ�" << endl << endl;
    int expected_seq = 1; // �����յ���seq
    start = clock();
    while (1) {
        if (recvfrom(socketServer, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            // ���У��ʹ���
            if (recvMsg.packetIncorrection()) {
                cout << "checksum����" << endl;
                cout << "checksum:" << recvMsg.checksum << endl;
                continue;
            }

            // ����ͻ��˷���seq8��9��10��11��12��13��14��15������seq9��14��ʧ
            // ����seq8��������ջ�������expected_seq=9
            // ����seq10������ack9���ͻ��ˣ�������ջ�����
            // ����seq11��������ջ�����
            // ����seq12��������ջ�����
            // ����seq13��������ջ�����
            // ����seq15��������ջ�����
            // ���ջ�����:{seq8��pack8��seq10��pack10��seq11��pack11��seq12��pack12��seq13��pack13��seq15��pack15}
            // seq9��ʱ�ش������·����ѷ���δȷ�ϵ�seq9��10��11��12��13��14��15
            // ����seq9��������ջ�������expected_seq=14
            // ����seq10��С��expected_seq������
            // ����seq11��С��expected_seq������
            // ����seq12��С��expected_seq������
            // ����seq13��С��expected_seq������
            // ����seq14��������ջ�������expected_seq=16
            // ����seq15��С��expected_seq������
            // ��ʱ���ջ�����������д���ļ�
            // ������յ����������յ���seq

            // �洢���յ������ݰ�
            recv_buffer[recvMsg.seq] = recvMsg;
            cout << "����seq:" << recvMsg.seq << endl;
            cout << "checksum:" << recvMsg.checksum << endl;

            if (recvMsg.seq == expected_seq) {

                auto last_seq = recv_buffer.rbegin()->first;

                // ������һ�����������к�
                bool foundGap = false;
                for (int i = expected_seq + 1; i <= last_seq; ++i) {
                    if (recv_buffer.find(i) == recv_buffer.end()) {
                        // �ҵ���һ��gap������һ�����������к�
                        expected_seq = i;
                        foundGap = true;
                        break;
                    }
                }
                if (!foundGap) {
                    // ���û���ҵ�gap����ôexpected_seq�������һ�����кż�1
                    expected_seq = last_seq + 1;
                }

                // ����ack���ͻ���
                sendMsg.setACK();
                sendMsg.ack = expected_seq;
                sendMsg.setChecksum();
                cout << "����ack:" << sendMsg.ack << endl << endl;
                if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "�������˷���ack����ʧ��!" << endl;
                    cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                    return;
                }
            }
            // ������յ��Ĳ����������յ�seq
            else {
                // �����ۼ�ȷ�ϵ�ack���ͻ���
                cout << "��������seq:" << expected_seq << endl;

                sendMsg.setACK();
                sendMsg.ack = expected_seq;
                sendMsg.setChecksum();
                cout << "�����ۼ�ȷ��ack:" << sendMsg.ack << endl << endl;
                if (sendto(socketServer, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "�������˷���ack����ʧ��!" << endl;
                    cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                    return;
                }
            }

            // ������յ��������ݰ�
            if (expected_seq == packetNum + 1) {
                cout << "���ջ�����д���ļ�" << endl << endl;
                // ��׷��ģʽ���ļ�����д���ļ�
                ofstream out(outputPath, ios::app | std::ios::binary);
                // �������ջ������������кŴ�С����˳�����ݰ�д���ļ�
                for (auto it = recv_buffer.begin(); it != recv_buffer.end(); ++it) {
                    const Message& bufferRecvMsg = it->second;
                    out.write(bufferRecvMsg.data, bufferRecvMsg.len);
                    dataAmount += bufferRecvMsg.len;
                }
                out.close();
                recv_buffer.clear(); // ��ս��ջ�����


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
    routerAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &routerAddr.sin_addr.s_addr); // ROUTER��IP��ַ
    routerAddr.sin_port = htons(ROUTER_PORT); // ROUTER�Ķ˿ں�

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