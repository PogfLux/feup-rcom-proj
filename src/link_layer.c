// Link layer protocol implementation

#include "link_layer.h"
#include "utils.h"
#include <fcntl.h>
#include <termios.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} SET_STATES;

enum {
    INFO_START,
    INFO_FLAG_RCV,
    INFO_A_RCV,
    INFO_C_RCV,
    BCC1_OK,
    READ_DATA,
    EXPECT_BSTUFF,
    INFO_STOP
} INFO_STATES;

int super_state = START;
int info_state = INFO_START;

bool received_correct_message = false;
bool received_reject = false;
bool packet_switch = false;
unsigned char last_char;

bool reading_closing_UA = false;

int packets_sent = 0;
int packets_read = 0;

int current_fd;
int current_retries;
LinkLayerRole current_role;


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0) {
        perror(connectionParameters.serialPort);
        return -1;
    }

    struct termios oldter;
    struct termios newter;

    if (tcgetattr(fd, &oldter) == -1) {
        perror("Can't fetch port settings.\n");
        return -1;
    }

    memset(&newter, 0, sizeof(newter));

    newter.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newter.c_iflag = IGNPAR;
    newter.c_oflag = 0;

    newter.c_lflag = 0;
    newter.c_cc[VTIME] = connectionParameters.timeout * 10; // 4 seconds by default
    newter.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newter) == -1) {
        perror("Error setting termios struct.\n");
        return -1;
    }

    printf("\n\nNew termios structure set\n");

    current_fd = fd;
    current_retries = connectionParameters.nRetransmissions;
    current_role = connectionParameters.role;

    if (connectionParameters.role == LlTx) {
        
        unsigned char sender_block[5];
        unsigned char read_buf;
        sender_block_create(sender_block);

        write(fd, sender_block, 5);
        
        printf("\nSent SET block, waiting for UA response\n");

        int tries = 0;

        while (!received_correct_message) {

            if (read(fd, &read_buf, 1) == 0) {

                if (tries >= current_retries) {
                    fprintf(stderr, "Failed to receive UA message, connection timed out...\n");
                    return -1;
                }

                printf("Failed to read UA frame on llopen(), retrying...\n");
                tries++;
                write(fd, sender_block, 5);
                continue;

            }

            ua_state_machine(read_buf);

        }
        
        fprintf(stdout, "Got back UA block, connection established...\n");
        return 0;

    } else {

        unsigned char receiver_block[5];
        unsigned char read_buf;
        receiver_block_create(receiver_block);

        printf("\nBeginning read cycle...\n");

        int tries = 0;

        while (!received_correct_message) {

            if (read(fd, &read_buf, 1) <= 0) {

                if (tries >= current_retries) {
                    fprintf(stderr, "Failed to receive SET message, connection timed out...\n");
                    return -1;
                }

                printf("Failed to read SET frame on llopen(), retrying...\n");
                tries++;
                continue;

            }

            set_state_machine(read_buf);

        }

        write(fd, receiver_block, 5);

        fprintf(stdout, "Received correct SET block and returned UA...\n\n");
        return 0;

    }

    return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char* buf, int bufSize)
{

    unsigned char bcc2 = buf[0];

    for (int i = 1; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }

    Array pre_stuff_packet;
    Array stuffed_packet;
    
    init_array(&pre_stuff_packet, bufSize);
    init_array(&stuffed_packet, bufSize * 2);
    memcpy(pre_stuff_packet.array, buf, bufSize);
    pre_stuff_packet.used = bufSize; 

    insert_array(&pre_stuff_packet, bcc2);

    bstuff(&pre_stuff_packet, &stuffed_packet);
    attach_info_frame(&stuffed_packet, packet_switch);

    int written_bytes = write(current_fd, stuffed_packet.array, stuffed_packet.used);

    unsigned char read_buf;
    int tries = 0;
    super_state = START;
    received_correct_message = false;    


    while (!received_correct_message) {


        if (read(current_fd, &read_buf, 1) <= 0) {


            if (tries >= current_retries) {
                fprintf(stderr, "Lost connection, not getting response from receiver...\n");
                return -1;
            }

            printf("Failed to read response frame on llwrite(), retrying...\n");
            tries++;
            written_bytes = write(current_fd, stuffed_packet.array, stuffed_packet.used);
            continue;
        
        }


        if (llwrite_state_machine(read_buf) == -1) {
            tries++;

            if (tries >= current_retries) {
                fprintf(stderr, "Received 3 rejects in a row, leaving...\n");
                return -1;
            }

            written_bytes = write(current_fd, stuffed_packet.array, stuffed_packet.used);
        }
    
    }

    if (packet_switch) packet_switch = false;
    else packet_switch = true;

    packets_sent++;

    return written_bytes;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    Array packet_array;
    init_array(&packet_array, 1);
    free(packet_array.array);
    packet_array.size = STD_BUFF_SIZE*2;
    packet_array.array = packet;

    unsigned char read_buf;
    unsigned char response[5];
    int tries = 0;
    int bytes_read = 0;
    info_state = INFO_START;
    received_correct_message = false;

    bool rejected = true;

    while (rejected) {

        while (!received_correct_message) {

            int curr_read = read(current_fd, &read_buf, 1);
            
            if (curr_read <= 0) {

                if (tries >= current_retries) {
                    fprintf(stderr, "Lost connection, not getting data from transmitter...\n");
                    return -1;
                }
                printf("Failed to read information frame on llread(), retrying...\n");
                tries++;
                continue;

            }

            if (info_state == READ_DATA) bytes_read += curr_read;
            llread_state_machine(read_buf, &packet_array);

        }

        bytes_read--; // cheeky little bcc2 innit ;)

        unsigned char supposed_bcc2 = packet_array.array[0];
        for (int i = 1; i < packet_array.used; i++) {
            supposed_bcc2 ^= packet_array.array[i];
        }

        if (supposed_bcc2 != last_char) {

            response_block_create(response, REJ_C, packet_switch);
            write(current_fd, response, 5);
            tries++;
            received_correct_message = false;
            info_state = INFO_START;
            memset(packet_array.array, 0, packet_array.used);
            packet_array.used = 0; // rewriting data

        } else rejected = false;
    }

    packets_read++;
    response_block_create(response, RR_C, packet_switch);
    write(current_fd, response, 5);

    if (packet_switch) packet_switch = false;
    else packet_switch = true;

    return bytes_read;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    if (current_role == LlTx) {

        unsigned char read_buf;
        unsigned char block[5];
        command_block_create(block, DISC_C);

        write(current_fd, block, 5);
        
        printf("\nSent DISC block, waiting for response...\n");

        int tries = 0;
        received_correct_message = false;
        super_state = START;
        
        while (!received_correct_message) {

            int read_bytes = read(current_fd, &read_buf, 1);
            if (read_bytes <= 0) {

                if (tries >= current_retries) {
                    fprintf(stderr, "Failed to receive DISC message, connection timed out...\n");
                    return -1;
                }

                printf("Failed to read DISC frame on llclose(), retrying...\n");
                tries++;
                write(current_fd, block, 5);
                continue;

            }

            llclosetx_state_machine(read_buf);

        }
        
        fprintf(stdout, "Got back DISC block, sending UA, file transfer successful!\n\n");
        command_block_create(block, UA_C);
        write(current_fd, block, 5);

        if (showStatistics) {
            printf("Packets sent: %d\n", packets_sent);
        }

        return 0;

    } else {

        unsigned char read_buf;
        int tries = 0;
        received_correct_message = false;
        super_state = START;

        while (!received_correct_message) {

            int read_bytes = read(current_fd, &read_buf, 1);
            if (read_bytes == 0) {

                if (tries >= current_retries) {
                    fprintf(stderr, "Failed to receive any DISC block after trying to close connection...\n");
                    return -1;
                }

                printf("Failed to read DISC frame on llclose(), retrying...\n");
                tries++;
                continue;

            }

            llcloserx_state_machine(read_buf);

        }

        unsigned char block[5];
        command_block_create(block, DISC_C);

        fprintf(stdout, "Sending DISC and awaiting UA response\n");
        write(current_fd, block, 5);
        tries = 0;
        received_correct_message = false;
        super_state = START;

        while (!received_correct_message) {

            if (read(current_fd, &read_buf, 1) == 0) {

                if (tries >= current_retries) {
                    fprintf(stderr, "Failed to receive UA response on closing...\n");
                    return -1;
                }

                printf("Failed to read UA frame on llclose(), retrying...\n");
                tries++;
                continue;

            }

            llcloserx_state_machine(read_buf);

        }

        fprintf(stdout, "Received UA response from transmitter, file transfer successful!\n\n");
        if (showStatistics) printf("Packets read: %d\n", packets_read);

        return 0;

    }
}


