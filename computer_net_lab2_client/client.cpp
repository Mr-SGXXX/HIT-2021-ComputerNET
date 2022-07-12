#define _WINSOCK_DEPRECATED_NO_WARNINGS
// GBN_client.cpp : �������̨Ӧ�ó������ڵ㡣
#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <stdio.h>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")

#define SERVER_PORT 12340 //�������ݵĶ˿ں�
#define SERVER_IP "127.0.0.1"  // �������� IP ��ַ
#define INPUT_FILE "./input.txt"
#define OUTPUT_FILE "./output.txt"

using namespace std;

const int BUFFER_LENGTH = 1026;
const int SEQ_SIZE = 20;//���ն����кŸ�����Ϊ 1~20

//��ʼ���׽���
bool initSocket(SOCKET& socketClient, SOCKADDR_IN& addrServer);
//-time �ӷ������˻�ȡ��ǰʱ��
//-quit �˳��ͻ���
//- testgbn	[X][Y] ���� GBN Э��ʵ�ֿɿ����ݴ���
//			[X][0, 1] ģ�����ݰ���ʧ�ĸ���
//			[Y][0, 1] ģ�� ACK ��ʧ�ĸ���
void printTips();
//���ݶ�ʧ���������һ�����֣��ж��Ƿ�ʧ, ��ʧ�򷵻�TRUE�����򷵻� FALSE
BOOL lossInLossRatio(float lossRatio);

