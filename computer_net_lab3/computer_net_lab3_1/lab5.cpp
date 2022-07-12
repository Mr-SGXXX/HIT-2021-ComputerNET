/*
* THIS FILE IS FOR IP FORWARD TEST
*/
#include "sysInclude.h"
#include "Windows.h"

#pragma comment(lib,"Ws2_32.lib")

// system support
extern void fwd_LocalRcv(char* pBuffer, int length);

extern void fwd_SendtoLower(char* pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char* pBuffer, int type);

extern unsigned int getIpv4Address();

// implemented by students
typedef struct stud_route_msg {
	unsigned int dest;
	unsigned int masklen;
	unsigned int nexthop;
}stud_route_msg;

typedef struct dataNode {
	unsigned int highPart;
	unsigned int lowPart;
	unsigned int masklen;
	unsigned int nextLeap;
}DataNode;

typedef struct routeListNode {
	DataNode* data;
	struct routeListNode* pNext;
}RouteListNode;

class RouteList {
public:
	RouteListNode* head;
	RouteListNode* tail;

	RouteList() {
		head = nullptr;
		tail = nullptr;
	}

	~RouteList() {
		RouteListNode* pCur = head;
		RouteListNode* pNext;
		while (pCur != nullptr)
		{
			pNext = pCur->pNext;
			delete pCur->data;
			delete pCur;
			pCur = pNext;
		}
	}

	void addNode(unsigned int IPv4Addr, unsigned int masklen, unsigned int nextLeap) {
		RouteListNode* temp = new RouteListNode();
		DataNode* data = new DataNode();
		temp->data = data;
		temp->pNext = nullptr;
		IPv4Addr = htonl(IPv4Addr);
		masklen = htonl(masklen);
		nextLeap = htonl(nextLeap);
		data->highPart = getHighPart(IPv4Addr, masklen);
		data->lowPart = getLowPart(IPv4Addr, masklen);
		data->masklen = masklen;
		data->nextLeap = nextLeap;
		if (head == nullptr) {
			tail = temp;
			head = tail;
		}
		else {
			tail->pNext = temp;
			tail = temp;
		}
	}

	bool getNextLeap(unsigned int dstAddr, unsigned int& nextLeap) {
		int maxMaskLen = 0;
		RouteListNode* pCur = head;
		bool findNextLeap = false;
		while (pCur != nullptr)
		{
			int masklen = pCur->data->masklen;
			if (masklen > maxMaskLen && getHighPart(dstAddr, masklen) == pCur->data->highPart) {
				nextLeap = pCur->data->nextLeap;
				maxMaskLen = masklen;
				findNextLeap = true;
			}
			pCur = pCur->pNext;
		}
		return findNextLeap;
	}
};

class IPv4Msg
{
public:
	unsigned char versionAndIHL;
	unsigned char typeOfService;
	unsigned short totalLength;
	unsigned short id;
	unsigned short offset;
	unsigned char ttl;
	unsigned char protocol;
	unsigned short checksum;
	unsigned int srcAddr;
	unsigned int dstAddr;
	char* pData;
	IPv4Msg() {
		memset(this, 0, sizeof(IPv4Msg));
	}

	IPv4Msg(char* pBuffer, unsigned short length) {
		memset(this, 0, sizeof(IPv4Msg));
		memcpy(this, (IPv4Msg*)pBuffer, sizeof(IPv4Msg));
		this->pData = pBuffer + (versionAndIHL & 0xf) * 4;
	}

	//构造IPv4分组
	IPv4Msg(char* pBuffer, unsigned int len, unsigned int srcAddr, unsigned int dstAddr,
		byte _protocol, byte ttl) {
		memset(this, 0, sizeof(IPv4Msg));
		this->versionAndIHL = 0x45;
		this->totalLength = htons(len + 20);
		this->ttl = ttl;
		this->protocol = _protocol;
		this->srcAddr = htonl(srcAddr);
		this->dstAddr = htonl(dstAddr);
		this->pData = pBuffer;
		//计算校验和
		updataCheckSum();
	}

	void updataCheckSum() {
		this->checksum = 0;
		int checkSum = 0;
		for (int i = 0; i < 10; i++) {
			checkSum += (int)(((unsigned char*)this)[i * 2] << 8);
			checkSum += (int)(((unsigned char*)this)[i * 2 + 1]);
		}
		while ((checkSum & 0xffff0000) != 0) {
			checkSum = (checkSum & 0xffff) + ((checkSum >> 16) & 0xffff);
		}
		this->checksum = htons((unsigned short)(~checkSum));
	}

	//检查校验和
	bool checkRight() {
		int checkSum = ntohs(this->checksum);
		int sum = 0;
		int IHL = this->versionAndIHL & 0xf;
		for (int i = 0; i < IHL * 2; i++) {
			sum += (int)(((unsigned char*)this)[i * 2] << 8);
			sum += (int)(((unsigned char*)this)[i * 2 + 1]);
		}

		while ((sum & 0xffff0000) != 0)
		{
			sum = sum & 0xffff + (sum >> 16) & 0xffff;
		}
		if ((unsigned short)sum != 0xffff)
			return false;
		else return true;
	}
};
//全局变量
RouteList* routeList;

//获取子网号
unsigned int getHighPart(unsigned int IPv4Addr, unsigned int masklen) {
	return IPv4Addr | ((1 << (32 - masklen)) - 1);
}

//获取子网下的主机号
unsigned int getLowPart(unsigned int IPv4Addr, unsigned int masklen) {
	IPv4Addr = IPv4Addr << (32 - masklen);
	IPv4Addr = IPv4Addr >> (32 - masklen);
	return IPv4Addr;
}

void stud_Route_Init()
{
	routeList = new RouteList();
	return;
}

void stud_route_add(stud_route_msg* proute)
{
	routeList->addNode(proute->dest, proute->masklen, proute->nexthop);
	return;
}

int stud_fwd_deal(char* pBuffer, int length)
{
	IPv4Msg* msg = new IPv4Msg(pBuffer, length);
	unsigned int dstAddr = ntohl(msg->dstAddr);
	unsigned int hostAddr = getIpv4Address();
	unsigned int nextLeap;
	if (dstAddr == 0xFFFFFFFF || dstAddr == hostAddr)
	{
		fwd_LocalRcv(pBuffer, length);
		return 0;
	}
	if (routeList->getNextLeap(dstAddr, nextLeap)) {
		int ttl = (int)msg->ttl;
		if (ttl == 0)
		{
			fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
			return 1;
		}
		msg->ttl = (unsigned char)(ttl - 1);

		msg->updataCheckSum();
		memcpy(pBuffer, msg, 20);
		fwd_SendtoLower(pBuffer, length, nextLeap);
		return 0;
	}
	else {
		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
		return 1;
	}
	return 0;
}

