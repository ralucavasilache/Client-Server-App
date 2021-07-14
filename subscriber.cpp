#include "utils.h"

void usage(char *file) {
	fprintf(stderr, "Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", file);
	exit(0);
}

void extract_data_type(uint8_t type, char *data_type) {
	switch (type) {
	case 0:
		strcpy(data_type, "INT");
		break;
	case 1:
		strcpy(data_type, "SHORT_REAL");
		break;
	case 2:
		strcpy(data_type, "FLOAT");
		break;
	case 3:
		strcpy(data_type, "STRING");
		break;
	default:
		break;
	}
}
// printeaza un mesaj atunci cand clinetul da subscribe/unsubscribe
void print_feedback(char *type) {
	if (strcmp(type, "subscribe") == 0) {
		printf("Subscribed to topic.\n");
	}
	else {
		printf("Unsubscribed from topic.\n");
	}
}
// functie folosita pentru a putea reutiliza porturile
void reuse(int sockfd) {
	const int opt = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) < 0) {
		printf("setsockopt(SO_REUSEADDR) failed");
	}
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) < 0) {
		printf("setsockopt(SO_REUSEPORT) failed");
	}
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	int sockfd, n, ret;
	struct sockaddr_in serv_addr;
	char buffer[BUFLEN];
	char id[ID_LEN];
	memset(id, 0, ID_LEN);

	fd_set read_fds; // multimea de citire folosita in select()
	FD_ZERO(&read_fds);
	fd_set tmp_fds;	 // multime folosita temporar
	FD_ZERO(&tmp_fds);
	int fdmax;		 // valoare maxima fd din multimea read_fds

	if (argc < 4) {
		usage(argv[0]);
	}
	// id-ul clientului
	strcpy(id, argv[1]);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");

	reuse(sockfd);

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	inet_aton(argv[2], &serv_addr.sin_addr);

	// clientul trimite cerere de conectare catre server
	ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");

	FD_SET(sockfd, &read_fds);
	FD_SET(STDIN_FILENO, &read_fds);
	fdmax = sockfd;

	// dezactivare algpritmul lui Nagle
	int flag = 1;
	int result = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
	if (result < 0)
	{
		fprintf(stderr, "Error while dezactivating Nagle's Algorithm\n");
	}

	// se trimite id-ul meu catre server
	n = send(sockfd, id, ID_LEN, 0);
	DIE(n < 0, "send");

	while (1) {
		tmp_fds = read_fds;

		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
			// se citeste de la tastatura
			memset(buffer, 0, BUFLEN);
			fgets(buffer, BUFLEN - 1, stdin);
			// se inchide clientul
			if (strcmp(buffer, "exit\n") == 0) {
				close(sockfd);
				return 0;
			}

			// structura care va fi trimisa catre server
			struct tcp_msg *new_msg = (struct tcp_msg *)malloc(sizeof(struct tcp_msg));
			memset(new_msg, 0, sizeof(struct tcp_msg));
			strcpy(new_msg->id, id);

			char *token = strtok(buffer, " ");
			// se primeste un msg de tip subscribe
			if (strcmp(token, "subscribe") == 0) {
				strcpy(new_msg->type, token);
				// completare topic
				token = strtok(NULL, " ");
				if (token == NULL) {
					fprintf(stderr, "Incomplete message: missing topic and SF!\n");
					close(sockfd);
					return 0;
				}
				strcpy(new_msg->topic, token);
				// completare SF
				token = strtok(NULL, " \n");
				if (token == NULL) {
					fprintf(stderr, "Incomplete message: missing SF!\n");
					close(sockfd);
					return 0;
				}
				new_msg->SF = atoi(token);
			}
			// se primeste un msj de tip unsusbcribe
			else if (strcmp(token, "unsubscribe") == 0) {
				strcpy(new_msg->type, token);
				// completare topic
				token = strtok(NULL, " ");
				if (token == NULL) {
					fprintf(stderr, "Incomplete message: missing topic!\n");
					close(sockfd);
					return 0;
				}
				strcpy(new_msg->topic, token);
			}
			else {
				fprintf(stderr, "Wrong type\n");
				close(sockfd);
				return 0;
			}

			n = send(sockfd, (char *)new_msg, sizeof(struct tcp_msg), 0);
			DIE(n < 0, "send");
			print_feedback(new_msg->type);
		}
		else if (FD_ISSET(sockfd, &tmp_fds)) {
			// se primesc mesaje de la server
			memset(buffer, 0, BUFLEN);
			n = recv(sockfd, (struct udp_msg *)buffer, sizeof(struct udp_msg), 0);
			// daca serverul se deconecteaza -> se va deconecta si clientu;
			if (n == 0) {
				close(sockfd);
				return 0;
			}
			char data_type[20];
			extract_data_type(((struct udp_msg *)buffer)->data_type, data_type);
			// se printeaza update-urile
			printf("%s:%d - %s - %s - %s\n", ((struct udp_msg *)buffer)->ip, ((struct udp_msg *)buffer)->port,
											 ((struct udp_msg *)buffer)->topic,
											 data_type,
			   								 ((struct udp_msg *)buffer)->data);
		}
	}
	close(sockfd);
	return 0;
}
