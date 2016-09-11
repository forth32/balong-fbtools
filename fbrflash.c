//   Утилита для чтения флешки модемов на платформе BALONG, находящихся в режиме fastboot.
//
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libusb-1.0/libusb.h>
//#include <usb.h>

int siofd;

struct {
    char name[16];
    unsigned start;
    unsigned lsize;
    unsigned length;
    unsigned type;  
    unsigned loadaddr;   
    unsigned nproperty;  // флаги раздела
    unsigned entry;      
    unsigned count;
} ptable[100];


libusb_context* ctx=0;
libusb_device_handle* udev=0;
unsigned char EP_out = 0x81; // выходной EP - для приема данных от устройства
unsigned char EP_in  = 0x01; // входной EP - для передачит данных устройству
int upid=0;                  // PID устройства для режима libusb


//*************************************************
//* HEX-дамп области памяти                       *
//*************************************************

void dump(unsigned char buffer[],int len) {
int i,j;
unsigned char ch;

printf("\n");
for (i=0;i<len;i+=16) {
  printf("%04x: ",i);
  for (j=0;j<16;j++){
   if ((i+j) < len) printf("%02x ",buffer[i+j]&0xff);
   else printf("   ");}
  printf(" *");
  for (j=0;j<16;j++) {
   if ((i+j) < len) {
    // преобразование байта для символьного отображения
    ch=buffer[i+j];
    if ((ch < 0x20)||((ch > 0x7e)&&(ch<0xc0))) putchar('.');
    else putchar(ch);
   } 
   // заполнение пробелами для неполных строк 
   else printf(" ");
  }
  printf("*\n");
 }
}


//****************************************************************
// отправка команды и получение ответа
//
//  возвращает 0 - ответ ERROR
//             1 - ответ ОК 
//****************************************************************
int sendcmd(char* cmdbuf, char* resbuf) {

int dlen;
int res;

if (upid == 0) {
  // режим SIO
  tcflush(siofd,TCIOFLUSH);  // очистка выходного буфера
  write(siofd,cmdbuf,strlen(cmdbuf));  // отсылка команды
  usleep(600);
  dlen=read(siofd,resbuf,80960);   // ответ команды
}
else {
  // режим libusb
  // отправляем команду
  res=libusb_bulk_transfer(udev, EP_in, cmdbuf, strlen(cmdbuf), &dlen, 100);  
  if (res<0) {
    printf("\n Ошибка передачи команды в режиме libusb: %s\n",libusb_error_name(res));
    return 0;
  }
  usleep(600);
  // прием данных
  res=libusb_bulk_transfer(udev, EP_out, resbuf, 0x2000, &dlen, 100);
  if (res<0) {
    printf("\n Ошибка приема данных в режиме libusb: %s\n",libusb_error_name(res));
    return 0;
  }
}  
return dlen; 
}

//*****************************************
//*  Чтение страницы флешки 
//*****************************************
int readpage(int adr, char* buf) {

char cmdbuf[100];
  
sprintf(cmdbuf,"oem nanddump:%x:840:40",adr);
return sendcmd(cmdbuf,buf);
}

//*****************************************
//*  Чтение блока флешки 
//*
//*  oobflag=0 - чтение data
//*  oobflag=1 - чтение data+oob
//*****************************************
int readblock(int blk, char* databuf, int oobflag) {
  
int i;
char allbuf[0x1000];

for(i=0;i<4;i++) {
  if (readpage(blk*0x2000+0x800*i,allbuf) != 0x840) return 0;
  if (oobflag) memcpy(databuf+0x840*i,allbuf,0x840);
  else         memcpy(databuf+0x800*i,allbuf,0x800);
}
return 1;
}



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

