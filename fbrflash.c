//   Утилита для чтения флешки модемов на платформе BALONG, находящихся в режиме fastboot.
//
//
#include <stdio.h>
#ifndef WIN32
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#else
#include <windows.h>
#include "wingetopt.h"
#include "printf.h"
#endif


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

//============== Параметры флешки ==================
// Размер страницы данной флешки
uint32_t pagesize=2048;
// Размер OOB 
uint32_t oobsize=64;
// Число страниц на блок
uint32_t ppb=64;

#ifndef WIN32
int siofd;

char databuf[0x3000*65];

libusb_context* ctx=0;
libusb_device_handle* udev=0;
unsigned char EP_out = 0x81; // выходной EP - для приема данных от устройства
unsigned char EP_in  = 0x01; // входной EP - для передачит данных устройству
int upid=0x36dd;                  // PID устройства для режима libusb
#else
static HANDLE hSerial;
#endif


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
//  Отправка команды и получение ответа
//
// * возвращает размер полученного ответа
//****************************************************************
int sendcmd(char* cmdbuf, char* resbuf) {

int dlen;

#ifndef WIN32
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
  usleep(1000);
  // прием данных
  res=libusb_bulk_transfer(udev, EP_out, resbuf, 0x2000, &dlen, 10800);
  if (res<0) {
    printf("\n Ошибка приема данных в режиме libusb: %s\n",libusb_error_name(res));
    return 0;
  }
}  
#else
DWORD bytes_written = 0;
//BOOL res;

WriteFile(hSerial, cmdbuf, strlen(cmdbuf), &bytes_written, NULL);
FlushFileBuffers(hSerial);

//Sleep(2);

dlen = 0;
/*res = */ReadFile(hSerial, resbuf, 0x1000, (LPDWORD)&dlen, NULL);
//res = GetLastError();
#endif

return dlen; 
}

//*****************************************
//*  Чтение страницы флешки 
//*****************************************
int readpage(int adr, char* buf) {

char cmdbuf[100];
uint32_t res;

sprintf(cmdbuf,"oem nanddump:%x:840:40",adr);
// sprintf(cmdbuf,"oem nanddump:%x:%x:%x",adr,pagesize+oobsize,oobsize);
// sprintf(cmdbuf,"oem pagenanddump:0:%x:%x",adr,pagesize+oobsize);
res=sendcmd(cmdbuf,buf);
// printf("\n ----res = %i----\n",res);
// usleep(800);
// dump(buf,res);
//   res=4096;
return res;
  
}

//*****************************************
//*  Чтение блока флешки 
//*
//*  oobmode=0 - чтение data
//*  oobmode=1 - чтение data+oob
//*  oobmode=2 - чтение data+yaffs2 tag
//*****************************************
int readblock(int blk, char* databuf, int oobmode) {
  
int i;
char allbuf[0x2000];

for(i=0;i<ppb;i++) {
  if (readpage(blk*pagesize*ppb+pagesize*i,allbuf) != (pagesize+oobsize)) return 0;
  switch (oobmode) {
    case 0:   // data
      memcpy(databuf+pagesize*i,allbuf,pagesize);
      break;
    case 1:  // data+oob
      memcpy(databuf+(pagesize+oobsize)*i,allbuf,pagesize+oobsize);
      break;
    case 2:  // data+tag 
      memcpy(databuf+(pagesize+16)*i,allbuf,pagesize);    // data
      memcpy(databuf+(pagesize+16)*i+pagesize,allbuf+pagesize+0x20,16); //tag
      break;
  }    
}
return 1;
}

//********************************************************
//* Определение больших флешек (со страницей 4К)
//*
//* возвращает:
//*   0 - ошибка обработки команды
//*   1 - команда обработана и размер флешки определен
//********************************************************
int32_t detect_flash() {
  
uint8_t resbuf[100];
uint32_t res;

res=sendcmd("getvar:pagesize",resbuf);
if (res == 0) return 0;
if (strncmp(resbuf,"OKAY2048",8) == 0) {
  // флешка со страницей 2048 байт
  pagesize=2048;
  oobsize=64;
  ppb=64;
  return 1;
}  

if (strncmp(resbuf,"OKAY4096",8) == 0) {
  // флешка со страницей 2048 байт
  pagesize=4096;
  oobsize=0;
  ppb=64;
  return 1;
}  
printf("\n Команда getvar:pagesize возвратила ошибку");
return 0;
}


