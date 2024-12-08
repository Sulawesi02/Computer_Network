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
#define TIMEOUT 100 //��ʱ�ش�ʱ��
#define MAX_RETURN_TIMES 5 //��ʱ�ش�����


SOCKADDR_IN routerAddr, clientAddr;
SOCKET socketClient;
int len = sizeof(SOCKADDR);
bool quit = false;
const int cwnd = 5;
int base = 1; // ���ڵ���ʼ���к�
int next_seq = 1; // ��һ�������͵����к�
clock_t send_time = 0;  // ���ķ�����ʼʱ��
mutex seq_mutex; // ���кŵĻ�����
bool send_over = false; // �������

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
        // ��0У����ֶ�
        this->checksum = 0;

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

bool waitConnect() {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵�һ��������Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յڶ���������Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isSYN() && recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "�ͻ��˽��յ��ڶ���������Ϣ���ڶ������ֳɹ�!" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "��һ�����ֳ�ʱ,�ͻ������·��͵�һ��������Ϣ" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵�һ�λ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    // ���յڶ��λ�����Ϣ����ʱ�ش�
    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "�ͻ��˽��յ��ڶ��λ�����Ϣ���ڶ��λ��ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "��һ�λ��ֳ�ʱ,�ͻ������·��͵�һ�λ�����Ϣ" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "�ͻ��˽��յ������λ�����Ϣ�������λ��ֳɹ���" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "�ڶ��λ��ֳ�ʱ,�ͻ������·��͵ڶ��λ�����Ϣ" << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
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
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷��͵��Ĵλ�����Ϣʧ��!" << endl;
        cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
        return 0;
    }

    return 1;
}


// �����߳�
void send_thread(SOCKET socketClient, sockaddr_in& routerAddr, ifstream& in,int filePtrLoc, int packetNum) {
    Message sendMsg;
    int count = 0;

    // �����ڷ�������
    while (1) {
        if (send_over) {
            break;
        }
        if(next_seq <= packetNum && (next_seq - base) < cwnd){
            {
                cout << "�����߳�׼������" << endl;
                unique_lock<mutex> lock(seq_mutex);// ����
                cout << "�����̼߳����ɹ�" << endl;
                if (next_seq == packetNum) {
                    in.read(sendMsg.data, filePtrLoc);
                    sendMsg.len = filePtrLoc;
                    sendMsg.setEND(); // �ļ�������־
                    filePtrLoc = 0;
                }
                else {
                    in.read(sendMsg.data, 1024);// ��ȡ�ļ�����
                    sendMsg.len = 1024;
                    filePtrLoc -= 1024;
                }

                // �������ݰ�
                sendMsg.seq = next_seq;
                sendMsg.setChecksum();
                cout << "����seq:" << next_seq << endl;
                cout << "base��" << base << endl;
                cout << "next_seq��" << next_seq << endl;
                cout << "len:" << sendMsg.len << endl;
                cout << "checksum��" << sendMsg.checksum << endl << endl;

                if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "�������ݰ�ʧ��!" << endl;
                }

                if (base == next_seq) {
                    send_time = clock();
                }
                next_seq++;
                cout << "�����߳�׼������" << endl;
            }
            cout << "�����߳̽����ɹ�" << endl;

        }
        // ��ʱ�ش�
        if(clock() - send_time > TIMEOUT){
            {
                cout << "�����߳�׼������" << endl;
                unique_lock<mutex> lock(seq_mutex);// ����
                cout << "�����̼߳����ɹ�" << endl;
                cout << "Ӧ��ʱ�����·���δȷ�ϵ����ݰ�" << endl;

                for (int i = base; i < next_seq; i++) {
                    in.seekg((i - 1) * 1024, ios::beg);// �����ļ�ָ�뵽��ȷ��λ��
                    if (i == packetNum) {
                        in.read(sendMsg.data, filePtrLoc);
                        sendMsg.len = filePtrLoc;
                        sendMsg.setEND(); // �ļ�������־
                    }
                    else {
                        in.read(sendMsg.data, 1024);// ��ȡ�ļ�����
                        sendMsg.len = 1024;
                    }

                    // �������ݰ�
                    sendMsg.seq = i;
                    sendMsg.setChecksum();
                    cout << "����seq:" << i << endl;
                    cout << "base��" << base << endl;
                    cout << "next_seq��" << i << endl;
                    cout << "len:" << sendMsg.len << endl;
                    cout << "checksum��" << sendMsg.checksum << endl << endl;
                    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                        cout << "�������ݰ�ʧ��!" << endl;
                    }
                    cout << "�����߳�׼������" << endl;
                }
                cout << "�����߳̽����ɹ�" << endl;
                send_time = clock();
            }
        }
    }
}

