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
    Sleep(100);

    Message sendMsg, recvMsg;
    clock_t start;

    // �����׽���Ϊ������ģʽ
    int mode = 1;
    ioctlsocket(socketClient, FIONBIO, (u_long FAR*) & mode);

    // ���͵�һ��������Ϣ
    cout << "���Խ������ӣ��ͻ��˷��͵�һ��������Ϣ" << endl;
    sendMsg.setSYN();
    sendMsg.seq = 2000;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵�һ��������Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յڶ���������Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "�ͻ��˽��յ��ڶ���������Ϣ���ڶ������ֳɹ�!" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "��һ�����ֳ�ʱ,�ͻ������·��͵�һ��������Ϣ" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�ͻ��˷��͵�һ��������Ϣʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // ���͵�����������Ϣ
    cout << "�ͻ��˷��͵�����������Ϣ" << endl;
    sendMsg.setACK();
    sendMsg.seq = 2001;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵�����������Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    return 1;
}

bool closeConnect() {
    Message sendMsg, recvMsg;
    clock_t start;

    // ���͵�һ�λ�����Ϣ
    cout << "���Թر����ӣ��ͻ��˷��͵�һ�λ�����Ϣ" << endl;
    sendMsg.setFIN();
    sendMsg.seq = 3000;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵�һ�λ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յڶ��λ�����Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "�ͻ��˽��յ��ڶ��λ�����Ϣ���ڶ��λ��ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "��һ�λ��ֳ�ʱ,�ͻ������·��͵�һ�λ�����Ϣ" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�ͻ��˷��͵�һ�λ�����Ϣʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // ���յ����λ�����Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "�ͻ��˽��յ������λ�����Ϣ�������λ��ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "�ڶ��λ��ֳ�ʱ,�ͻ������·��͵ڶ��λ�����Ϣ" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�ͻ��˷��͵ڶ��λ�����Ϣʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return 0;
            }
            start = clock();
        }
    }

    // ���͵��Ĵλ�����Ϣ
    cout << "�ͻ��˷��͵��Ĵλ�����Ϣ��" << endl;
    sendMsg.setACK();
    sendMsg.seq = 3001;
    sendMsg.ack = recvMsg.seq + 1;
    sendMsg.setChecksum();
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵��Ĵλ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
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

    cout << "������Ҫ���͵��ļ�����";
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
        in.open(inputPath, ifstream::in | ios::binary);// �Զ�ȡģʽ�������Ʒ�ʽ���ļ�
        in.seekg(0, ios_base::end);// ���ļ���ָ���ƶ����ļ���ĩβ
        dataAmount = in.tellg();//�ļ���С�����ֽ�Ϊ��λ�� 
        filePtrLoc = dataAmount;
        packetNum = filePtrLoc / 1024 + 1;//���ݰ�����
        in.seekg(0, ios_base::beg);// ���ļ���ָ���ƻ��ļ��Ŀ�ͷ
        cout << "�ļ�" << temp << "��" << packetNum << "�����ݰ�" << endl;
    }
    else {
        cout << "�ļ������ڣ�������������Ҫ������ļ�����" << endl;
        return;
    }

    // ���͵�һ�������������ļ���
    cout << "�ͻ��˷����ļ���" << endl;
    memcpy(sendMsg.data, filePath, strlen(filePath));
    sendMsg.setSTART();
    sendMsg.seq = 0;
    sendMsg.len = strlen(filePath);
    sendMsg.num = packetNum;
    sendMsg.setChecksum();
    cout << "checksum��" << sendMsg.checksum << endl;
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷����ļ���ʧ��!" << endl;
        return;
    }

    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetCorruption()) {
                cout << "�ͻ��˷����ļ����ɹ�!" << endl;
                break;
            }
        }
        if (clock() - start > RTO) {
            cout << "Ӧ��ʱ���ͻ������·����ļ���" << endl;
            sendMsg.setChecksum();
            cout << "checksum��" << sendMsg.checksum << endl << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�ͻ��˷����ļ���ʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return;
            }
            start = clock();
        }
    }

    // ��ʼ�����ļ�����
    cout << "�ͻ��˿�ʼ�����ļ����ݣ�" << endl;
    int seq = 1;
    start = clock();
    for (int i = 0; i < packetNum; i++) {
        if (i == packetNum - 1) {
            in.read(sendMsg.data, filePtrLoc);
            sendMsg.seq = seq;
            sendMsg.len = filePtrLoc;
            sendMsg.setEND(); // �ļ�������־
            filePtrLoc = 0;
        }
        else {
            in.read(sendMsg.data, 1024);// ��ȡ�ļ�����
            sendMsg.seq = seq;
            sendMsg.len = 1024;
            filePtrLoc -= 1024;
        }

        // �������ݰ�
        sendMsg.seq = seq;
        sendMsg.setChecksum();
        cout << "checksum��" << sendMsg.checksum << endl << endl;
        cout << "����seqΪ" << seq << "�����ݰ�" << endl;
        if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
            cout << "�������ݰ�ʧ��!" << endl;
        }

        // �����׽���Ϊ������ģʽ
        int mode = 1;
        ioctlsocket(socketClient, FIONBIO, (u_long FAR*) & mode);
        int count = 0;

        clock_t c = clock();
        while (1) {
            // ���Խ���ack
            if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, &len)) {
                if (recvMsg.isACK() && recvMsg.ack == seq && !recvMsg.packetCorruption()) {
                    break;
                }
            }

            // ����Ƿ�ʱ
            if (clock() - c > RTO) {
                cout << "Ӧ��ʱ�����·������ݰ�" << endl;
                if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "�������ݰ�ʧ��!" << endl;
                }
                count++;
                cout << "�������·���seqΪ" << seq << "�����ݰ���" << count << "�Σ����5��" << endl;
                if (count >= 5) {
                    cout << "���Դ�������5�Σ��˳�����" << endl;
                    return;
                }
                c = clock();
            }
            count = 0;
            // Ϊ�˱���CPUռ���ʹ��ߣ�����ӳ�
            Sleep(2);
        }
        seq++;
    }
    end = clock();
    cout << "�ɹ������ļ���" << endl;

    double TotalTime = (double)(end - start) / CLOCKS_PER_SEC;
    cout << "������ʱ��: " << TotalTime << "s" << endl;
    cout << "������: " << (double)dataAmount / TotalTime << " bytes/s" << endl << endl;

    // �ر��ļ���׼��������һ���ļ�
    in.close();
    in.clear();
}