//********************************************************
//* Поиск таблицы разделов внутри m3boot
//********************************************************
int locate_ptable(uint8_t* ptbuf) {

int blk,i;

for(blk=2;blk<16;blk++) {
  if (readblock(blk,databuf,0) != 1) continue; // ошибка чтения
  for (i=0;i<pagesize*ppb;i++) {
    if (memcmp(databuf+i,"pTableHead\x00\x00",12) == 0) goto found;
  }
}  
return 0; // не нашли
found:
// dump(databuf+i,1024);
// нашли
memcpy(ptbuf,databuf+i+0x30,2048);
return 1;
}


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

void main(int argc, char* argv[]) {

#ifndef WIN32
struct termios sioparm;
#else
char device[20] = "\\\\.\\COM";
DCB dcbSerialParams = {0};
COMMTIMEOUTS CommTimeouts;
#endif
FILE* out;
char filename[100];
#ifndef WIN32
char sioname[50]="/dev/ttyUSB0";
#else
char sioname[50]="";
#endif
unsigned int startblk;
unsigned int len;
unsigned int blk;
int pnum;
unsigned int opt;
int i,j,skipflag;

unsigned int mflag=0,oflag=0,rflag=0,yflag=0,oobmode,tflag=0, nflag=0;
unsigned int pnums[50];  // список разделов для чтения
unsigned int pncount=0;  // число разделов для чтения
int blklen;
int usbkdriver=0;        // признак занятости usb-устройства драйвером ядра

// имя файла замещающей таблицы разделов
char ptfile[200];
FILE* pt;

/////////////////////////////
// pagesize=4096;
// oobsize=0;
////////////////////////////
// Расширения выходных файлов 
char* extlist[3]={"bin","oob","yaffs2"};

while ((opt = getopt(argc, argv, "nu:p:mof:r:hyt:")) != -1) {
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
-n       - показать параметры nand flash\n\
-t <file> - взять таблицу разделов из указанного файла вместо чтения из модема\n\
-o       - чтение с OOB (в формате 2048+64), без ключа - только данные\n\
-y       - чтение с тегом yaffs2 в формате 2048+16\n\
  -- Выбор режимов чтения --- \n\
-f #     - чтение раздела с указанным номером, ключ можно указать несколько раз\n\
-r start[:len] - прочитать len блоков с блока start (по умолчанию len=1)\n\
без ключа -r и -f - чтение всех разделов модема\n\
\n",argv[0]);
    return;

   case 'p':
#ifndef WIN32
    upid=0; 
#endif
    strcpy(sioname,optarg);
    break;

   case 'u':
#ifndef WIN32
    sscanf(optarg,"%x",&upid); 
    break;
#else
    printf("\n Ключ -u не поддерживается!\n");
    return;
#endif
    
   case 'm':
    mflag=1;
    break;
    
   case 'n':
    nflag=1;
    break;
    
   case 'o':
    oflag=1;
    break;
    
   case 'y':
    yflag=1;
    break;
   
   case 't':
     tflag=1;
     strcpy(ptfile,optarg);
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

if (oflag && yflag) {
  printf("\n Ключи -y и -o несовместимы\n");
  return;
}  

// полный размер блока
if (oflag) {
  blklen=(pagesize+oobsize)*ppb;           // полный oob 
  oobmode=1;
}  
else if (yflag) {
  blklen=(pagesize+16)*ppb;      // yaffs2 тег
  oobmode=2;
}  
else {
  blklen=pagesize*ppb;          // только данные
  oobmode=0;
}  

// настройка интерфейса

#ifndef WIN32
if (upid == 0) {
#endif
#ifdef WIN32
    if (*sioname == '\0')
    {
       printf("\n - Последовательный порт не задан\n"); 
       return; 
    }
#endif

#ifndef WIN32
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
 sioparm.c_cc[VTIME]=20;  // таймаут 
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
     printf("\n Невозможно отсоединить устройство от ядра:  %s\n",libusb_error_name(i));
     return;
   }
  } 
  libusb_set_configuration(udev,1);  
  i=libusb_claim_interface(udev,0);
  if (i<0) {
     printf("\n Невозможно захватить интерфейс 0 устройства: %s\n",libusb_error_name(i));
     return;
  }
}
#else
strcat(device, sioname);
    
hSerial = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
if (hSerial == INVALID_HANDLE_VALUE)
{
    printf("\n - Последовательный порт COM%s не открывается\n", sioname); 
    return;
}