int main(int argc, char* argv[])
{
	SOCKET socketClient;
	SOCKADDR_IN addrServer;
	if (!initSocket(socketClient, addrServer))
	{
		printf("Failed to init Socket\n");
		return -1;
	}
	//���ջ�����
	char* buffer = (char*)calloc(BUFFER_LENGTH,sizeof(char));
	char* data = (char*)calloc(1024 * 113, sizeof(char*));
	if (data == NULL) return -1;
	if (buffer == NULL) return -1;
	ifstream icin;
	ofstream icout;
	icin.open(INPUT_FILE, ios::in || ios::binary);
	icin.read(data, 1024 * 113);
	icin.close(); 
	int totalPacket = (strlen(data) + 1023) / 1024;
	ZeroMemory(buffer, sizeof(buffer));
	int len = sizeof(SOCKADDR);
	//Ϊ�˲���������������ӣ�����ʹ�� -time ����ӷ������˻�õ�ǰʱ��
	//ʹ�� -testgbn [X] [Y] ���� GBN ����[X]��ʾ���ݰ���ʧ����
	//									 [Y]��ʾ ACK ��������
	printTips();
	int ret;
	int interval = 1;//�յ����ݰ�֮�󷵻� ack �ļ����Ĭ��Ϊ 1 ��ʾÿ�������� ack��0 ���߸�������ʾ���еĶ������� ack
	char cmd[128];
	memset(cmd, 0, 128 * sizeof(char));
	float packetLossRatio = 0.2; //Ĭ�ϰ���ʧ�� 0.2
	float ackLossRatio = 0.2; //Ĭ�� ACK ��ʧ�� 0.2
	//��ʱ����Ϊ������ӣ�����ѭ����������
	srand((unsigned)time(NULL));
	while (true) {
		gets_s(buffer, BUFFER_LENGTH);
		ret = sscanf_s(buffer, "%s %f%f", &cmd, 128, &packetLossRatio, &ackLossRatio);
		//��ʼ GBN ���ԣ�ʹ�� GBN Э��ʵ�� UDP �ɿ��ļ�����
		if (!strcmp(cmd, "-testgbn")) {
			printf("%s\n", "Begin to test GBN protocol, please don't abort the process");
			printf("The loss ratio of packet is %.2f,the loss ratio of ack is % .2f\n", packetLossRatio, ackLossRatio);
			printf("File size is %dB, each packet is 1024B and packet total num is % d\n", strlen(data), totalPacket);
			int waitCount = 0;
			int stage = 0;
			int totalSeq = 0;
			int recvSize;
			int serverSize = 0;
			int sendSuccessTime = 0;
			bool b;
			bool recvOver = false;
			bool sendOver = false;
			unsigned char u_code;//״̬��
			unsigned short seq;//�������к�
			unsigned short recvSeq;//���մ��ڴ�СΪ 1����ȷ�ϵ����к�
			unsigned short waitSeq;//�ȴ������к�

			sendto(socketClient, "-testgbn", strlen("-testgbn") + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
			while (true)
			{
				//�ȴ� server �ظ����� UDP Ϊ����ģʽ
				recvSize = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
				switch (stage) {
				case 0://�ȴ����ֽ׶�
					u_code = (unsigned char)buffer[0];
					serverSize = (unsigned char)buffer[1];
					if ((unsigned char)buffer[0] == 205)
					{
						printf("Ready for file transmission\n");
						buffer[0] = 200;
						buffer[1] = totalPacket;
						buffer[2] = '\0';
						sendto(socketClient, buffer, 2, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
						stage = 1;
						recvSeq = 0;
						waitSeq = 1;
						totalSeq = 0;
						recvOver = false;
						sendOver = false;
						icout.open(OUTPUT_FILE, ios::binary | ios::out);
					}
					break;
				case 1://�ȴ��������ݽ׶�
					seq = (unsigned short)buffer[0];
					//�����ģ����Ƿ�ʧ
					b = lossInLossRatio(packetLossRatio);
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;
					}
					printf("Receive a packet with a seq of %d\n", seq);
					//������ڴ��İ�����ȷ���գ�����ȷ�ϼ���
					if (waitSeq == seq) {
						++waitSeq;
						sendSuccessTime++;
						if (waitSeq == 21) {
							waitSeq = 1;
						}
						//�������
						if (buffer[1] != '\0') {
							//printf("Receive data:\n%s\n", &buffer[1]);
							icout << &buffer[1];
						}
						else {
							printf("Server data sending is over\n");
							recvOver = true;
						}
						if(sendSuccessTime == serverSize) recvOver = true;
						memset(buffer, 0, BUFFER_LENGTH);
						buffer[0] = seq;
						recvSeq = seq;
						if (totalSeq >= totalPacket)
						{
							printf("Client data sending is over\n");
							buffer[1] = '\0';
							sendOver = true;
						}
						else 
						{
							//˫����
							memcpy(&buffer[1], data + 1024 * totalSeq, 1024);
							++totalSeq;
						}
					}
					else
					{
						//�����ǰһ������û���յ�����ȴ� Seq Ϊ 1 �����ݰ��������򲻷��� ACK����Ϊ��û����һ����ȷ�� ACK��
						if (!recvSeq) continue;
						buffer[0] = recvSeq;
						memcpy(&buffer[1], data + 1024 * (totalSeq - 1), 1024);
					} 
					b = lossInLossRatio(ackLossRatio);
					if (b) {
						printf("The ack of %d loss\n", (unsigned char)buffer[0]);
						continue;
					}
					sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
					printf("send a ack of %d\n", (unsigned char)buffer[0]);
					break;
				}
				Sleep(50);
				if (recvOver && sendOver) {
					printf("Data trans is over\n");
					icout.close();
					break;
				}
			}
		}
		else
		{
			sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
			ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
			printf("%s\n", buffer);
			if (!strcmp(buffer, "Good bye!")) {
				break;
			}
		}
		getchar();
		printTips();
	}
	free(buffer);
	free(data);
	closesocket(socketClient);
	WSACleanup();
	return 0;
}

bool initSocket(SOCKET& socketClient, SOCKADDR_IN& addrServer)
{
	//�����׽��ֿ⣨���룩
	WORD wVersionRequested;
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
		printf("WSAStartup failed with error: %d\n", err);
		return false;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
		return false;
	}
	else {
		printf("The Winsock 2.2 dll was found okay\n");
	}
	socketClient = socket(AF_INET, SOCK_DGRAM, 0);
	addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	return true;
}

void printTips() {
	printf("*****************************************\n");
	printf("| -time to get current time |\n");
	printf("| -quit to exit client |\n");
	printf("| -testgbn [X] [Y] to test the gbn |\n");
	printf("*****************************************\n");
}

BOOL lossInLossRatio(float lossRatio) {
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}