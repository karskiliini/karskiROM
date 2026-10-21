/* Malicious on-disk cluster numbers must not index outside the FAT. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fat12.h"
#ifdef NANO
#include "config.h"
static uint8_t sram[131072];
void sram_read(uint32_t a,uint8_t*b,uint16_t n){ assert(a+n<=sizeof(sram));memcpy(b,sram+a,n); }
void sram_write(uint32_t a,const uint8_t*b,uint16_t n){ assert(a+n<=sizeof(sram));memcpy(sram+a,b,n); }
void uart_putdec(uint16_t n){(void)n;}
void uart_puthex8(uint8_t n){(void)n;}
void uart_puthex16(uint16_t n){(void)n;}

#endif
static uint8_t disk[FAT12_TOTAL_SECTORS][512];
int disk_read_sector(uint16_t lba, uint8_t *buf) { if(lba>=FAT12_TOTAL_SECTORS)return -1;memcpy(buf,disk[lba],512);return 0; }
int disk_write_sector(uint16_t lba,const uint8_t *buf) { if(lba>=FAT12_TOTAL_SECTORS)return -1;memcpy(disk[lba],buf,512);return 0; }
int main(void) {
 fat12_bpb_t *b=(fat12_bpb_t*)disk[0];
 b->bytes_per_sector=512;b->sectors_per_cluster=1;b->num_fats=2;b->total_sectors=2880;b->media_type=0xF0;
 disk[FAT12_FAT1_START][0]=0xF0;
 assert(fat12_mount()==FAT12_OK);
 assert(fat12_read_fat_entry(65535)==FAT12_BAD);
 fat12_write_fat_entry(65535,0x123);
 assert(fat12_read_fat_entry(2)==0);
 fat12_file_t file={0}; file.active=true;file.current_cluster=65535;file.file_size=512;
 uint8_t buf[512];
 assert(fat12_read(&file,buf,sizeof(buf))==FAT12_ERR_INVALID);
 puts("FAT12 malicious cluster bounds passed");
 return 0;
}
