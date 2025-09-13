#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <stdint.h>
#include <netdb.h>

struct IPv4Header
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t IHL : 4;
    uint8_t Version : 4;
    uint8_t ECN : 2;
    uint8_t DSCP : 6;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    uint8_t Version : 4;
    uint8_t IHL : 4;
    uint8_t DSCP : 6;
    uint8_t ECN : 2;
#endif
    uint16_t TotalLength;
    uint16_t Identification;
    uint8_t Flags : 3;
    uint16_t FragmentOffset : 13;
    uint8_t TTL;
    uint8_t Protocol;
    uint16_t HeaderChecksum;
    uint32_t SourceAddress;
    uint32_t DestinationAddress;
} __attribute__((packed));

struct ICMPHeader
{
    uint8_t Type;
    uint8_t Code;
    uint16_t Checksum;
    uint16_t Identifier;
    uint16_t Sequence;
} __attribute__((packed));

uint16_t calculate_checksum(void *buf, int length)
{
    uint16_t *ptr = (uint16_t *)buf;
    uint32_t sum = 0;

    while (length > 1)
    {
        sum += *ptr++;
        length -= 2;
    }

    if (length == 1)
    {
        sum += *(uint8_t *)ptr;
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

int main(int argc, char const *argv[])
{
    // ===== VALIDASI ARGUMENT =====
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <hostname or IP address>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // ===== DEKLARASI VARIABEL =====
    int sockfd;
    struct sockaddr_in dest_addr, from_addr;
    struct hostent *host_entry;

    // Packet buffers
    char send_packet[1024];
    char recv_packet[1024];

    // Packet components
    struct ICMPHeader *icmp_header;
    struct IPv4Header *ipv4_header;
    char *data_payload;

    // Data yang akan dikirim
    char packet_data[] = "Hello im from pinging!";
    int data_size = strlen(packet_data);
    int packet_size = sizeof(struct ICMPHeader) + data_size;

    // Timing variables
    struct timeval start_time, end_time;
    double rtt;

    // Socket variables
    fd_set read_fds;
    struct timeval timeout;
    socklen_t from_len = sizeof(from_addr);
    int bytes_received;

    // Ping variables
    uint16_t ping_id = getpid() & 0xFFFF;
    uint16_t sequence = 1;

    // ===== RESOLVE HOSTNAME =====
    printf("Resolving hostname: %s\n", argv[1]);
    if ((host_entry = gethostbyname(argv[1])) == NULL)
    {
        fprintf(stderr, "gethostbyname() failed: %s\n", hstrerror(h_errno));
        exit(EXIT_FAILURE);
    }

    // ===== CREATE RAW SOCKET =====
    printf("Creating raw ICMP socket...\n");
    if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
    {
        perror("socket() failed");
        fprintf(stderr, "Note: Raw sockets require root privileges or CAP_NET_RAW capability\n");
        fprintf(stderr, "Try: sudo %s %s\n", argv[0], argv[1]);
        exit(EXIT_FAILURE);
    }

    // ===== SETUP DESTINATION ADDRESS =====
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr = *((struct in_addr *)host_entry->h_addr_list[0]);

    // ===== BUILD ICMP PACKET =====
    memset(send_packet, 0, sizeof(send_packet));

    // Setup pointers
    icmp_header = (struct ICMPHeader *)send_packet;
    data_payload = send_packet + sizeof(struct ICMPHeader);

    // Fill ICMP header
    icmp_header->Type = 8;                    // Echo Request
    icmp_header->Code = 0;                    // Must be 0 for Echo Request
    icmp_header->Identifier = htons(ping_id); // Process ID
    icmp_header->Sequence = htons(sequence);  // Sequence number
    icmp_header->Checksum = 0;                // Set to 0 before calculation

    // Fill data payload
    memcpy(data_payload, packet_data, data_size);

    // Calculate checksum (over header + data)
    icmp_header->Checksum = calculate_checksum(icmp_header, packet_size);

    // ===== DISPLAY INFO =====
    printf("PING %s (%s): %d data bytes\n",
           argv[1], inet_ntoa(dest_addr.sin_addr), data_size);
    printf("Packet size: %d bytes (ICMP header: %lu + Data: %d)\n",
           packet_size, sizeof(struct ICMPHeader), data_size);
    printf("Ping ID: %u, Sequence: %u\n", ping_id, sequence);
    printf("Data: \"%s\"\n\n", packet_data);
    printf("Data payload (string): \"%s\"\n", data_payload);

    // ===== SEND PING =====
    printf("Sending ping...\n");
    gettimeofday(&start_time, NULL);

    if (sendto(sockfd, send_packet, packet_size, 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
    {
        perror("sendto() failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // ===== WAIT FOR REPLY =====
    printf("Waiting for reply...\n");

    // Setup timeout (5 seconds)
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);

    if (select_result < 0)
    {
        perror("select() failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    else if (select_result == 0)
    {
        printf("Timeout: No reply received within 5 seconds\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // ===== RECEIVE REPLY =====
    bytes_received = recvfrom(sockfd, recv_packet, sizeof(recv_packet), 0,
                              (struct sockaddr *)&from_addr, &from_len);
    gettimeofday(&end_time, NULL);

    if (bytes_received < 0)
    {
        perror("recvfrom() failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // ===== CALCULATE RTT =====
    rtt = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
          (end_time.tv_usec - start_time.tv_usec) / 1000.0;

    // ===== PARSE REPLY =====
    // IP header is included in raw socket receive
    ipv4_header = (struct IPv4Header *)recv_packet;
    int ip_header_length = (ipv4_header->IHL) * 4;

    // ICMP header starts after IP header
    struct ICMPHeader *recv_icmp = (struct ICMPHeader *)(recv_packet + ip_header_length);
    char *recv_data = recv_packet + ip_header_length + sizeof(struct ICMPHeader);

    // ===== DISPLAY RESULTS =====
    printf("\n=== PING REPLY RECEIVED ===\n");
    printf("From: %s\n", inet_ntoa(from_addr.sin_addr));
    printf("Bytes received: %d\n", bytes_received);
    printf("IP header length: %d bytes\n", ip_header_length);
    printf("ICMP Type: %u (should be 0 for Echo Reply)\n", recv_icmp->Type);
    printf("ICMP Code: %u\n", recv_icmp->Code);
    printf("ICMP ID: %u (sent: %u)\n", ntohs(recv_icmp->Identifier), ping_id);
    printf("ICMP Sequence: %u (sent: %u)\n", ntohs(recv_icmp->Sequence), sequence);
    printf("TTL: %u\n", ipv4_header->TTL);
    printf("Round Trip Time: %.3f ms\n", rtt);

    // Show received data (first 50 chars)
    int recv_data_size = bytes_received - ip_header_length - sizeof(struct ICMPHeader);
    if (recv_data_size > 0)
    {
        printf("Received data (%d bytes): \"", recv_data_size);
        for (int i = 0; i < recv_data_size && i < 50; i++)
        {
            if (recv_data[i] >= 32 && recv_data[i] <= 126)
                printf("%c", recv_data[i]);
            else
                printf(".");
        }
        if (recv_data_size > 50)
            printf("...");
        printf("\"\n");
    }

    // ===== VERIFY REPLY =====
    if (recv_icmp->Type == 0 &&
        ntohs(recv_icmp->Identifier) == ping_id &&
        ntohs(recv_icmp->Sequence) == sequence)
    {
        printf("\n✓ PING SUCCESSFUL!\n");
        printf("64 bytes from %s: icmp_seq=%u ttl=%u time=%.3f ms\n",
               inet_ntoa(from_addr.sin_addr), sequence, ipv4_header->TTL, rtt);
    }
    else
    {
        printf("\n✗ Received packet but ID/sequence doesn't match\n");
    }

    // ===== CLEANUP =====
    close(sockfd);
    return 0;
}

# i learned bit operation
