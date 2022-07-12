#include <stdlib.h>
#include <time.h>
#include <WinSock2.h>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")
#define SERVER_PORT 12340 //端口号
#define SERVER_IP "0.0.0.0" //IP 地址
#define INPUT_FILE "./input.txt"
#define OUTPUT_FILE "./output.txt"

using namespace std;

const int BUFFER_LENGTH = 1026;	//缓冲区大小，（以太网中 UDP 的数据帧中包长度应小于 1480 字节）
const int SEND_WIND_SIZE = 5;	//发送窗口大小为 10，sr 中应满足 W + 1 <= N（W 为发送窗口大小，N 为序列号个数）
//SR协议中，接收窗口大小不应大于发送窗口大小
//本例取序列号 0...19 共 20 个
//如果将窗口大小设为 1，则为停-等协议
const int SEQ_SIZE = 20;	//序列号的个数，从 0~19 共计 20 个
//由于发送数据第一个字节如果值为 0，则数据会发送失败
//因此接收端序列号为 1~20，与发送端一一对应

int ack[SEQ_SIZE];	//收到 ack 情况，对应 0~19 的 ack，0 代表不可用，1 代表可用，2代表当前被使用
int ackTime[SEQ_SIZE];
int curSeq;			//当前数据包的 seq
int curAck;			//当前等待确认的 ack
int totalSeq;		//收到的包的总数
int totalPacket;	//需要发送的包总数
int sendSuccessTime = 0;
ofstream icout;
char ackCache[SEQ_SIZE][BUFFER_LENGTH];	//ack数据缓存
int totalSeqList[SEQ_SIZE];

//初始化套接字
bool initSocket(SOCKET& sockServer);	
//获取当前系统时间，结果存入 ptime 中
void getCurTime(char* ptime);		
//检查当前序列号 curSeq 是否可用
bool seqIsAvailable();			
//超时重传处理函数，滑动窗口内的数据帧都要重传
void timeoutHandler(int ackNum);
//收到 ack，累积确认，取数据帧的第一个字节
void ackHandler(char c);				
//由于发送数据时，第一个字节（序列号）为 0（ASCII）时发送失败，因此加一了，此处需要减一还原

