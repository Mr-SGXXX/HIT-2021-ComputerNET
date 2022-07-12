#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <time.h>
#include <io.h>

#pragma comment(lib,"Ws2_32.lib")

#define MAX_SIZE 65507
#define HTTP_PORT 80
#define PROXY_THREAD_POOL 20
#define BANNED_USR_IP "127.0.0.0"	//被禁止访问的IP，127.0.0.1时禁用本机 
#define FISHING_URL "http://jwes.hit.edu.cn"	//钓鱼网站
#define FISHED_URL "http://jwc.hit.edu.cn/"		//被钓鱼的网站
#define BANNED_URL "http://jwts.hit.edu.cn/"	//被禁止访问的网站

typedef struct {
	char method[4]; // POST 或者 GET
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
}HttpHeader;

typedef struct {
	SOCKET clientSocket;
	SOCKET serverSocket;
}ProxyParam;

BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
//缓存相关函数
void SaveCache(const HttpHeader* header,const char** Buffers, int* size, int counter);
int LoadCache(const HttpHeader* header, char* clientBuffer, SOCKET Server, int* size, char** Buffers);
BOOL CheckModified(SOCKET Server, char* host, char* clientBuffer, char* cacheBuffer);

//代理相关参数
SOCKET ProxyServer;
SOCKADDR_IN ProxyServerAddr;
const int ProxyPort = 10240;
const char* CacheDir = ".\\Cache\\";

int main(char ** argv,int argc)
{
	printf("Proxy Server Launching\n");
	printf("Initializing...\n");
	//若Cache文件夹不存在则创建
	if(_access(CacheDir,0) == -1)
		system("mkdir Cache");
	if (!InitSocket()) {
		printf("Failed to init Socket\n");
		return -1;
	}
	else printf("Socket Init Success, Listener Port: %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam* lpProxyParam;
	struct sockaddr_in addr;
	int addrlen = sizeof(struct sockaddr);
	HANDLE hThread;
	while (1) {
		acceptSocket = accept(ProxyServer, &addr, &addrlen);
		if (!strcmp(BANNED_USR_IP, inet_ntoa(addr.sin_addr)))
		{
			printf("-------ERROR--BEGIN--LINE-------\nIP:%s Access Denied\n-------ERROR---END---LINE-------\n", inet_ntoa(addr.sin_addr));
			continue; //用户过滤
		}
		lpProxyParam = (ProxyParam*)calloc(sizeof(ProxyParam), 1);
		if (lpProxyParam == NULL) continue;
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}

BOOL InitSocket() {
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
		printf("Load winsock failed. Error code: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Failed to find right winsock version\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("Failed to get Socket. Error code: %d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("Faild to bind Socket\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("Failed to listen to port %d", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char** Buffers = (char**)calloc(sizeof(char*), 1000);
	int* size = (int*)calloc(sizeof(int), 1000);
	char* tempBuffer;
	int counter = 0, i;
	char* Buffer = (char*)calloc(sizeof(char), MAX_SIZE);
	char* CacheBuffer = (char*)calloc(sizeof(char), MAX_SIZE);
	if (Buffer == NULL || Buffers == NULL || size == NULL) goto error;
	memset(Buffer, 0, MAX_SIZE * sizeof(char));
	int recvSize;
	int ret;
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAX_SIZE, 0);
	if (recvSize <= 0) goto error;
	//if (Buffer[0] == 'G' && !memcmp(Buffer + 4, FISHED_URL, strlen(FISHED_URL)))
		//Fish(Buffer);	//钓鱼网站
	if(Buffer[0] == 'G')
		printf("Client Message:%d\n-------MESSAGE-BEGIN-LINE-------\n%s-------MESSAGE--END--LINE-------\n", recvSize, Buffer);
	else if (Buffer[0] != 'G' && Buffer[0] != 'P') goto error;
	HttpHeader* httpHeader = (HttpHeader*)calloc(sizeof(HttpHeader), 1);
	if (CacheBuffer == NULL || httpHeader == NULL) goto error;
	memset(CacheBuffer, 0, MAX_SIZE);
	memcpy(CacheBuffer, Buffer, MAX_SIZE);
	ParseHttpHead(CacheBuffer, httpHeader);
	if (strstr(httpHeader->url, FISHED_URL) != NULL) {
		//钓鱼网站功能
		printf("-------ERROR--BEGIN--LINE-------\nSource URL:%s Turned to Destination URL:%s\n-------ERROR---END---LINE-------\n", FISHED_URL, FISHING_URL);
		memcpy(httpHeader->host, FISHING_URL + 7, strlen(FISHING_URL + 7) + 1);
		memcpy(httpHeader->url, FISHING_URL, strlen(FISHING_URL)+1);
	}
	if (!strcmp(httpHeader->url, BANNED_URL)) {
		printf("-------ERROR--BEGIN--LINE-------\nURL:%s Access Denied\n-------ERROR---END---LINE-------\n", httpHeader->url);
		goto error;	//网站过滤
	}
	memset(CacheBuffer, 0, ((size_t)recvSize + 1) * sizeof(char));
	memcpy(CacheBuffer, Buffer, recvSize);
	if (httpHeader->method[0] == 'G' && (counter = LoadCache(httpHeader, CacheBuffer, ((ProxyParam*)lpParameter)->serverSocket, size, Buffers)))
	{
		//缓存功能
		for (i = 0; i < counter; i++)
		{
			Buffer = Buffers[i];
			ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, size[i], 0);
		}
		Buffer = NULL;
	}
	else
	{
		//正常转发请求报文
		if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host))
			goto error;
		printf("Proxy Link Success, Host:%s \n", httpHeader->host);
		//将客户端发送的 HTTP 数据报文直接转发给目标服务器
		ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
		while (1)
		{
			//等待目标服务器返回数据
			recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAX_SIZE, 0);
			if (recvSize < 0)	goto error;
			else if (recvSize == 0) break;
			//将目标服务器返回的数据直接转发给客户端
			//printf("Server Message[%d]:\n-------MESSAGE-BEGIN-LINE-------\n%s-------MESSAGE--END--LINE-------\n", counter + 1, Buffer);
			ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, recvSize, 0);
			if (httpHeader->method[0] == 'G')
			{
				printf("Server Message[%d]:%d\n-------MESSAGE-BEGIN-LINE-------\n%s\n-------MESSAGE--END--LINE-------\n", counter + 1, recvSize, Buffer);
				tempBuffer = (char*)calloc(sizeof(char), MAX_SIZE);
				if (tempBuffer == NULL) goto error;
				memcpy(tempBuffer, Buffer, sizeof(char) * MAX_SIZE);
				size[counter] = recvSize;
				Buffers[counter++] = tempBuffer;
			}
		}
		if (httpHeader->method[0] == 'G' &&
			!(Buffers[0][9] == '3' && Buffers[0][10] == '0' && Buffers[0][11] == '4'))
		{
			SaveCache(httpHeader, Buffers, size, counter);
			//printf("Socket Closed\n---------------------------------------------------------------------------\n");
		}
	}
	//错误处理
	printf("Socket Closed\n---------------------------------------------------------------------------\n");