// �����߳�
void recv_thread(SOCKET socketClient, int packetNum) {
    Message recvMsg;
    clock_t start;

    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            cout << "�����߳�׼������" << endl;
            unique_lock<mutex> lock(seq_mutex);// ����
            cout << "�����̼߳����ɹ�" << endl;
            {
                if (recvMsg.isACK() && !recvMsg.packetIncorrection()) {
                    cout << "����ack��" << recvMsg.ack << endl;
                    cout << "base��" << base << endl;

                    if (recvMsg.ack <= base + cwnd) {
                        base = recvMsg.ack;
                        if (recvMsg.ack % cwnd == 1) {
                            cout << "���³�ʱʱ�䣡" << endl;
                            send_time = clock();
                        }
                        else {
                            cout << endl << "���������������³�ʱʱ�䣡" << endl << endl;
                        }
                    }
                    // չʾ�������
                    cout << "base=����ack=" << base << endl;
                    cout << "next_seq��" << next_seq << endl;
                    cout << "�������ѷ��͵�δ�յ�ack�İ���" << next_seq - base << endl;
                    cout << "������δ���͵İ���" << cwnd - (next_seq - base) << endl << endl;
                    // ��������ļ��������
                    if (recvMsg.ack == packetNum + 1) {
                        cout << "�ļ�������ɣ�" << endl;
                        send_over = true;
                        break;
                    }
                }
                cout << "�����߳�׼������" << endl;
            }
            cout << "�����߳̽����ɹ�" << endl;
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
    cout << "����seq:" << sendMsg.seq << endl;
    cout << "base��" << base << endl;
    cout << "next_seq��" << next_seq << endl;
    cout << "len:" << sendMsg.len << endl;
    cout << "checksum��" << sendMsg.checksum << endl;
    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "�ͻ��˷����ļ���ʧ��!" << endl;
        return;
    }

    start = clock();
    while (1) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            if (recvMsg.isACK() && recvMsg.ack == sendMsg.seq + 1 && !recvMsg.packetIncorrection()) {
                cout << "�ͻ��˷����ļ����ɹ�!" << endl;
                break;
            }
        }
        if (clock() - start > TIMEOUT) {
            cout << "Ӧ��ʱ���ͻ������·����ļ���" << endl;
            cout << "checksum��" << sendMsg.checksum << endl << endl;
            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                cout << "�ͻ��˷����ļ���ʧ��!" << endl;
                cout << "��ǰ����״̬���ѣ����Ժ�����" << endl;
                return;
            }
            start = clock();
        }
    }
    
    // ��ʼ�����ļ�����
    cout << "�ͻ��˿�ʼ�����ļ����ݣ�" << endl << endl;
    start = clock();

    // �������ͺͽ����߳�
    thread sender(send_thread, socketClient, ref(routerAddr), ref(in), filePtrLoc, packetNum);
    thread receiver(recv_thread, socketClient, packetNum);

    sender.join(); // �ȴ������߳����
    receiver.join(); // �ȴ������߳����

    end = clock();
    cout << "�ɹ������ļ���" << endl;

    double TotalTime = (double)(end - start) / CLOCKS_PER_SEC;
    cout << "������ʱ��: " << TotalTime << "s" << endl;
    cout << "������: " << (double)dataAmount / TotalTime << " bytes/s" << endl << endl;

    // �ر��ļ���׼��������һ���ļ�
    in.close();
    in.clear();

    base = 1; // ���ڵ���ʼ���к�
    next_seq = 1; // ��һ�������͵����к�
    send_time = 0;  // ���ķ�����ʼʱ��
    send_over = false; // �������
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
    routerAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &routerAddr.sin_addr.s_addr); // ROUTER��IP��ַ
    routerAddr.sin_port = htons(ROUTER_PORT); // ROUTER�Ķ˿ں�

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