#include <stdlib.h>
#include <time.h>
#include <WinSock2.h>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")
#define SERVER_PORT 12340 //�˿ں�
#define SERVER_IP "0.0.0.0" //IP ��ַ
#define INPUT_FILE "./input.txt"
#define OUTPUT_FILE "./output.txt"

using namespace std;

const int BUFFER_LENGTH = 1026;	//��������С������̫���� UDP ������֡�а�����ӦС�� 1480 �ֽڣ�
const int SEND_WIND_SIZE = 5;	//���ʹ��ڴ�СΪ 10��sr ��Ӧ���� W + 1 <= N��W Ϊ���ʹ��ڴ�С��N Ϊ���кŸ�����
//SRЭ���У����մ��ڴ�С��Ӧ���ڷ��ʹ��ڴ�С
//����ȡ���к� 0...19 �� 20 ��
//��������ڴ�С��Ϊ 1����Ϊͣ-��Э��
const int SEQ_SIZE = 20;	//���кŵĸ������� 0~19 ���� 20 ��
//���ڷ������ݵ�һ���ֽ����ֵΪ 0�������ݻᷢ��ʧ��
//��˽��ն����к�Ϊ 1~20���뷢�Ͷ�һһ��Ӧ

int ack[SEQ_SIZE];	//�յ� ack �������Ӧ 0~19 �� ack��0 �������ã�1 ������ã�2����ǰ��ʹ��
int ackTime[SEQ_SIZE];
int curSeq;			//��ǰ���ݰ��� seq
int curAck;			//��ǰ�ȴ�ȷ�ϵ� ack
int totalSeq;		//�յ��İ�������
int totalPacket;	//��Ҫ���͵İ�����
int sendSuccessTime = 0;
ofstream icout;
char ackCache[SEQ_SIZE][BUFFER_LENGTH];	//ack���ݻ���
int totalSeqList[SEQ_SIZE];

//��ʼ���׽���
bool initSocket(SOCKET& sockServer);	
//��ȡ��ǰϵͳʱ�䣬������� ptime ��
void getCurTime(char* ptime);		
//��鵱ǰ���к� curSeq �Ƿ����
bool seqIsAvailable();			
//��ʱ�ش������������������ڵ�����֡��Ҫ�ش�
void timeoutHandler(int ackNum);
//�յ� ack���ۻ�ȷ�ϣ�ȡ����֡�ĵ�һ���ֽ�
void ackHandler(char c);				
//���ڷ�������ʱ����һ���ֽڣ����кţ�Ϊ 0��ASCII��ʱ����ʧ�ܣ���˼�һ�ˣ��˴���Ҫ��һ��ԭ

//SRʵ��
int main(char** argv, int argc)
{
	SOCKET sockServer;
	SOCKADDR_IN addrClient; //�ͻ��˵�ַ
	int length = sizeof(SOCKADDR);
	char* buffer = (char*)calloc(BUFFER_LENGTH,sizeof(char*)); //���ݷ��ͽ��ջ�����
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
	//���������ݶ����ڴ� 
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
		//���������գ���û���յ����ݣ�����ֵΪ-1
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
			//���� sr ���Խ׶�
			//���� server��server ���� 0 ״̬���� client ���� 205 ״̬�루server���� 1 ״̬��
			//server �ȴ� client �ظ� 200 ״̬�룬����յ���server ���� 2 ״̬������ʼ�����ļ���������ʱ�ȴ�ֱ����ʱ
			//���ļ�����׶Σ�server ���ʹ��ڴ�С��Ϊ10
			ZeroMemory(buffer, sizeof(buffer));
			int recvSize;
			int waitCount = 0;
			//������һ�����ֽ׶�
			//���ȷ�������ͻ��˷���һ�� 205 ��С��״̬�루���Լ�����ģ���ʾ������׼�����ˣ����Է�������
			//�ͻ����յ� 205 ֮��ظ�һ�� 200 ��С��״̬�룬��ʾ�ͻ���׼�����ˣ����Խ���������
			//�������յ� 200 ״̬��֮�󣬾Ϳ�ʼʹ�� sr ����������
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
				case 0://���� 205 �׶�
					buffer[0] = 205;
					buffer[1] = totalPacket;
					sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
					Sleep(100);
					stage = 1;
					break;
				case 1://�ȴ����� 200 �׶Σ�û���յ��������+1����ʱ������˴Ρ����ӡ����ȴ��ӵ�һ����ʼ
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
				case 2://���ݴ���׶�
					if (seqIsAvailable() && (!sendOver || !recvOver || !endFlag)) {
						//���͸��ͻ��˵����кŴ� 1 ��ʼ
						buffer[0] = curSeq + 1;
						if (totalSeqList[curSeq] == -1) {
							totalSeqList[curSeq] = totalSeq;
							totalSeq++;
						}	
						ack[curSeq] = 2;
						//���ݷ��͵Ĺ�����Ӧ���ж��Ƿ������
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
					//�ȴ� Ack����û���յ����򷵻�ֵΪ-1��������+1
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, &length);
					if (recvSize < 0) {
						//10 �εȴ� ack ��ʱ�ش�
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
						//�յ� ack
						printf("Receive a ack of %d\n", buffer[0]);
						if (ack[buffer[0] - 1] == 2) {
							if (buffer[1] != '\0'){
								//printf("Data from client��\n%s\n", &buffer[1]);
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
						//����ȷ��
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
	//�ر��׽��֣�ж�ؿ�
	closesocket(sockServer);
	WSACleanup();
	free(data);
	free(buffer);
	return 0;
}


bool initSocket(SOCKET& sockServer)
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
	sockServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//�����׽���Ϊ������ģʽ
	int iMode = 1; //1����������0������
	ioctlsocket(sockServer, FIONBIO, (u_long FAR*) & iMode);//����������
	SOCKADDR_IN addrServer; //��������ַ
	//addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//���߾���
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
	//���к��Ƿ��ڵ�ǰ���ʹ���֮��
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
	unsigned char index = (unsigned char)c - 1; //���кż�һ
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
	//	//ack ���������ֵ���ص��� curAck �����
	//	for (int i = curAck; i < SEQ_SIZE; ++i) {
	//		ack[i] = TRUE;
	//	}
	//	for (int i = 0; i <= index; ++i) {
	//		ack[i] = TRUE;
	//	}
	//	curAck = index + 1;
	//}
}