error:
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	free(lpParameter);
	free(CacheBuffer);
	free(Buffer);
	for (; counter > 0; counter--)
		free(Buffers[counter - 1]);
	free(Buffers);
	_endthreadex(0);
	return 0;
}

void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	//printf("Client First Line:\n%s\n", p);
	if (p[0] == 'G') {//GET 方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST 方式
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	//printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[7];
				memset(header, 0, sizeof(char) * 7);
				memcpy(header, p, 6);
				header[6] = '\0';
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

BOOL ConnectToServer(SOCKET* serverSocket, char* host) {
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host);
	if (!hostent) {
		return FALSE;
	}
	IN_ADDR Inaddr = *((IN_ADDR*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr))
		== SOCKET_ERROR) {
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}

void SaveCache(const HttpHeader* header, const char** Buffers, int* size, int counter)
{
	char path[270];
	int i;
	char fileName[255];
	FILE* fp;
	//生成Cache存储文件名
	memset(fileName, 0, sizeof(char) * 80);
	strcpy_s(path, 100, CacheDir);
	for (i = 0; header->url[i + 7] != '\0'; ++i)
	{
		if (header->url[i + 7] == '/')
			fileName[i] = '#';
		else if (header->url[i + 7] == ':')
			fileName[i] = ';';
		else if (header->url[i + 7] == '.')
			fileName[i] = '_';
		else if (header->url[i + 7] == '?')
			fileName[i] = '!';
		else
			fileName[i] = header->url[i + 7];
	}
	fileName[i] = '\0';
	strcat_s(path, strlen(fileName) + strlen(path) + 2, fileName);
	fopen_s(&fp, path, "wb");
	if (fp == NULL) {
		printf("Cache Writer Error\n");
		return;
	}
	for (i = 0; i < counter; ++i)
	{                           
		//缓存文件写入
		//fprintf(fp, "%s\n%d---------------------------------------------------------------------------\n", Buffers[i], size[i]);
		fwrite(size + i, sizeof(int), 1, fp);	//一次接收到的响应报文大小
		fwrite(Buffers[i], sizeof(char), size[i], fp);	//一次接收到的响应报文内容
	}
	fclose(fp);
}

