#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define STD_BUFF_SIZE 400

#define BIT(n) (1 << (n))

#define FLAG 0x7E
#define ESCAPE_FLAG 0x7D
#define REPLACE_FLAG 0x5E
#define REPLACE_ESCAPE 0x5D
#define XOR_FLAG 0x20

#define WRITE_C 0x00

#define SET_A 0x03
#define UA_A 0x01

#define SET_C 0x03
#define DISC_C 0x11
#define UA_C 0x07
#define RR_C 0x05
#define REJ_C 0x01

typedef enum
{
    DATA_PACKET_C = 1,
    START_PACKET_C = 2,
    END_PACKET_C = 3
} PacketC;

typedef enum
{
    SIZE_PACKET_T,
    FILENAME_PACKET_T
} PacketT;

typedef struct {
    unsigned char* array;
    size_t used;
    size_t size;
} Array;

typedef union {
    unsigned char array[8];
    long filesize;
} Filesize;

void init_array(Array* a, size_t init_size);
void insert_array(Array* a, char element);
void insert_long(Array* a, long element);
void insert_char_pointer(Array* a, const char * element);
void insert_uchar_pointer(Array* a, unsigned char * element, int bytes_read);
void free_array(Array* a);

void sender_block_create(unsigned char block[5]);
void receiver_block_create(unsigned char block[5]);
void command_block_create(unsigned char block[5], unsigned char command);
void response_block_create(unsigned char block[5], unsigned char response, bool packet_switch);

void start_packet_create(Array* a, const char* filename, long file_size);
void end_packet_create(Array* a, const char* filename, long file_size);
void data_packet_create(Array* a, int order, int buff_size, unsigned char buffer[]);

int parse_start_packet(Array* a, Array* rcv_filename, Filesize* rcv_filesize);
void create_filename(Array* s, Array* rcv_filename);
void get_buffer(Array* a, Array* buffer, int size);

void set_state_machine(unsigned char super_frame);
void ua_state_machine(unsigned char super_frame);
int llwrite_state_machine(unsigned char super_frame);
void llread_state_machine(unsigned char info_frame, Array* buffer);
void llclosetx_state_machine(unsigned char super_frame);
void llcloserx_state_machine(unsigned char super_frame);

long get_file_size(FILE* file_stream);

// link layer functions

void bstuff(Array* a, Array* b);
unsigned char bdestuff(unsigned char a);

void packet_state_machine(unsigned char info_frame);

void attach_info_frame(Array* buf, bool packet_switch);
void frame_header_create(Array* buf, bool packet_switch);