#include "utils.h"

void sender_block_create(unsigned char block[5]) {

    block[0] = FLAG;
    block[1] = SET_A;
    block[2] = SET_C;
    block[3] = block[1] ^ block[2];
    block[4] = FLAG;

}

void receiver_block_create(unsigned char block[5]) {

    block[0] = FLAG;
    block[1] = UA_A;
    block[2] = UA_C;
    block[3] = block[1] ^ block[2];
    block[4] = FLAG;

}

void command_block_create(unsigned char block[5], unsigned char command) {

    block[0] = FLAG;
    block[1] = SET_A;
    block[2] = command;
    block[3] = SET_A ^ command;
    block[4] = FLAG;

}

void response_block_create(unsigned char block[5], unsigned char response, bool packet_switch) {
    
    block[0] = FLAG;
    block[1] = SET_A;

    unsigned char C;
    if (packet_switch) C = response;
    else C = (response | BIT(7));

    block[2] = C;
    block[3] = SET_A ^ C;
    block[4] = FLAG;

}

long get_file_size(FILE* file_stream) {

    fseek(file_stream, 0L, SEEK_END);
    long file_size = ftell(file_stream);

    // set file pointer to beginning after getting size
    fseek(file_stream, 0L, SEEK_SET);

    return file_size;

}

void init_array(Array* a, size_t init_size) {

    a->array = (unsigned char *) malloc(sizeof(unsigned char) * init_size);
    a->size = init_size;
    a->used = 0;

}

void insert_array(Array* a, char element) {

    if (a->used == a->size) {
        a->size *= 2;
        a->array = (unsigned char *) realloc(a->array, sizeof(unsigned char) * a->size);
    }

    a->array[a->used++] = element;

}

void insert_long(Array* a, long element) {
    
    for (int i = 7; i == 0; i--) {
        insert_array(a, (unsigned char) (element >> i*8));
    }

}

void insert_char_pointer(Array* a, const char * element) {

    for (int i = 0; i < strlen(element); i++) {
        insert_array(a, element[i]);
    }

}

void insert_uchar_pointer(Array* a, unsigned char * element, int bytes_read) {

    for (int i = 0; i < bytes_read; i++) {
        insert_array(a, element[i]);
    }

}

void free_array(Array* a) {
    free(a->array);
    a->array = NULL;
    a->used = 0;
    a->size = 0;
}

void start_packet_create(Array* a, const char* filename, long file_size) {

    insert_array(a, START_PACKET_C);
    insert_array(a, FILENAME_PACKET_T);
    insert_array(a, (unsigned char) strlen(filename));
    insert_char_pointer(a, filename);
    insert_array(a, SIZE_PACKET_T);
    insert_array(a, sizeof(file_size));
    insert_long(a, file_size);

}

void end_packet_create(Array* a, const char* filename, long file_size) {

    insert_array(a, END_PACKET_C);
    insert_array(a, FILENAME_PACKET_T);
    insert_array(a, (unsigned char) strlen(filename));
    insert_char_pointer(a, filename);
    insert_array(a, SIZE_PACKET_T);
    insert_array(a, sizeof(file_size));
    insert_long(a, file_size);

}

void data_packet_create(Array* a, int order, int bytes_read, unsigned char buffer[]) {

    insert_array(a, DATA_PACKET_C);
    insert_array(a, order);
    insert_array(a, bytes_read / 256);
    insert_array(a, bytes_read % 256);
    insert_uchar_pointer(a, buffer, bytes_read);

}

int parse_start_packet(Array* a, Array* rcv_filename, Filesize* rcv_filesize) {

    if (a->array[0] != START_PACKET_C) {
        return -1;
    }

    // READ THE FILENAME AND WRITE IT TO rcv_filename
    int filename_size = a->array[2];
    
    for (int i = 0; i < filename_size; i++) {
        insert_array(rcv_filename, a->array[i+3]);
    }

    // READ THE FILESIZE AND WRITE IT TO rcv_filesize
    int filesize_size = a->array[filename_size+4];

    for (int i = 0; i < filesize_size; i++) {
        rcv_filesize->array[(filesize_size-1) - i] = a->array[i+filename_size+5];
    }

    return 0;
}

void create_filename(Array* s, Array* rcv_filename) {
    unsigned char* fname = rcv_filename->array;
    char* beg = strtok((char*) fname, ".");
    char* end = strtok(NULL, ".");

    char name[strlen(beg)];
    char term[strlen(end)];
    
    strcpy(name, beg);
    strcpy(term, end);
    char* str = strcat(strcat(name,"-received."), term);
    
    insert_char_pointer(s, str);
}

void get_buffer(Array* a, Array* buffer, int size) {

    for (int i = 0; i < size; i++) {
        insert_array(buffer, a->array[4+i]);
    }
    
}

void bstuff(Array* a, Array* b) {
    int N = 0;
    while (N < a->used) {

        if (a->array[N] == FLAG) {

            insert_array(b, ESCAPE_FLAG);
            insert_array(b, REPLACE_FLAG);
        
        } else if (a->array[N] == ESCAPE_FLAG) {
        
            insert_array(b, ESCAPE_FLAG);
            insert_array(b, REPLACE_ESCAPE);
        
        } else {
        
            insert_array(b, a->array[N]);
        
        }
        N++;
    }

    free_array(a);

}

unsigned char bdestuff(unsigned char a) {

    if (a == REPLACE_FLAG) {
        return FLAG;
    } else if (a == REPLACE_ESCAPE) {
        return ESCAPE_FLAG;
    }

    return 0;

}

void attach_info_frame(Array* buf, bool packet_switch) {

    Array aux;
    init_array(&aux, buf->size);
    frame_header_create(&aux, packet_switch);

    for (int i = 0; i < buf->used; i++) {
        insert_array(&aux, buf->array[i]);
    }

    insert_array(&aux, FLAG);

    buf->array = aux.array;
    buf->size = aux.size;
    buf->used = aux.used;

}

void frame_header_create(Array* packet, bool packet_switch) {

    insert_array(packet, FLAG);
    insert_array(packet, SET_A);

    unsigned char C = 0;
    if (packet_switch) C |= BIT(6);
    insert_array(packet, C);

    insert_array(packet, SET_A^C);

}
