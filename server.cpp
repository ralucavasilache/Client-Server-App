#include "utils.h"
using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}
// functie apelata pentru a putea reutiliza un port
void reuse(int sock_tcp, int sock_udp) {
	const int opt = 1;
	if (setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0) {
    	printf("setsockopt(SO_REUSEADDR) failed");
	}
	if (setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) < 0) {
		printf("setsockopt(SO_REUSEPORT) failed");
	}

	if (setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0) {
    	printf("setsockopt(SO_REUSEADDR) failed");
	}
	if (setsockopt(sock_udp, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) < 0) {
		printf("setsockopt(SO_REUSEPORT) failed");
	}
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	// key = topic, value = id-urile clientilor abonate la acel topic
	map<string, vector<string>> subscribers;
	// key = id, value = struct client
	map <string, struct client*> clients;

	int newsockfd;
	char buffer[BUFLEN];
	struct sockaddr_in serv_addr_tcp, serv_addr_udp, cli_addr;
	int n, i, ret;
	socklen_t clilen = sizeof(cli_addr);

	if (argc < 2) {
		usage(argv[0]);
	}

	int portno = atoi(argv[1]);
	DIE(portno == 0, "atoi");

	// socket care asteapta conexiuni de la clientii tcp
	int sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sock_tcp < 0, "socket");
	// socket care asteapta conexiuni de la clientii udp
	int sock_udp = socket(PF_INET, SOCK_DGRAM, 0);
	DIE(sock_udp < 0, "socket");
	// functii necesara pentru reutilizarea porturilor
	reuse(sock_tcp, sock_udp);

	// multimea de citire folosita in select()
	fd_set read_fds;
	FD_ZERO(&read_fds);
	// multime folosita temporar
	fd_set tmp_fds;
	FD_ZERO(&tmp_fds);
	// valoare maxima fd din multimea read_fds
	int fdmax;

	memset((char *) &serv_addr_tcp, 0, sizeof(serv_addr_tcp));
	serv_addr_tcp.sin_family = AF_INET;
	serv_addr_tcp.sin_port = htons(portno);
	serv_addr_tcp.sin_addr.s_addr = INADDR_ANY;
	// legare tcp
	ret = bind(sock_tcp, (struct sockaddr *) &serv_addr_tcp, sizeof(struct sockaddr));
	DIE(ret < 0, "bind");
	// se asculta conexiuni tcp pe portul sock_tcp
	ret = listen(sock_tcp, MAX_CL);
	DIE(ret < 0, "listen");

	memset((char *) &serv_addr_udp, 0, sizeof(serv_addr_udp));
	serv_addr_udp.sin_family = AF_INET;
	serv_addr_udp.sin_port = htons(portno);
	serv_addr_udp.sin_addr.s_addr = INADDR_ANY;
	// legare udp
	ret = bind(sock_udp, (struct sockaddr *) &serv_addr_udp, sizeof(struct sockaddr));
	DIE(ret < 0, "bind");

	// se adauga in multimea read_fds socket-ul pe care se asculta conexiuni tcp, respectiv udp
	FD_SET(STDIN_FILENO, &read_fds);
	FD_SET(sock_tcp, &read_fds);
	FD_SET(sock_udp, &read_fds);
	if (sock_tcp > sock_udp) {
		fdmax = sock_tcp;
	} else {
		fdmax = sock_udp;
	}

	while (1) {
		tmp_fds = read_fds; 

		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");

		for (i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &tmp_fds)) {
				if (i == STDIN_FILENO) {
					fgets(buffer, BUFLEN - 1, stdin);
					if (strcmp(buffer, "exit\n") == 0) {
						// se inchid conexiunile cu toti clientii
						for (i = 0; i <= fdmax; i++) {
							if ((FD_ISSET(i, &tmp_fds) && i != STDIN_FILENO)) {
								close(i);
							}
						}
						exit(0);
                    } else {
                        printf("Wrong command\n");
                    }
				} 
				else if (i == sock_tcp) {
					// s-a primit o cerere de conexiune pe socketul tcp care asculta conexiuni

					// accept conexiunea
					newsockfd = accept(sock_tcp, (struct sockaddr *)&cli_addr, &clilen);
					DIE(newsockfd < 0, "accept");

					// dezactivare algoritmul lui Nagle
					int flag = 1;
					int result = setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
					if (result < 0) {
						fprintf(stderr, "Error while dezactivating Nagle's Algorithm\n");
					}

					memset(buffer, 0, BUFLEN);
					// primesc id-ul clientului tcp
					n = recv(newsockfd, buffer, ID_LEN, 0);
					DIE(n < 0, "no id received");

					// daca clientul cu id-ul primit nu exista in baza de date, il adaug
					if (clients.find(buffer) == clients.end()) {
						struct client *new_client = new client();
						new_client->status = true;
						new_client->socket = newsockfd;
						clients[buffer] = new_client;

						// adaug socket in lista
						FD_SET(newsockfd, &read_fds);
						if (newsockfd > fdmax) { 
							fdmax = newsockfd;
						}

						printf("New client %s connected from %s:%d.\n", buffer,
						   inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
					// daca exista deja
					} else {
						// este conectat -> eroare
						if (clients[buffer]->status == true) {
							printf("Client %s already connected.\n", buffer);
							close(newsockfd);
						// nu este conectat -> se reconecteaza
						} else {
							clients[buffer]->status = true;
							// trimit toate update-urile din coada
							while(!clients[buffer]->updates.empty()) {
								struct udp_msg *new_msg = clients[buffer]->updates.front();
								n = send(clients[buffer]->socket, new_msg, sizeof(struct udp_msg), 0);
								DIE(n < 0, "couldn't send message");
								
								clients[buffer]->updates.pop();
							}

							// adaug socket in lista
							FD_SET(newsockfd, &read_fds);
							if (newsockfd > fdmax) { 
								fdmax = newsockfd;
							}
							printf("New client %s connected from %s:%d.\n", buffer,
						    	inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
						}
					}
					
				} else if (i == sock_udp) {
					// a venit o conexiune udp
					memset(buffer, 0, BUFLEN);

					socklen_t len = sizeof(serv_addr_udp);
					n = recvfrom(sock_udp, buffer, BUFLEN, 0, (sockaddr*) &serv_addr_udp, &len);
					DIE(n < 0, "recv");

					struct udp_payload recv_udp;
					memset(&recv_udp, 0, sizeof(udp_payload));
					memcpy(&recv_udp, buffer, sizeof(udp_payload));
					
					// creez o structura care trebuie trimisa catre clientii tcp
					struct udp_msg *to_send = new udp_msg();
					to_send->port = ntohs(serv_addr_udp.sin_port);
					int size = sizeof(inet_ntoa(serv_addr_udp.sin_addr));
					memcpy(to_send->ip, inet_ntoa(serv_addr_udp.sin_addr), size);
					
					// daca update-ul e de tip int
					if (recv_udp.data_type == 0) {
						
						to_send->data_type = 0;
						uint32_t payload = ntohl(*(uint32_t*)(recv_udp.data + 1));
						if (recv_udp.data[0] == 1) {
							payload = (-1) * payload;
						}
						sprintf(to_send->data, "%d", payload);
					// daca update-ul e de tip short real
					}  else if (recv_udp.data_type == 1) {
						
						to_send->data_type = 1;
						float payload = ntohs(*(uint16_t*)(recv_udp.data));
						payload = payload / 100;
						setprecision(2);
						sprintf(to_send->data, "%.2f", payload);
					// daca update-ul e float
					} else if (recv_udp.data_type == 2) {
						
						to_send->data_type = 2;
						double payload = ntohl(*(uint32_t*)(recv_udp.data + 1));
						payload = payload / pow(10, recv_udp.data[5]);
						if (recv_udp.data[0] == 1) {
							payload = (-1) * payload;
						}
						sprintf(to_send->data, "%lf", payload);
					// daca update-ul e un string
					} else if (recv_udp.data_type == 3) {
						
						to_send->data_type = 3;
						strcpy(to_send->data, recv_udp.data);
					}
					strcpy(to_send->topic, recv_udp.topic);
					// se cauta toti clientii abonati la topic
					for (auto p : subscribers[recv_udp.topic]) {
						// daca sunt conectati, se trimite updat-ul
						if (clients[p]->status == true) {
							n = send(clients[p]->socket, (char *)to_send, sizeof(struct udp_msg), 0);
							DIE(n < 0, "couldn't send message");
						// altfel, se adauga in coada
						} else {
							if (clients[p]->subscriptions[recv_udp.topic] == 1) {
								clients[p]->updates.push(to_send);
							}
						}
					}

				} else {
					// se primesc mesaje de la clientii tcp de tip subscribe/unsubscribe
					memset(buffer, 0, BUFLEN);
					n = recv(i, buffer, sizeof(struct tcp_msg), 0);
					DIE(n < 0, "recv");

					// daca conexiunea s-a inchis
					if (n == 0) {
						for (const auto &p : clients) {
							if (p.second->socket == i) {
								//  updatez statusul ca fiind false
								p.second->status = false;
								cout << "Client " << p.first << " disconnected.\n";
								break;
							}
						}
						close(i);						
						// se scoate din multimea de citire socketul inchis 
						FD_CLR(i, &read_fds);
					} else {
						struct tcp_msg *msg = (struct tcp_msg*)buffer;
						// clienttul vrea sa se aboneze la un topic
						if (strcmp(msg->type, "subscribe") == 0) {
							clients[msg->id]->subscriptions[msg->topic] = msg->SF;
							// adaugam subscriptia in lista de topicuri
							subscribers[msg->topic].push_back(msg->id);
						// clientul vrea sa se dezaboneze
						} else {
							// sterg id-ul din lista de topic-uri
							subscribers[msg->topic].erase(remove(subscribers[msg->topic].begin(),
																	subscribers[msg->topic].end(),msg->id),
																	subscribers[msg->topic].end());
							// sterg topicul din lista de subscriptions
							clients[msg->id]->subscriptions.erase(clients[msg->id]->subscriptions.find(msg->topic));
						}
					}
				}
			}
		}
	}

	close(sock_tcp);

	return 0;
}