void main(int argc, char* argv[]) {

struct termios sioparm;
char cmdbuf[8192];
char* lptr;
char databuf[0x3000];
char oobuf[0x200];
int dlen;
FILE* out;
char filename[100];
char sioname[50]="/dev/ttyUSB0";
unsigned int startblk;
unsigned int len;
unsigned int blk;
int pnum;
unsigned int opt;
int i,j,skipflag;
int pflag=0;

unsigned int mflag=0,oflag=0,rflag=0;
unsigned int pnums[50];  // список разделов для чтения
unsigned int pncount=0;  // число разделов для чтения
int blklen;
int usbkdriver=0;        // признак занятости usb-устройства драйвером ядра


while ((opt = getopt(argc, argv, "u:p:mof:r:h")) != -1) {
  switch (opt) {
   case 'h': 
     
printf("\n Утилита для чтения flash модемов на balong-платформе\
\n Модем должен находиться в режиме fastboot\
\n\n\
%s [ключи] \n\n\
 Допустимы следующие ключи:\n\n\
-p <tty> - последовательный порт fastboot в режиме serial (по умолчанию /dev/ttyUSB0\n\
-u <pid> - PID USB-устройства fastboot в режиме libusb\n\
-m       - показать карту разделов\n\
-o       - чтение с OOB (в формате 2048+64), без ключа - только данные\n\
-f #     - чтение раздела с указанным номером, ключ можно указать несколько раз\n\
-r start[:len] - прочитать len блоков с блока start (по умолчанию len=1)\n\
без ключа -r и -f - чтение всех разделов модема\n\
\n",argv[0]);
    return;

   case 'p':
    if (upid != 0) {
       printf("\n Ключи -p и -u несовместимы\n");
       return;
     }  
    strcpy(sioname,optarg);
    pflag=1;
    break;

   case 'u':
    sscanf(optarg,"%x",&upid); 
    break;
    
   case 'm':
    mflag=1;
    break;
    
   case 'o':
    oflag=1;
    break;
    
   case 'f':
     if (rflag) {
       printf("\n Ключи -f и -r несовместимы\n");
       return;
     }  
     sscanf(optarg,"%i",&pnums[pncount++]);
     break;
    
   case 'r':
     if (pncount != 0) {
       printf("\n Ключи -f и -r несовместимы\n");
       return;
     }  
     rflag=1;
     sscanf(optarg,"%x",&startblk);
     if (strchr(optarg,':')) sscanf(strchr(optarg,':')+1,"%x",&len);
     else len=1;
     break;
     
   case '?':
   case ':':  
     return;
 
  }
}  

// полный размер блока
if (!oflag) blklen=0x800*4;
else 	    blklen=0x840*4;

// настройка интерфейса

if (!pflag && (upid == 0)) upid=0x36dd; // композиция fastboot по умолчанию

if (pflag) {

  // Настройка SIO
 siofd = open(sioname, O_RDWR | O_NOCTTY |O_SYNC);
 if (siofd == -1) {
   printf("\n - Последовательный порт %s не открывается\n", argv[1]);
   return;
 }

 bzero(&sioparm, sizeof(sioparm));
 sioparm.c_cflag = B19200 | CS8 | CLOCAL | CREAD;
 sioparm.c_iflag = 0;  // INPCK;
 sioparm.c_oflag = 0;
 sioparm.c_lflag = 0;
 sioparm.c_cc[VTIME]=10;  // таймаут 
 sioparm.c_cc[VMIN]=0;   // 1 байт минимального ответа

 tcsetattr(siofd, TCSANOW, &sioparm);
}
else {
  // настройка libusb
  i=libusb_init(&ctx);
  if (i <0) {
    printf("\n Ошибка инициализации подсистемы USB - %s\n",libusb_error_name(i));
    return;
  }
  libusb_set_debug(ctx,3);
  udev=libusb_open_device_with_vid_pid(ctx,0x12d1,upid);
  if (udev == 0) {
    printf("\n Ошибка открытия устройства PID=%04x\n",upid);
    return;
  }
  usbkdriver=libusb_kernel_driver_active(udev,0);
  if (usbkdriver) {
   i=libusb_detach_kernel_driver(udev,0);
   if (i < 0) {
     printf("\n Невозможно отсоединить устройство от ядра: PID=%04x - %s\n",udev,libusb_error_name(i));
     return;
   }
  } 
  libusb_set_configuration(udev,1);  
  i=libusb_claim_interface(udev,0);
  if (i<0) {
     printf("\n Невозможно захватить интерфейс 0 устройства: PID=%04x - %s\n",udev,libusb_error_name(i));
     return;
  }
}

// режим абсолютного чтения
if (rflag) {
 printf("\n");	 
 sprintf(filename,"blk%04x.%s",startblk,oflag?"oob":"bin");
 out=fopen(filename,"w");
 if (out == 0) {
   printf("\n Ошибка открытия выходного файла %s\n",filename);
   return;
 }
 for (blk=startblk;blk<startblk+len;blk++) {
  printf("\r Блок %04x",blk); fflush(stdout);
  if (!readblock(blk,databuf,oflag)) {
    printf(" - ошибка чтения");
    break;
  }  
  fwrite(databuf,1,blklen,out);
 }

 fclose(out);
 printf("\n\n");  
 return;
} 

// чтение таблицы разделов

if (!readblock(15,databuf,0)) {
  printf("\n Ошибка чтения таблицы разделов\n");
  return;
}
memcpy(ptable,databuf+0x1830,0x7c0);

if (mflag) printf("\n ## ----- NAME ----- start  len  loadsize loadaddr  entry    flags    type     count\n------------------------------------------------------------------------------------------");
for(pnum=0;
   (ptable[pnum].name[0] != 0) &&
   (strcmp(ptable[pnum].name,"T") != 0);
   pnum++) {
   ptable[pnum].start/=0x2000;
   ptable[pnum].length/=0x2000;

   if (mflag) printf("\n %02i %-16.16s %4x  %4x  %08x %08x %08x %08x %08x %08x",
	 pnum,
	 ptable[pnum].name,
	 ptable[pnum].start,
	 ptable[pnum].length,
	 ptable[pnum].lsize,
	 ptable[pnum].loadaddr,
	 ptable[pnum].type,
	 ptable[pnum].entry,
	 ptable[pnum].nproperty,
	 ptable[pnum].count);
}
printf("\n");
if (mflag) {
  out=fopen("ptable.bin","w");
  fwrite(databuf+0x1800,1,0x800,out);
  fclose(out);
  return;
}

// чтение разделов

for(i=0;i<pnum;i++) {
  // режим чтения по списку
  if (pncount != 0) {
   skipflag=1;
   if (pncount != 0) 
    for(j=0;j<pncount;j++)
      if (pnums[j] == i) {
	skipflag=0;
	break;
      }
   if (skipflag) continue;
  }
 printf("\n"); 
 sprintf(filename,"%02i-%s.%s",i,ptable[i].name,oflag?"oob":"bin");
 out=fopen(filename,"w");
 for (blk=ptable[i].start;blk<ptable[i].start+ptable[i].length;blk++) {
  printf("\r Раздел %s  Блок %04x",ptable[i].name,blk); fflush(stdout);
  if (!readblock(blk,databuf,oflag)) {
    printf(" - ошибка чтения");
    break;
  }  
  fwrite(databuf,1,blklen,out);
 }
 fclose(out);
} 
// деинициализация libusb
if (upid != 0) {
  libusb_release_interface(udev,0);
  if (usbkdriver) libusb_attach_kernel_driver(udev,0);
  libusb_close(udev);
  libusb_exit(ctx);
}  

printf("\n\n");  
}
 
