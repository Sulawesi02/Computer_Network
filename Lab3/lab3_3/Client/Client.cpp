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
#define TIMEOUT 10000 //��ʱ�ش�ʱ��

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

SOCKADDR_IN routerAddr, clientAddr;
SOCKET socketClient;
int len = sizeof(SOCKADDR);
bool quit = false;
int rwnd = 20; // ���մ���
int base = 1; // ���ڵ���ʼ���к�
int next_seq = 1; // ��һ�������͵����к�
mutex seq_mutex; // ���кŵĻ�����
bool send_over = false; // �������
map<int, Message> send_buffer; // ���кż��䱨�ĵ�ӳ��
map<int, clock_t> send_times;  // ���кż��䷢��ʱ���ӳ��

int cwnd = 1; // ӵ������
int ssthresh = 16; // ��������ֵ
int dup_ack_count = 0; // ���� ACK ������

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

    // �����ڷ�������
    while (!send_over) {
        {
            unique_lock<mutex> lock(seq_mutex);// ����
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

                // �������ݰ�
                sendMsg.seq = next_seq;
                sendMsg.setChecksum();
                if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                    cout << "�������ݰ�ʧ��!" << endl;
                }
                // ��¼����ʱ��
                send_buffer[next_seq] = sendMsg;
                send_times[next_seq] = clock();

                cout << "����seq:" << next_seq << endl;
                cout << "checksum:" << sendMsg.checksum << endl;
                cout << "ssthresh:" << ssthresh << endl;
                cout << "swnd:" << min(cwnd, rwnd) << endl;
                cout << "base:" << base << endl;

                next_seq++;

                cout << "next_seq:" << next_seq << endl;
                cout << "top:" << base + min(cwnd, rwnd) << endl << endl;
            }
            lock.unlock(); // ����
        }
        {
            unique_lock<mutex> lock(seq_mutex);// ����
            // ��ʱ�ش�
            for (auto it = send_buffer.begin(); it != send_buffer.end(); it++) {
                if (clock() - send_times[it->first] > TIMEOUT) {
                    cout << "Ӧ��ʱ�����·����ѷ���δȷ�ϵ����ݰ�" << endl;

                    Message sendMsg = it->second;
                    if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                        cout << "�ش����ݰ�ʧ��!" << endl;
                    }
                    // ���ü�ʱ��
                    send_times[it->first] = clock();

                    cout << "����seq:" << it->first << endl;
                    cout << "checksum��" << sendMsg.checksum << endl;
                    cout << "ssthresh:" << ssthresh << endl;
                    cout << "swnd:" << min(cwnd, rwnd) << endl;
                    cout << "base��" << base << endl;
                    cout << "next_seq��" << next_seq << endl;
                    cout << "top��" << base + min(cwnd, rwnd) << endl << endl;

                    cout << "���·������" << endl;
                }
            }
            lock.unlock(); // ����
        }

    }
}

// �����߳�
void recv_thread(SOCKET socketClient, int packetNum) {
    Message recvMsg;
    clock_t start;
    int expected_ack = 2; // �����յ���ack
    int prev_ack = 0; // ��һ���յ���ȷ�Ϻ�

    while (!send_over) {
        if (recvfrom(socketClient, (char*)&recvMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, &len) != SOCKET_ERROR) {
            {
                unique_lock<mutex> lock(seq_mutex);// ����
                if (recvMsg.isACK() && !recvMsg.packetIncorrection()) {
                    if (recvMsg.ack > prev_ack) { // �µ� ack
                        cout << "�����µ�ack:" << recvMsg.ack << endl;
                        dup_ack_count = 0; // �������� ACK ����
                        if (recvMsg.ack >= expected_ack) {
                            // �Ƴ���ȷ�ϵ����ݰ�
                            auto it = send_buffer.begin();
                            while (it != send_buffer.end() && it->first < recvMsg.ack) {
                                it = send_buffer.erase(it); // �ӷ��ͻ�������ɾȥ��Ӧ����
                                send_times.erase(recvMsg.ack); // ͬʱ�Ƴ���ʱ��
                            }
                            base = recvMsg.ack;

                            if (cwnd < ssthresh) {
                                // ������
                                cout << "������:" << endl;
                                cwnd *= 2;
                            }
                            else {
                                // ӵ������
                                cout << "ӵ������:" << endl;
                                cwnd += 1;
                            }
                            expected_ack += min(cwnd, rwnd);
                        }
                        prev_ack = recvMsg.ack;

                        // չʾ�������
                        cout << "ssthresh:" << ssthresh << endl;
                        cout << "swnd:" << min(cwnd, rwnd) << endl;
                        cout << "base:" << base << endl;
                        cout << "next_seq:" << next_seq << endl;
                        cout << "top:" << base + min(cwnd, rwnd) << endl << endl;

                    }
                    else if (recvMsg.ack == prev_ack) { //���� ack
                        if (dup_ack_count < 3) {
                            dup_ack_count++;
                            cout << "���յ���" << dup_ack_count << "������ack:" << recvMsg.ack << endl;
                            cwnd += 1;
                        }
                        if (dup_ack_count == 3) { // �յ���3������ ACK
                            // ���ش�
                            cout << "���ش�:" << endl;
                            Message sendMsg = send_buffer.find(prev_ack)->second;
                            if (sendto(socketClient, (char*)&sendMsg, BUFFER, 0, (SOCKADDR*)&routerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
                                cout << "�ش����ݰ�ʧ��!" << endl;
                            }
                            cout << "����seq:" << sendMsg.seq << endl;
                            cout << "checksum:" << sendMsg.checksum << endl;
                            cout << "ssthresh:" << ssthresh << endl << endl;

                            // ��ָ�
                            cout << "��ָ�:" << endl;
                            ssthresh = cwnd / 2;
                            cwnd = ssthresh + 3;

                            // չʾ�������
                            cout << "ssthresh:" << ssthresh << endl;
                            cout << "swnd:" << min(cwnd, rwnd) << endl;
                            cout << "base:" << base << endl;
                            cout << "next_seq:" << next_seq << endl;
                            cout << "top:" << base + min(cwnd, rwnd) << endl << endl;

                            dup_ack_count = INT_MAX;
                        }
                    }
                    // ��������ļ��������
                    if (recvMsg.ack == packetNum + 1) {
                        cout << "�ļ�������ɣ�" << endl;
                        send_over = true;
                    }
                }
                lock.unlock(); // ����
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

    cout << "������Ҫ���͵��ļ���:";
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
    cout << "checksum:" << sendMsg.checksum << endl;
    cout << "base:" << base << endl;
    cout << "next_seq:" << next_seq << endl;
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
            cout << "checksum:" << sendMsg.checksum << endl << endl;
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
    send_over = false; // �������
    cwnd = 1; // ӵ������
    ssthresh = 16; // ��������ֵ
    dup_ack_count = 0; // ���� ACK ������
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