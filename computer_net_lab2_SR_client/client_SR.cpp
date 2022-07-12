#define _WINSOCK_DEPRECATED_NO_WARNINGS
// SR_client.cpp : �������̨Ӧ�ó������ڵ㡣
#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <stdio.h>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")

#define SERVER_PORT 12340 //�������ݵĶ˿ں�
#define SERVER_IP "127.0.0.1" // �������� IP ��ַ
#define INPUT_FILE "./input.txt"
#define OUTPUT_FILE "./output.txt"

using namespace std;

const int BUFFER_LENGTH = 1026;
const int SEQ_SIZE = 20;//���ն����кŸ�����Ϊ 1~20
const int RECV_WIND_SIZE = 5;	//���մ��ڴ�СΪ 10��SR������Wr + Ws <= N��Wr Ϊ���մ��ڴ�С��Ws Ϊ���ʹ��ڴ�С��N Ϊ���кŸ�����
char seqCache[RECV_WIND_SIZE][BUFFER_LENGTH];	//���մ��ڻ���
bool ack[RECV_WIND_SIZE];		//ack����������ѷ���ʱΪtrue

//��ʼ���׽���
bool initSocket(SOCKET& socketClient, SOCKADDR_IN& addrServer);
//-time �ӷ������˻�ȡ��ǰʱ��
//-quit �˳��ͻ���
//- testsr	[X][Y] ���� SR Э��ʵ�ֿɿ����ݴ���
//			[X][0, 1] ģ�����ݰ���ʧ�ĸ���
//			[Y][0, 1] ģ�� ACK ��ʧ�ĸ���
void printTips();
//���ݶ�ʧ���������һ�����֣��ж��Ƿ�ʧ, ��ʧ�򷵻�TRUE�����򷵻� FALSE
BOOL lossInLossRatio(float lossRatio);

//SRʵ��
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
	ZeroMemory(buffer, BUFFER_LENGTH);
	int len = sizeof(SOCKADDR);
	//Ϊ�˲���������������ӣ�����ʹ�� -time ����ӷ������˻�õ�ǰʱ��
	//ʹ�� -testsr [X] [Y] ���� SR ����[X]��ʾ���ݰ���ʧ����
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
		ZeroMemory(buffer, BUFFER_LENGTH);
		gets_s(buffer, BUFFER_LENGTH);
		memset(cmd, 0, 128 * sizeof(char));
		ret = sscanf_s(buffer, "%s %f%f", &cmd, 128, &packetLossRatio, &ackLossRatio);
		//��ʼ SR ���ԣ�ʹ�� SR Э��ʵ�� UDP �ɿ��ļ�����
		if (!strcmp(cmd, "-testsr")) {
			printf("%s\n", "Begin to test SR protocol, please don't abort the process");
			printf("The loss ratio of packet is %.2f,the loss ratio of ack is % .2f\n", packetLossRatio, ackLossRatio);
			printf("File size is %dB, each packet is 1024B and packet total num is % d\n", strlen(data), totalPacket);
			int waitCount = 0;
			int stage = 0;
			int totalSeq = 0;
			int recvSize;
			int serverSize = 0;
			int sendSuccessTime = 0;
			int timeCounter = 0;
			bool b;
			bool recvOver = false;
			bool sendOver = false;
			bool endflag = false;
			unsigned char u_code;//״̬��
			unsigned short seq;//�������к�
			unsigned short recvSeq;//���մ��ڴ�СΪ 1����ȷ�ϵ����к�
			unsigned short waitSeq;//�ȴ�����С���к�
			for (int i = 0; i < RECV_WIND_SIZE; i++)
				ZeroMemory(seqCache[i],BUFFER_LENGTH);
			sendto(socketClient, "-testsr", strlen("-testsr") + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
			ZeroMemory(buffer, BUFFER_LENGTH);
			while (true)
			{
				if (recvOver && sendOver) {
					printf("Data trans is over\n");
					icout.close();
					break;
				}
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
						sendSuccessTime = 0;
						icout.open(OUTPUT_FILE, ios::binary | ios::out);
						for (int i = 0; i < RECV_WIND_SIZE; i++)
						{
							ack[i] = false;
						}
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
					//������յ����ڷ�Χ�ڵİ������ղ����棬����ȷ�ϼ���
					if ((seq - waitSeq) % SEQ_SIZE < RECV_WIND_SIZE) {
						memcpy(seqCache[seq % RECV_WIND_SIZE], &buffer[1], BUFFER_LENGTH);
						ack[seq % RECV_WIND_SIZE] = true;
						//�������
						if (buffer[1] != '\0' && buffer[1] != '\r') {
							//printf("Receive data:\n%s\n", &buffer[1]);
						}
						else{
							
							if (sendSuccessTime >= serverSize && buffer[1] == '\r')	//\r�������ͽ���
							{
								printf("Server data sending is over\n");
								recvOver = true;
							}
						}
						int start = waitSeq;
						memset(buffer, 0, BUFFER_LENGTH);
						buffer[0] = seq;
						recvSeq = seq;
						if (totalSeq >= totalPacket)
						{
							if (totalSeq == totalPacket)
								printf("Client data sending is over\n");
							buffer[1] = '\0';
							sendOver = true;
						}
						else 
						{
							//˫����
							memcpy(&buffer[1], data + 1024 * (totalSeq + seq - waitSeq), 1024);
						}
						while (ack[waitSeq % RECV_WIND_SIZE]
							&& (waitSeq - start) % SEQ_SIZE < RECV_WIND_SIZE)
						{
							//���潻��
							ack[waitSeq % RECV_WIND_SIZE] = false;
							sendSuccessTime++;
							icout << seqCache[waitSeq % RECV_WIND_SIZE];
							ZeroMemory(seqCache[waitSeq % RECV_WIND_SIZE], BUFFER_LENGTH);
							waitSeq++;
							++totalSeq;
							if (waitSeq == 21) {
								waitSeq = 1;
							}
						}
						//if (sendSuccessTime == serverSize) recvOver = true;
						//seqFlag[seq - 1] = true;
					}
					//������յ�ȷ�Ϲ��İ���ȷ�ϼ���
					else if ((seq - waitSeq) % SEQ_SIZE >= RECV_WIND_SIZE && (waitSeq - seq) % SEQ_SIZE < SEQ_SIZE / 2) {
						buffer[0] = seq;
						if (totalSeq >= totalPacket)
						{
							printf("Client data sending is over\n");
							buffer[1] = '\0';
							sendOver = true;
						}
						else
						{
							//˫����
							memcpy(&buffer[1], data + 1024 * (totalSeq + seq - waitSeq), 1024);
						}
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
	printf("| -testsr [X] [Y] to test the sr |\n");
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