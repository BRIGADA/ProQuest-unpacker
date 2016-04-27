/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: brigada
 *
 * Created on 17 апреля 2016 г., 14:46
 */

#include <cstdlib>
#include <cstdio>

#include <string>

#include "pklib/pklib.h"

#define DEFAULT_CAT_KEY "LSLIB"
#define DEFAULT_REC_KEY "GreatBlueShark!"

#pragma pack(push, 1)

struct CatalogueRecord {
    char name[14];
    unsigned int offset;
    unsigned int size;
    unsigned int compressed;
};

struct BundleHeader {
    unsigned short magic; // must be 300
    unsigned short count;
    unsigned short res[21];
};
#pragma pack(pop)

struct BundleContext {
    FILE *stream;
    BundleHeader header;
    CatalogueRecord *catalogue;
};

struct io_ctx_t {
    FILE *in;
    FILE *out;
    std::string key;
};

std::string generate_key(const char *salt) {
    std::string result(salt);

    std::string::reverse_iterator it = result.rbegin();

    char v = *it;

    for (*it = 0; it != result.rend(); ++it) {
        *it = (*it | (v << 7)) ^ 0x55;
        v >>= 1;
    }

    return result;
}

void decrypt(void *buf, unsigned int size, std::string key) {
    if (!key.empty()) {
        for (unsigned int i = 0; i < size; ++i) {
            reinterpret_cast<char *> (buf)[i] ^= key[i % key.size()];
        }
    }
}

unsigned int read_buf(char *buf, unsigned int *size, void *ctx) {
    *size = fread(buf, 1, *size, ((io_ctx_t*)ctx)->in);
    decrypt(buf, *size, ((io_ctx_t*)ctx)->key);
    return *size;
}

void write_buf(char *buf, unsigned int *size, void *ctx) {
    *size = fwrite(buf, 1, *size, ((io_ctx_t*)ctx)->out);
}

void usage() {
    printf("ProQuest tool, v1.0\n");
    printf("-------------------\n");
    printf("\n");
    printf("Usage:\n");
    printf("\tpqtool <file> - list content\n");
    printf("\tpqtool <file> <index> [outfile] - extract content by index");
}

/*
 * 
 */
int main(int argc, char** argv) {
    
    if (argc < 2) {
        return -1;
    }
    
    io_ctx_t io_ctx;
    io_ctx.in = fopen(argv[1], "rb");
    if (io_ctx.in == NULL) {
        perror("fopen");
        return -2;
    }

    BundleHeader header;    
    if(fread(&header, sizeof (header), 1, io_ctx.in) != 1) {
        printf("can't read header\n");
        return -3;
    }
    if(header.magic != 300) {
        return -4;
    }

    CatalogueRecord *catalogue = (CatalogueRecord*)calloc(header.count, sizeof(CatalogueRecord));
    
    fseek(io_ctx.in, -(sizeof (CatalogueRecord) * header.count), SEEK_END);
    fread(catalogue, sizeof (CatalogueRecord), header.count, io_ctx.in);

    std::string cat_key = generate_key(DEFAULT_CAT_KEY);
    std::string rec_key = generate_key(DEFAULT_REC_KEY);

    io_ctx.key = rec_key;
    
    for (unsigned short i = 0; i < header.count; ++i) {
        decrypt(&catalogue[i], sizeof (CatalogueRecord), cat_key);
        //        d0.decrypt(&catalogue[i], sizeof (CatalogueRecord));

        printf("Record %hu\n--------\n", i);
        printf("name: '%.14s'\n", catalogue[i].name);
        printf(" off: %u\n", catalogue[i].offset);
        printf("size: %u\n", catalogue[i].size);
        printf("flag: %08x\n\n", catalogue[i].compressed);

        fseek(io_ctx.in, catalogue[i].offset, SEEK_SET);
        
        io_ctx.out = fopen(catalogue[i].name, "w+b");

        if (catalogue[i].compressed) {
            // PKWARE comression
            void *pkware_ctx = malloc(sizeof(TDcmpStruct));
            explode(read_buf, write_buf, (char *)pkware_ctx, &io_ctx);
            free(pkware_ctx);
            
        } else {
            // no compression
            void *buf = malloc(512);
            
            unsigned int remain = catalogue[i].size;
            while(remain) {
                unsigned int bsize = remain > 512 ? 512 : remain;
                read_buf((char*)buf, &bsize, &io_ctx);
                write_buf((char*)buf, &bsize, &io_ctx);
                remain -= bsize;
            }
            
            free(buf);
        }
        
        fclose(io_ctx.out);
    }

    free(catalogue);

    fclose(io_ctx.in);

    return 0;
}

