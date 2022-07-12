/*
* THIS FILE IS FOR IP TEST
*/
// system support
#include "sysInclude.h"
#include <iostream>
#include <Windows.h>

#pragma comment(lib,"Ws2_32.lib")

extern void ip_DiscardPkt(char* pBuffer, int type);

extern void ip_SendtoLower(char* pBuffer, int length);

extern void ip_SendtoUp(char* pBuffer, int length);

extern unsigned int getIpv4Address();

// implemented by students

typedef unsigned char byte;

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

	//????IPv4????
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
		//????§µ???
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
	
	//???§µ???
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

int stud_ip_recv(char* pBuffer, unsigned short length)
{
	IPv4Msg* msg = new IPv4Msg(pBuffer, length);
	int version = (msg->versionAndIHL >> 4) & 0xf;
	if (version != 4) {
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_VERSION_ERROR);
		return 1;
	}
	int IHR = msg->versionAndIHL & 0xf;
	if (IHR < 5) {
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_HEADLEN_ERROR);
		return 1;
	}
	int ttl = (int)msg->ttl;
	if (ttl == 0) {
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
		return 1;
	}
	int dstAddr = ntohl(msg->dstAddr);
	if (dstAddr != getIpv4Address() && dstAddr != 0xffffffff) {
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_DESTINATION_ERROR);
		return 1;
	}
	if (!msg->checkRight()) {
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
		return 1;
	}
	ip_SendtoUp(pBuffer, length);
	return 0;
}

int stud_ip_Upsend(char* pBuffer, unsigned short len, unsigned int srcAddr,
	unsigned int dstAddr, byte protocol, byte ttl)
{
	IPv4Msg* msg = new IPv4Msg(pBuffer, len, srcAddr, dstAddr, protocol, ttl);
	char* pack_to_sent = new char[len + 20];
	memcpy(pack_to_sent, msg, 20);
	memcpy(pack_to_sent + 20, msg->pData, len);
	ip_SendtoLower(pack_to_sent, len + 20);
	delete[] pack_to_sent;
	return 0;
}