int LoadCache(const HttpHeader* header, char* clientBuffer, SOCKET Server, int* size, char** Buffers)
{
	char* Buffer;
	char path[270];
	char* line;
	int i;
	int counter = 0;
	char fileName[255];
	FILE* fp;
	memset(fileName, 0, sizeof(char) * 80);
	strcpy_s(path, 100, CacheDir);
	//生成Cache存储文件名
	for (i = 0; header->url[i + 7] != '\0'; ++i)
	{
		if (header->url[i + 7] == '/')
			fileName[i] = '#';
		else if (header->url[i + 7] == ':')
			fileName[i] = ';';
		else if (header->url[i + 7] == '.')
			fileName[i] = '_';
		else if (header->url[i + 7] == '?')
			fileName[i] = '!';
		else
			fileName[i] = header->url[i + 7];
	}
	fileName[i] = '\0';
	strcat_s(path, strlen(fileName) + strlen(path) + 2, fileName);
	fopen_s(&fp, path, "rb");
	if (fp == NULL)	return 0;
	Buffer = (char*)calloc(sizeof(char), MAX_SIZE);
	line = (char*)calloc(sizeof(char), MAX_SIZE);
	if (Buffer == NULL || line == NULL) return 0;
	//缓存数据读取
	while (!feof(fp))
	{
		fread_s(size + counter, 4, sizeof(int), 1, fp);
		fread_s(Buffer, MAX_SIZE, sizeof(char), size[counter], fp);
		Buffers[counter++] = Buffer;
		if (counter == 1 && CheckModified(Server, header->host, clientBuffer, Buffer)) {
			//检测到缓存过期
			fclose(fp);
			free(Buffer);
			free(line);
			return 0;
		}
		printf("Cache Buffer[%d]:%d\n-------MESSAGE-BEGIN-LINE-------\n%s", counter, size[counter - 1], Buffer);
		printf("\n-------MESSAGE--END--LINE-------\n");
		Buffer = (char*)calloc(sizeof(char), MAX_SIZE);
		if (Buffer == NULL) return 0;
	}
	/*fgets(line, MAX_SIZE, fp);
	while (!feof(fp))
	{
		if (strstr(line, "---------------------------------------------------------------------------\n") != NULL)
		{
			sscanf_s(line, "%d---------------------------------------------------------------------------\n", size + counter);
			Buffers[counter++] = Buffer;
			
			printf("Cache Buffer[%d]:%d\n-------MESSAGE-BEGIN-LINE-------\n%s", counter, size[counter - 1], Buffer);
			printf("\n-------MESSAGE--END--LINE-------\n");
			Buffer = (char*)calloc(sizeof(char), MAX_SIZE);
			if (Buffer == NULL) return 0;
		}
		else
		{
			strcat_s(Buffer, strlen(Buffer) + strlen(line) + 1, line);
		}
		fgets(line, MAX_SIZE, fp);
	}*/
	fclose(fp);
	free(Buffer);
	free(line);
	return counter;
}

BOOL CheckModified(SOCKET Server, char* host, char* clientBuffer, char* cacheBuffer)
{
	char* IMSBuffer = (char*)calloc(sizeof(char), 100);
	char* checkBuffer = (char*)calloc(sizeof(char), MAX_SIZE);
	if (IMSBuffer == NULL || checkBuffer == NULL)
		return TRUE;
	char* splitPoint = strstr(cacheBuffer, "Last-Modified:");
	//查找Last-Modified段
	if (splitPoint == NULL)
	{
		free(IMSBuffer);
		free(checkBuffer);
		return TRUE;
	}
	//构建If-Modified-Since段
	memcpy(IMSBuffer, "If-Modified-Since: ",20);
	int pos = strlen(IMSBuffer);
	int i = 0;
	while (1)
	{
		IMSBuffer[pos + i] = splitPoint[i + 15];
		if (IMSBuffer[pos + i] == '\n') break;
		i++;
	}
	//在第二行插入If-Modified-Since段
	splitPoint = strstr(clientBuffer, "\r\n") + 2;
	if (splitPoint == NULL) 
	{
		free(IMSBuffer);
		free(checkBuffer);
		return TRUE;
	}
	char* checkptr = checkBuffer;
	while (1)
	{
		*checkptr = *clientBuffer;
		checkptr++;
		clientBuffer++;
		if (clientBuffer == splitPoint)
		{
			memcpy(checkptr, IMSBuffer, strlen(IMSBuffer));
			checkptr += strlen(IMSBuffer);
		}
		if (*clientBuffer == '\0') break;
	}
	if (!ConnectToServer(&Server, host))
	{
		free(IMSBuffer);
		free(checkBuffer);
		return TRUE;
	}
		
	int ret = send(Server, checkBuffer, strlen(checkBuffer) + 1, 0);
	memset(checkBuffer, 0, MAX_SIZE);
	int recvSize = recv(Server, checkBuffer, MAX_SIZE, 0);
	if (!(checkBuffer[9] == '3' && checkBuffer[10] == '0' && checkBuffer[11] == '4'))
	{
		//状态码非304
		free(IMSBuffer);
		free(checkBuffer);
		return TRUE;
	}
	free(IMSBuffer);
	free(checkBuffer);
	return FALSE;
}