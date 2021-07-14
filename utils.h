#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <bits/stdc++.h>
#include "helpers.h"
#include <cstring>
using namespace std;

#define MAX_CL 100000

#define ID_LEN 11
#define TYPE_LEN 20
#define TOPIC_LEN 50
#define IP_LEN 16
#define PORT_LEN 40
#define DATA_LEN 1501

// mesaj trimis de catre clientul tcp catre server
struct tcp_msg {
	char id[ID_LEN];
    char type[TYPE_LEN];
	char topic[TOPIC_LEN];
	int SF;
};

// mesaj trimis de catre server catre clinetul tcp
struct udp_msg {
    char ip[IP_LEN];
    uint16_t port;
    char topic[TOPIC_LEN];
    uint8_t data_type;
    char data[DATA_LEN];
};

struct client {
    int socket;
    // conectat/neconectat
    bool status;
    // key : topic, value : sf
    map<string, int> subscriptions;
    // mesajele in asteptare
    queue<udp_msg*> updates;
};

// structura corespunzatoare pachetului venit de la clientul udp
struct udp_payload {
    char topic[TOPIC_LEN];
    uint8_t data_type;
    char data[DATA_LEN];
};