////////////////////////////////////////////////
// UTILITY FUNCTIONS
////////////////////////////////////////////////

void set_state_machine(unsigned char super_frame) {
    switch (super_state) {
        case START: {
            if (super_frame == FLAG) super_state = FLAG_RCV;
            break;
        }
        case FLAG_RCV: {
            if (super_frame == SET_A) super_state = A_RCV;
            else if (super_frame != FLAG) super_state = START;
            break;
        }
        case A_RCV: {
            if (super_frame == SET_C) super_state = C_RCV;
            else if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case C_RCV: {
            if (super_frame == (SET_A ^ SET_C)) super_state = BCC_OK;
            else if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case BCC_OK: {
            if (super_frame == FLAG) {
                received_correct_message = true;
                super_state = STOP;
            }
            else super_state = START;
            break;
        }
        case STOP: {
            break;
        }
    }
}

void ua_state_machine(unsigned char super_frame) {
    switch (super_state) {
        case START: {
            if (super_frame == FLAG) super_state = FLAG_RCV;
            break;
        }
        case FLAG_RCV: {
            if (super_frame == UA_A) super_state = A_RCV;
            else if (super_frame != FLAG) super_state = START;
            break;
        }
        case A_RCV: {
            if (super_frame == UA_C) super_state = C_RCV;
            else if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case C_RCV: {
            if (super_frame == (UA_A ^ UA_C)) super_state = BCC_OK;
            else if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case BCC_OK: {
            if (super_frame == FLAG) {
                received_correct_message = true;
                super_state = STOP;
            }
            else super_state = START;
            break;
        }
        case STOP: {
            break;
        }
    }
}

int llwrite_state_machine(unsigned char super_frame) {
    
    switch (super_state) {
        case START: {
            if (super_frame == FLAG) super_state = FLAG_RCV;
            break;
        }
        case FLAG_RCV: {
            if (super_frame == SET_A) super_state = A_RCV;
            else if (super_frame != FLAG) super_state = START;
            break;
        }
        case A_RCV: {
            if (packet_switch) {
                if (super_frame == RR_C || super_frame == REJ_C) super_state = C_RCV; 
                break;
            } else {
                if (super_frame == (RR_C | BIT(7)) || super_frame == (REJ_C | BIT(7))) super_state = C_RCV; 
                break;
            }

            if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case C_RCV: {
            if (packet_switch) {
                if (super_frame == (RR_C ^ SET_A)) { 
                    super_state = BCC_OK; 
                    break;
                } else if (super_frame == (REJ_C ^ SET_A)) {
                    super_state = BCC_OK;
                    received_reject = true;
                    break;
                }
            } else {

                if (super_frame == ((RR_C | BIT(7)) ^ SET_A)) {
                    super_state = BCC_OK; 
                    break;
                } else if (super_frame == ((REJ_C | BIT(7)) ^ SET_A)) {
                    super_state = BCC_OK;
                    received_reject = true;
                    break;
                }
            }
            if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case BCC_OK: {
            if (super_frame == FLAG) {

                if (received_reject) {
                    super_state = START;
                    return -1;
                }
    
                received_correct_message = true;
                super_state = STOP;
                
            }
            else super_state = START;
            break;
        }
        case STOP: {
            break;
        }
    }

    return 0;

}

void llread_state_machine(unsigned char info_frame, Array* buffer) {

    switch (info_state) {

        case INFO_START: {
            if (info_frame == FLAG) info_state = INFO_FLAG_RCV;
            break;
        }
        case INFO_FLAG_RCV: {
            if (info_frame == SET_A) info_state = INFO_A_RCV;
            else if (info_frame != FLAG) info_state = INFO_START;
            break;
        }
        case INFO_A_RCV: {
            if (packet_switch) {
                if (info_frame == (WRITE_C | BIT(6))) info_state = INFO_C_RCV;
                break;
            } else {
                if (info_frame == WRITE_C) info_state = INFO_C_RCV;
                break;
            }
            if (info_frame == FLAG) info_state = INFO_FLAG_RCV;
            else info_state = INFO_START;
            break;
        }
        case INFO_C_RCV: {
            if (packet_switch) {
                if (info_frame == (SET_A ^ (WRITE_C | BIT(6)))) info_state = BCC1_OK;
                break;
            } else {
                if (info_frame == (SET_A ^ WRITE_C)) info_state = BCC1_OK;
                break;
            }
            if (info_frame == FLAG) info_state = INFO_FLAG_RCV;
            else info_state = INFO_START;
            break;
        }
        case BCC1_OK: {
            if (info_frame == FLAG) info_state = INFO_FLAG_RCV;
            else {
                last_char = info_frame;
                info_state = READ_DATA;
            }
            break;
        }
        case READ_DATA: {
            if (info_frame == ESCAPE_FLAG) {
                info_state = EXPECT_BSTUFF;
                insert_array(buffer, last_char);
                last_char = info_frame;
            }
            else if (info_frame == FLAG) {
                received_correct_message = true;
                info_state = INFO_STOP;    
            }
            else {
                insert_array(buffer, last_char);
                last_char = info_frame;
            }
            break;
        }
        case EXPECT_BSTUFF: {
            unsigned char dstuff = bdestuff(info_frame);
            if (dstuff == 0) {
                insert_array(buffer, last_char);
                last_char = info_frame;
                break;
            }

            last_char = dstuff;
            info_state = READ_DATA;
        }
        case INFO_STOP: break;

    }

}

void llclosetx_state_machine(unsigned char super_frame) {
    
   switch (super_state) {
        case START: {
            if (super_frame == FLAG) super_state = FLAG_RCV;
            break;
        }
        case FLAG_RCV: {
            if (super_frame == SET_A) super_state = A_RCV;
            else if (super_frame != FLAG) super_state = START;
            break;
        }
        case A_RCV: {
            if (super_frame == DISC_C) super_state = C_RCV;
            else if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case C_RCV: {
            if (super_frame == (SET_A ^ DISC_C)) super_state = BCC_OK;
            else if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case BCC_OK: {
            if (super_frame == FLAG) {
                received_correct_message = true;
                super_state = STOP;
            }
            else super_state = START;
            break;
        }
        case STOP: {
            break;
        }
    }

}

void llcloserx_state_machine(unsigned char super_frame) {

    switch (super_state) {
        case START: {
            if (super_frame == FLAG) super_state = FLAG_RCV;
            break;
        }
        case FLAG_RCV: {
            if (super_frame == SET_A) super_state = A_RCV;
            else if (super_frame != FLAG) super_state = START;
            break;
        }
        case A_RCV: {
            if (reading_closing_UA) {
                if (super_frame == UA_C) super_state = C_RCV;
                break;
            } else {
                if (super_frame == DISC_C) super_state = C_RCV;
                break;
            }

            if (super_frame == FLAG) super_frame = FLAG_RCV;
            else super_state = START;
            break;
        }
        case C_RCV: {

            if (reading_closing_UA) {
                if (super_frame == (SET_A ^ UA_C)) super_state = BCC_OK;
                break;
            } else {
                if (super_frame == (SET_A ^ DISC_C)) super_state = BCC_OK;
                break;
            }
            if (super_frame == FLAG) super_state = FLAG_RCV;
            else super_state = START;
            break;
        }
        case BCC_OK: {
            if (super_frame == FLAG) {
                received_correct_message = true;
                super_state = STOP;
                reading_closing_UA = true;
            }
            else super_state = START;
            break;
        }
        case STOP: {
            break;
        }
    }

}