//SR实现
int main(char** argv, int argc)
{
	SOCKET sockServer;
	SOCKADDR_IN addrClient; //客户端地址
	int length = sizeof(SOCKADDR);
	char* buffer = (char*)calloc(BUFFER_LENGTH,sizeof(char*)); //数据发送接收缓冲区
	if (buffer == NULL) return -1;
	printf("Server Launching\n");
	printf("Initializing...\n");
	if (!initSocket(sockServer))
	{
		printf("Failed to init Socket\n");
		free(buffer);
		return -1;
	}
	else printf("Socket Init Success, Listener Port: %d\n", SERVER_PORT);
	ZeroMemory(buffer, sizeof(buffer));
	//将测试数据读入内存 
	ifstream icin;
	icin.open(INPUT_FILE, ios::binary | ios::in);
	char* data = (char*)calloc(1024 * 500, sizeof(char));
	if (data == NULL) return -1;
	ZeroMemory(data, sizeof(data));
	icin.read(data, 1024 * 500);
	icin.close();
	totalPacket = (strlen(data) + 1023) / 1024;
	int recvSize;
	while (true) {
		//非阻塞接收，若没有收到数据，返回值为-1
		recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
		if (recvSize < 0) {
			Sleep(200);
			continue;
		}
		printf("receive from client: %s\n", buffer);
		if (strcmp(buffer, "-time") == 0) 
		{
			getCurTime(buffer);
			sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
			Sleep(500);
		}
		else if (strcmp(buffer, "-quit") == 0)
		{
			strcpy_s(buffer, strlen("Good bye!") + 1, "Good bye!");
			sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
			Sleep(500);
			break;
		}
		else if (strcmp(buffer, "-testsr") == 0)
		{
			//进入 sr 测试阶段
			//首先 server（server 处于 0 状态）向 client 发送 205 状态码（server进入 1 状态）
			//server 等待 client 回复 200 状态码，如果收到（server 进入 2 状态），则开始传输文件，否则延时等待直至超时
			//在文件传输阶段，server 发送窗口大小设为10
			ZeroMemory(buffer, sizeof(buffer));
			int recvSize;
			int waitCount = 0;
			//加入了一个握手阶段
			//首先服务器向客户端发送一个 205 大小的状态码（我自己定义的）表示服务器准备好了，可以发送数据
			//客户端收到 205 之后回复一个 200 大小的状态码，表示客户端准备好了，可以接收数据了
			//服务器收到 200 状态码之后，就开始使用 sr 发送数据了
			int stage = 0;
			int clientSize = 0;
			bool runFlag = true;
			bool endFlag = false;
			bool sendOver = false;
			bool recvOver = false;
			for (int i = 0; i < SEQ_SIZE; ++i) {
				ack[i] = 1;
				ackTime[i] = 0;
				totalSeqList[i] = -1;
			}
			icout.open(OUTPUT_FILE, ios::binary | ios::out);
			printf("Begain to test SR protocol,please don't abort the process\n");
			printf("Shake hands stage\n");
			while (runFlag) {
				switch (stage) {
				case 0://发送 205 阶段
					buffer[0] = 205;
					buffer[1] = totalPacket;
					sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
					Sleep(100);
					stage = 1;
					break;
				case 1://等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
					if (recvSize < 0)
					{
						++waitCount;
						if (waitCount > 20) {
							printf("Timeout error\n");
							break;
						}
						Sleep(500);
						continue;
					}
					else
					{
						if ((unsigned char)buffer[0] == 200) {
							printf("Shake hands success\n");
							printf("Begin a file transfer\n");
							printf("File size is %dB, each packet is 1024B and packet total num is % d\n", strlen(data), totalPacket);
							clientSize = (unsigned char)buffer[1];
							curSeq = 0;
							curAck = 0;
							totalSeq = 0;
							waitCount = 0;
							sendOver = false;
							recvOver = false;
							endFlag = false;
							sendSuccessTime = 0;
							stage = 2;
						}
					}
					break;
				case 2://数据传输阶段
					if (seqIsAvailable() && (!sendOver || !recvOver || !endFlag)) {
						//发送给客户端的序列号从 1 开始
						buffer[0] = curSeq + 1;
						if (totalSeqList[curSeq] == -1) {
							totalSeqList[curSeq] = totalSeq;
							totalSeq++;
						}	
						ack[curSeq] = 2;
						//数据发送的过程中应该判断是否传输完成
						if (!endFlag && sendOver && recvOver)
						{
							printf("End request\n");
							endFlag = true;
							buffer[1] = '\r';
						}
						else if (sendOver)
						{
							printf("Server data sending is over, receiving client data with a seq of %d\n", curSeq + 1);
							buffer[1] = '\0';
						}
						else
						{
							printf("send a packet with a seq of %d\n", curSeq + 1);
							memcpy(&buffer[1], data + 1024 * totalSeqList[curSeq], 1024);
						}
						sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						while(ack[curSeq] == 0 || ack[curSeq] == 2){
							++curSeq;
							curSeq %= SEQ_SIZE;
						}
						Sleep(50);
					}
					//等待 Ack，若没有收到，则返回值为-1，计数器+1
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, &length);
					if (recvSize < 0) {
						//10 次等待 ack 则超时重传
						for (int i = 0; i < SEND_WIND_SIZE; i++)
						{
							if (ack[(curAck + i) % SEQ_SIZE] == 2)
							{
								ackTime[(curAck + i) % SEQ_SIZE]++;
								if (ackTime[(curAck + i) % SEQ_SIZE] >= 10) {
									timeoutHandler((curAck + i) % SEQ_SIZE);
								}
							}
						}
					}
					else {
						//收到 ack
						printf("Receive a ack of %d\n", buffer[0]);
						if (ack[buffer[0] - 1] == 2) {
							if (buffer[1] != '\0'){
								//printf("Data from client：\n%s\n", &buffer[1]);
								if (buffer[0] - 1 == curAck)
									icout << &buffer[1];
								else memcpy(ackCache[buffer[0] - 1], buffer + 1, BUFFER_LENGTH);
							}
							else if (buffer[1] == '\0' && !recvOver) {
								printf("Client data sending is over\n");
								memset(ackCache[buffer[0] - 1], 0, BUFFER_LENGTH);
							}
						}
						ackHandler(buffer[0]); 
						if (sendSuccessTime >= totalPacket)
							sendOver = true;
						if (sendSuccessTime >= clientSize)
							recvOver = true;
					}
					if (sendOver && recvOver && endFlag) {
						//结束确认
						sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						printf("Data trans is over\n");
						runFlag = false;
					}
					Sleep(50);
					break;
				}
			}
			icout.close();
		}
	}
	//关闭套接字，卸载库
	closesocket(sockServer);
	WSACleanup();
	free(data);
	free(buffer);
	return 0;
}


