// Application layer maotocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "utils.h"

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{

    LinkLayer link_info;

    link_info.baudRate = baudRate;
    link_info.nRetransmissions = nTries;
    link_info.timeout = timeout;
    strcpy(link_info.serialPort, serialPort);

    if (strcmp(role, "tx") == 0) {
        link_info.role = LlTx;
    } else if (strcmp(role, "rx") == 0) {
        link_info.role = LlRx;
    } else return;

    if (llopen(link_info) != 0) exit(-1);

    if (link_info.role == LlTx) {
        // READ FILE INFO

        FILE* file = fopen(filename, "r");
        if (file == 0) {
            fprintf(stderr, "Failed to open file. \n");
            exit(-1);
        }

        long file_size = get_file_size(file);

        // START_PACKET & SEND IT TO LINKLAYER [START_PACKET = C T1 L1 V1 T2 L2 V2]

        Array start;
        init_array(&start, 6);
        start_packet_create(&start, (const char *) filename, file_size);

        // CHECK IF RR
        if (llwrite(start.array, start.used) <= 0) {
            exit(-1);
        }

        free_array(&start);

        // READ AND SEND DATA

        Array packet;
        unsigned char buffer[STD_BUFF_SIZE];
        int bytes_read = 0;
        unsigned char order = 1;

        while((bytes_read = fread(buffer, sizeof(unsigned char), STD_BUFF_SIZE, file)) > 0) {

            // CREATE PACKET TO BE SENT
            init_array(&packet, bytes_read + 4);
            data_packet_create(&packet, order, bytes_read, buffer);

            if (llwrite(packet.array, packet.used) <= 0) {
                exit(-1);
            }

            order++;
            free_array(&packet);
        }

        // SEND END_PACKET

        Array end;
        init_array(&end, 6);
        end_packet_create(&end, filename, file_size);
        if (llwrite(end.array, end.used) <= 0) {
            exit(-1);
        }

        free_array(&end);

        llclose(1);

    } else if (link_info.role == LlRx) {
        // START_PACKET ARRIVED

        Array start;
        init_array(&start, 6);

        if (llread(start.array) <= 0) {
            exit(-1);
        }

        // PARSE START_PACKET

        Array rcv_filename;
        init_array(&rcv_filename, 1);
        
        Filesize rcv_filesize;
        memset(&rcv_filesize, 0, sizeof(Filesize));

        if (parse_start_packet(&start, &rcv_filename, &rcv_filesize) != 0) {
            exit(-1);
        }

        // CREATE DESTINATION FILE
        Array str;
        init_array(&str, 1);
        create_filename(&str, &rcv_filename);
        
        FILE* file = fopen((char*) str.array, "w");

        // WRITE FILE
        Array packet, buffer;
        init_array(&packet, STD_BUFF_SIZE*2);
        init_array(&buffer, STD_BUFF_SIZE);

        int end = FALSE;
        int total_size = 0;
        unsigned char order = 1;
        while (!end) {
            int read_bytes = llread(packet.array);
            if (read_bytes <= 0) {
                exit(-1);
            }

            switch (packet.array[0]) {
                case DATA_PACKET_C:
                    if (order != packet.array[1]) {
                        exit(-1);
                    }

                    order++;

                    int buf_size = packet.array[2] * 256 + packet.array[3];

                    get_buffer(&packet, &buffer, buf_size);
                    total_size += buf_size;

                    // WRITE BUFFER TO THE FILE
                    fwrite(buffer.array, sizeof(char), buffer.used, file);

                    free_array(&packet); init_array(&packet, STD_BUFF_SIZE*2);
                    free_array(&buffer); init_array(&buffer, STD_BUFF_SIZE);
                    break;

                case END_PACKET_C:
                    end = TRUE;
                    fclose(file);
                    llclose(1);
                    break;
                default:
                    exit(-1);
                    break;
            }
        }
    } else return;
}