int main() {
    WSAData wsd;

    // ��ʼ��Socket�⣬Э��ʹ�õ�Socket�汾
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        cout << "��ʼ��Socket��ʧ�ܣ�" << endl;
        return 1;
    }
    cout << "��ʼ��Socket��ɹ���" << endl;

    // �����ͻ���socket
    socketClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketClient == INVALID_SOCKET) {
        cout << "�����ͻ���socketʧ�ܣ�" << endl;
        WSACleanup(); // �ͷ�socket����Դ
        return 1;
    }
    cout << "�����ͻ���socket�ɹ���" << endl;

    // ���ͻ��˰󶨵�ַ�Ͷ˿�
    clientAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &clientAddr.sin_addr.s_addr); // �ͻ��˵�IP��ַ
    clientAddr.sin_port = htons(CLIENT_PORT); // �ͻ��˵Ķ˿ں�

    // ��ROUTER�󶨵�ַ�Ͷ˿�
    serverAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr.s_addr); // ROUTER��IP��ַ
    serverAddr.sin_port = htons(ROUTER_PORT); // ROUTER�Ķ˿ں�

    if (bind(socketClient, (LPSOCKADDR)&clientAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
        cout << "�󶨿ͻ��˵�ַ�Ͷ˿�ʧ�ܣ�" << endl;
        closesocket(socketClient); // �رտͻ���
        WSACleanup(); // �ͷ�socket����Դ
        return -1;
    }
    cout << "�󶨿ͻ��˵�ַ�Ͷ˿ڳɹ���" << endl;
    cout << "-----------------------------------------------" << endl;

    cout << "�ͻ��˵ȴ�����" << endl;
    if (waitConnect()) {
        cout << "���ӳɹ���" << endl;
    }
    else {
        cout << "����ʧ�ܣ�" << endl;
        return 1;
    }
    cout << "-----------------------------------------------" << endl;
    cout << "���롰quit���ر����ӣ�" << endl;

    while (!quit) {
        send_file();
    }

    closesocket(socketClient);
    WSACleanup();
    system("pause");
    return 0;
}