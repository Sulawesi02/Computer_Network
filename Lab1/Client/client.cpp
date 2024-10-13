#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include<string>
#include <thread>
#include <windows.h> // ���ڿ���̨������

#pragma comment(lib, "ws2_32.lib") // socket��

#define PORT 3410  // �����˿�
#define BUF_SIZE 1024  // ��������С

using namespace std;

string userInput; // ���浱ǰ�û����������

// ���������ͨ��
void handleServer(SOCKET serverSocket, int clientId) {
    char recvBuff[BUF_SIZE];

    while (true) {
        ZeroMemory(recvBuff, BUF_SIZE);
        int recvBytes = recv(serverSocket, recvBuff, BUF_SIZE, 0);
        if (recvBytes <= 0) {
            cout << "��������Ͽ�����" << endl;
            break;
        }

        // ���浱ǰ���λ��
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        COORD cursorPos = csbi.dwCursorPosition;  // ��ȡ��ǰ���λ��

        // ��ȡ��ʾ�ʵ����֮������ݲ��洢�� userInput ��
        string prompt = "�û�[" + to_string(clientId) + "]: ";// ��ʾ��
        COORD promptStartPos = cursorPos;// �û��������ʼλ��(��ʾ��ĩβ)
        promptStartPos.X = prompt.length(); 
        int remainingChars = cursorPos.X - promptStartPos.X;// �û�����ĳ��ȣ�����ʾ��ĩβ�����֮����ַ�����
        char* inputBuffer = new char[remainingChars + 1]; // �洢�û����������
        DWORD charsRead;// ���ڴ洢ʵ�ʶ�ȡ���ַ���
        ReadConsoleOutputCharacterA(GetStdHandle(STD_OUTPUT_HANDLE), inputBuffer, remainingChars, promptStartPos, &charsRead);// ��ȡ����̨�ϵ��ַ����洢�� inputBuffer ��
        inputBuffer[remainingChars] = '\0';  // ȷ���� '\0' ��ֹ�ַ���
        userInput = inputBuffer; // ����ǰ��������ݴ��� userInput
        delete[] inputBuffer; // �ͷ��ڴ�

        // �����ǰ��
        COORD startPos = cursorPos;
        startPos.X = 0;
        DWORD charsWritten;
        FillConsoleOutputCharacter(GetStdHandle(STD_OUTPUT_HANDLE), ' ', cursorPos.X, startPos, &charsWritten);
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), startPos); // �ָ����λ��

        // ��ӡ���յ�����Ϣ
        string message = recvBuff;
        cout << message << endl;

        // ���»����û�����
        cout << "�û�[" << clientId << "]: " << userInput;
        cout.flush();
    }
}

int main() {
    WSAData wsd; // ��ʼ��WinSock��
    SOCKET socketClient;
    SOCKADDR_IN serverAddr;
    char sendBuff[BUF_SIZE];

    // ��ʼ��Socket DLL
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        cout << "��ʼ��Socket DLLʧ�ܣ�" << endl;
        return 1;
    }
    cout << "��ʼ��Socket DLL�ɹ���" << endl;
    cout << "===============================================" << endl;

    // �����ͻ���socket
    socketClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // ����IPv4��ַ�塢��ʽ�׽����Լ�TCPЭ�� 
    if (socketClient == INVALID_SOCKET) {
        cout << "�����ͻ���socketʧ�ܣ�" << endl;
        WSACleanup(); // �ͷ�socket����Դ
        return 1;
    }
    cout << "�����ͻ���socket�ɹ���" << endl;
    cout << "===============================================" << endl;

    // ���������socket������������
    serverAddr.sin_family = AF_INET; // IPv4��ַ��
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr.s_addr);
    serverAddr.sin_port = htons(PORT); // �������ļ����˿ں�
    int addrLen = sizeof(serverAddr);
    if (connect(socketClient, (LPSOCKADDR)&serverAddr, addrLen) == SOCKET_ERROR) {
        cout << "���ӷ�����ʧ�ܣ�" << endl;
        WSACleanup(); // �ͷ�socket����Դ
        return -1;
    }
    cout << "���ӷ������ɹ���" << endl;
    cout << "===============================================" << endl;

    // ���ղ�����clientId
    int clientId;
    recv(socketClient, (char*)&clientId, sizeof(clientId), 0);

    // �����߳������շ�������Ϣ
    thread recvThread(handleServer, socketClient, clientId);
    recvThread.detach();

    // �����û�����
    while (true) {
        ZeroMemory(sendBuff, BUF_SIZE);
        cout << "�û�[" << clientId << "]: ";
        cout.flush(); // ȷ�����������ʾ
        cin.getline(sendBuff, BUF_SIZE);

        if (send(socketClient, sendBuff, strlen(sendBuff), 0) == SOCKET_ERROR) {
            cout << "������Ϣʧ�ܣ�" << endl;
            closesocket(socketClient); // �رտͻ���
            WSACleanup(); // �ͷ�socket����Դ
            return -1;
        }

        // �����˳����������
        if (string(sendBuff) == "exit" || string(sendBuff) == "quit") {
            break;
        }
    }

    // �رտͻ���
    closesocket(socketClient);
    WSACleanup();
    return 0;
}
