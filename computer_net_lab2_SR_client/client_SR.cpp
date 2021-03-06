#define _WINSOCK_DEPRECATED_NO_WARNINGS
// SR_client.cpp : 定义控制台应用程序的入口点。
#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <stdio.h>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")

#define SERVER_PORT 12340 //接收数据的端口号
#define SERVER_IP "127.0.0.1" // 服务器的 IP 地址
#define INPUT_FILE "./input.txt"
#define OUTPUT_FILE "./output.txt"

using namespace std;

const int BUFFER_LENGTH = 1026;
const int SEQ_SIZE = 20;//接收端序列号个数，为 1~20
const int RECV_WIND_SIZE = 5;	//接收窗口大小为 10，SR中满足Wr + Ws <= N（Wr 为接收窗口大小，Ws 为发送窗口大小，N 为序列号个数）
char seqCache[RECV_WIND_SIZE][BUFFER_LENGTH];	//接收窗口缓存
bool ack[RECV_WIND_SIZE];		//ack发送情况，已发送时为true

//初始化套接字
bool initSocket(SOCKET& socketClient, SOCKADDR_IN& addrServer);
//-time 从服务器端获取当前时间
//-quit 退出客户端
//- testsr	[X][Y] 测试 SR 协议实现可靠数据传输
//			[X][0, 1] 模拟数据包丢失的概率
//			[Y][0, 1] 模拟 ACK 丢失的概率
void printTips();
//根据丢失率随机生成一个数字，判断是否丢失, 丢失则返回TRUE，否则返回 FALSE
BOOL lossInLossRatio(float lossRatio);

//SR实现
int main(int argc, char* argv[])
{
	SOCKET socketClient;
	SOCKADDR_IN addrServer;
	if (!initSocket(socketClient, addrServer))
	{
		printf("Failed to init Socket\n");
		return -1;
	}
	//接收缓冲区
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
	//为了测试与服务器的连接，可以使用 -time 命令从服务器端获得当前时间
	//使用 -testsr [X] [Y] 测试 SR 其中[X]表示数据包丢失概率
	//									 [Y]表示 ACK 丢包概率
	printTips();
	int ret;
	int interval = 1;//收到数据包之后返回 ack 的间隔，默认为 1 表示每个都返回 ack，0 或者负数均表示所有的都不返回 ack
	char cmd[128];
	memset(cmd, 0, 128 * sizeof(char));
	float packetLossRatio = 0.2; //默认包丢失率 0.2
	float ackLossRatio = 0.2; //默认 ACK 丢失率 0.2
	//用时间作为随机种子，放在循环的最外面
	srand((unsigned)time(NULL));
	while (true) {
		ZeroMemory(buffer, BUFFER_LENGTH);
		gets_s(buffer, BUFFER_LENGTH);
		memset(cmd, 0, 128 * sizeof(char));
		ret = sscanf_s(buffer, "%s %f%f", &cmd, 128, &packetLossRatio, &ackLossRatio);
		//开始 SR 测试，使用 SR 协议实现 UDP 可靠文件传输
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
			unsigned char u_code;//状态码
			unsigned short seq;//包的序列号
			unsigned short recvSeq;//接收窗口大小为 1，已确认的序列号
			unsigned short waitSeq;//等待的最小序列号
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
				//等待 server 回复设置 UDP 为阻塞模式
				recvSize = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
				switch (stage) {
				case 0://等待握手阶段
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
				case 1://等待接收数据阶段
					seq = (unsigned short)buffer[0];
					//随机法模拟包是否丢失
					b = lossInLossRatio(packetLossRatio);
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;                                                                                      
					}
					printf("Receive a packet with a seq of %d\n", seq);
					//如果接收到窗口范围内的包，接收并缓存，正常确认即可
					if ((seq - waitSeq) % SEQ_SIZE < RECV_WIND_SIZE) {
						memcpy(seqCache[seq % RECV_WIND_SIZE], &buffer[1], BUFFER_LENGTH);
						ack[seq % RECV_WIND_SIZE] = true;
						//输出数据
						if (buffer[1] != '\0' && buffer[1] != '\r') {
							//printf("Receive data:\n%s\n", &buffer[1]);
						}
						else{
							
							if (sendSuccessTime >= serverSize && buffer[1] == '\r')	//\r代表发送结束
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
							//双向传输
							memcpy(&buffer[1], data + 1024 * (totalSeq + seq - waitSeq), 1024);
						}
						while (ack[waitSeq % RECV_WIND_SIZE]
							&& (waitSeq - start) % SEQ_SIZE < RECV_WIND_SIZE)
						{
							//缓存交付
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
					//如果接收到确认过的包，确认即可
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
							//双向传输
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
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
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