bool initSocket(SOCKET& sockServer)
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
	sockServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//设置套接字为非阻塞模式
	int iMode = 1; //1：非阻塞，0：阻塞
	ioctlsocket(sockServer, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置
	SOCKADDR_IN addrServer; //服务器地址
	//addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//两者均可
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	err = bind(sockServer, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
	if (err) {
		err = GetLastError();
		printf("Could not bind the port %d for socket.Error code is % d\n", SERVER_PORT, err);
			WSACleanup();
		return false;
	}
	return true;
}

void getCurTime(char* ptime) 
{
	char buffer[128];
	memset(buffer, 0, 128 * sizeof(char));
	time_t c_time;
	struct tm* p = (struct tm*)calloc(sizeof(struct tm),1);
	if (p == NULL) return;
	time(&c_time);
	localtime_s(p, &c_time);
	sprintf_s(buffer, "%d/%d/%d %d:%d:%d",
		p->tm_year + 1900,
		p->tm_mon,
		p->tm_mday,
		p->tm_hour,
		p->tm_min,
		p->tm_sec);
	strcpy_s(ptime, 128 * sizeof(char), buffer);
	free(p);
}

bool seqIsAvailable() 
{
	int step;
	step = curSeq - curAck;
	//printf("Step is %d\n", step);
	step = step >= 0 ? step : step + SEQ_SIZE;
	//序列号是否在当前发送窗口之内
	if (step >= SEND_WIND_SIZE) 
		return false;
	if (ack[curSeq] == 1) 
		return true;
	return false;
}

void timeoutHandler(int ackNum) 
{
	printf("ACK %d timer out error.\n", ackNum + 1);
	ack[ackNum] = 1;
	ackTime[ackNum] = 0;
	curSeq = ackNum;
}

void ackHandler(char c) {
	unsigned char index = (unsigned char)c - 1; //序列号减一
	ack[index] = 0;
	totalSeqList[index] = -1;
	while (ack[curAck] == 0) {
		ack[curAck] = 1;
		if (curAck != index && ackCache[curAck][0] != '\0') {
			icout << ackCache[curAck];
			memset(ackCache[curAck], 0, BUFFER_LENGTH);
		}
		curAck++;
		sendSuccessTime++;
		if (curAck == SEQ_SIZE)
			curAck = 0;
	}
	//if (curAck <= index) {
	//	for (int i = curAck; i <= index; ++i) {
	//		ack[i] = TRUE;
	//	}
	//	curAck = (index + 1) % SEQ_SIZE;
	//}
	//else {
	//	//ack 超过了最大值，回到了 curAck 的左边
	//	for (int i = curAck; i < SEQ_SIZE; ++i) {
	//		ack[i] = TRUE;
	//	}
	//	for (int i = 0; i <= index; ++i) {
	//		ack[i] = TRUE;
	//	}
	//	curAck = index + 1;
	//}
}