ZeroMemory(&dcbSerialParams, sizeof(dcbSerialParams));
dcbSerialParams.DCBlength=sizeof(dcbSerialParams);
dcbSerialParams.BaudRate = CBR_115200;
dcbSerialParams.ByteSize = 8;
dcbSerialParams.StopBits = ONESTOPBIT;
dcbSerialParams.Parity = NOPARITY;
dcbSerialParams.fBinary = TRUE;
dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
if (!SetCommState(hSerial, &dcbSerialParams))
{
    printf("\n - Ошибка при инициализации COM-порта\n", sioname); 
    CloseHandle(hSerial);
    return;
}

CommTimeouts.ReadIntervalTimeout = 2/*MAXDWORD*/;
CommTimeouts.ReadTotalTimeoutConstant = 500;
CommTimeouts.ReadTotalTimeoutMultiplier = 0;
CommTimeouts.WriteTotalTimeoutConstant = 0;
CommTimeouts.WriteTotalTimeoutMultiplier = 0;
if (!SetCommTimeouts(hSerial, &CommTimeouts))
{
    printf("\n - Ошибка при инициализации COM-порта\n", sioname); 
    CloseHandle(hSerial);
    return;
}
#endif

//----------------------------------------------------------------
// Определяем параметры флешки
if (!detect_flash()) {
  printf("\n Невозможно определить параметры nand flash\n");
  return;
}  

if (nflag) {
  printf("\n Параметры NAND Flash:\n\
  * Размер страницы: %i байт\n\
  * Размер ООВ:      %i байт\n\
  * Страниц на блок: %i\n\
  * Размер блока:    %i байт\n",
    pagesize,oobsize,ppb,pagesize*ppb);
  
  return;
}  

//----------------------------------------------------------------
// режим абсолютного чтения
if (rflag) {
 printf("\n");	 
 sprintf(filename,"blk%04x.%s",startblk,extlist[oobmode]);
 out=fopen(filename,"wb");
 if (out == 0) {
   printf("\n Ошибка открытия выходного файла %s\n",filename);
   return;
 }
 for (blk=startblk;blk<startblk+len;blk++) {
  printf("\r Блок %04x",blk); fflush(stdout);
  if (!readblock(blk,databuf,oobmode)) {
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


if (!tflag) {
  // таблица разделов с флешки
 if (!readblock(0,databuf,0)) memcpy(ptable,databuf+0x1f830,0x7c0);
 else {
  printf("\nТаблица разделов не найдена в разделе m3boot, ищем в fastboot...");
  fflush(stdout);
  if (locate_ptable((uint8_t*)&ptable)) printf("таблица найдена");
  else {
   printf("\n! Невозможно найти таблицу разделов"); 
   return;
  }  
 }
 
} // !tflag
else {
  // таблица разделов из файла
  pt=fopen(ptfile,"rb");
  if (pt == 0) {
    printf("\n ошибка открытия файла таблицы разделов %s",ptfile);
    return;
  }
  fread(databuf,0x800,1,pt);
  fclose(pt);
  memcpy(ptable,databuf+0x30,0x7c0);
}  
  
if (mflag) printf("\n ## ----- NAME ----- start  len  loadsize loadaddr  entry    flags    type     count\n------------------------------------------------------------------------------------------");
for(pnum=0;
   (ptable[pnum].name[0] != 0) &&
   (strcmp(ptable[pnum].name,"T") != 0);
   pnum++) {
   ptable[pnum].start/=0x20000;
   ptable[pnum].length/=0x20000;

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
  if (!tflag) {
    out=fopen("ptable.bin","wb");
    fwrite(databuf+0x1f800,1,0x800,out);
    fclose(out);
  }
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
 sprintf(filename,"%02i-%s.%s",i,ptable[i].name,extlist[oobmode]);
 out=fopen(filename,"wb");
 for (blk=ptable[i].start;blk<ptable[i].start+ptable[i].length;blk++) {
  printf("\r Раздел %s  Блок %04x",ptable[i].name,blk); fflush(stdout);
  if (!readblock(blk,databuf,oobmode)) {
    printf(" - ошибка чтения");
    break;
  }  
  fwrite(databuf,1,blklen,out);
 }
 fclose(out);
} 

#ifndef WIN32
// деинициализация libusb
if (upid != 0) {
  libusb_release_interface(udev,0);
  if (usbkdriver) libusb_attach_kernel_driver(udev,0);
  libusb_close(udev);
  libusb_exit(ctx);
}  
#endif

printf("\n\n");  
}
 
