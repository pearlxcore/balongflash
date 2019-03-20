#include <stdio.h>
#include <stdint.h>
#ifndef WIN32
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#else
#include <windows.h>
#include "getopt.h"
#include "printf.h"
#include "buildno.h"
#endif

#include "hdlcio.h"
#include "ptable.h"
#include "flasher.h"
#include "util.h"
#include "signver.h"

// флаг ошибки структуры файла
unsigned int errflag=0;

// флаг цифровой подписи
int gflag=0;
// флаг типа прошивки
int dflag=0;

// тип прошивки из заголовка файла
int dload_id=-1;

//***********************************************
//* Таблица разделов
//***********************************************
struct ptb_t ptable[120];
int npart=0; // число разделов в таблице


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

int main(int argc, char* argv[]) {

unsigned int opt;
int res;
FILE* in;
char devname[50] = "";
unsigned int  mflag=0,eflag=0,rflag=0,sflag=0,nflag=0,kflag=0,fflag=0;
unsigned char fdir[40];   // каталог для мультифайловой прошивки

// разбор командной строки
while ((opt = getopt(argc, argv, "d:hp:mersng:kf")) != -1) {
  switch (opt) {
   case 'h': 
     
printf("\nThe utility is designed for flashing modems on a chipset Balong V7\n\n\
%s [keys] <file name to download or file directory name>\n\n\
The following keys are valid.:\n\n"
#ifndef WIN32
"-p <tty> - serial port to communicate with the bootloader (by default /dev/ttyUSB0)\n"
#else
"-p# 	 - serial port number to communicate with the bootloader (for example, -p8)\n"
"  if the -p option is not specified, the port is automatically detected\n"
#endif
"-n      - multi-file firmware mode from the specified directory\n\
-g#      - digital signature setting\n\
-gl 	 - description of parameters\n\
-gd 	 - signature autodetection ban\n\
-m       - display the firmware file card and exit\n\
-e       - parse the firmware file into sections without headers\n\
-s       - parse the firmware file into sections with headers\n\
-k       - Do not restart the modem at the end of the firmware\n\
-r       - force reboot the modem without flashing partitions\n\
-f       - flash even if there are CRC errors in the source file\n\
-d#      - installation of the type of firmware (DLOAD_ID, 0..7), -dl - list of types\n\
\n",argv[0]);
    return 0;

   case 'p':
    strcpy(devname,optarg);
    break;

   case 'm':
     mflag=1;
     break;
     
   case 'n':
     nflag=1;
     break;
     
   case 'f':
     fflag=1;
     break;
     
   case 'r':
     rflag=1;
     break;
     
   case 'k':
     kflag=1;
     break;
     
   case 'e':
     eflag=1;
     break;

   case 's':
     sflag=1;
     break;

   case 'g':
     gparm(optarg);
     break;
     
   case 'd':
     dparm(optarg);
     break;
     
   case '?':
   case ':':  
     return -1;
  }
}  
printf("Emergency Balong-chipset USB downloader, V3.0.%i, (c) forth32, 2015, GNU GPLv3\nEnglish Version V2 Compiled by pearlxcore (https://github.com/pearlxcore)]",BUILDNO);
#ifdef WIN32
printf("\nPort for Windows 32bit (c) rust3028, 2016");
#endif
printf("\n--------------------------------------------------------------------------------------------------\n");

if (eflag&sflag) {
  printf("\nThe -s and -e options are incompatible.\n");
  return -1;
}  

if (kflag&rflag) {
  printf("\nThe -k and -r options are incompatible.\n");
  return -1;
}  

if (nflag&(eflag|sflag|mflag)) {
  printf("\nThe -n switch is incompatible with the -s, -m, and -e switches.\n");
  return -1;
}  
  

// ------  перезагрузка без указания файла
//--------------------------------------------
if ((optind>=argc)&rflag) goto sio; 


// Открытие входного файла
//--------------------------------------------
if (optind>=argc) {
  if (nflag)
    printf("\n- File directory not specified\n");
  else 
    printf("\n- File name is not specified for download, use the -h switch for the hint\n");
  return -1;
}  

if (nflag) 
  // для -n - просто копируем префикс
  strncpy(fdir,argv[optind],39);
else {
  // для однофайловых операций
in=fopen(argv[optind],"rb");
if (in == 0) {
  printf("\nOpen error %s",argv[optind]);
  return -1;
}
}


// Поиск разделов внутри файла
if (!nflag) {
  findparts(in);
  show_fw_info();
}  

// Поиск файлов прошивок в указанном каталоге
else findfiles(fdir);
  
//------ Режим вывода карты файла прошивки
if (mflag) show_file_map();

// выход по ошибкам CRC
if (!fflag && errflag) {
    printf("\n\nThe input file contains errors\n");
    return -1; 
}

//------- Режим разрезания файла прошивки
if (eflag|sflag) {
  fwsplit(sflag);
  printf("\n");
  return 0;
}

sio:
//--------- Основной режим - запись прошивки
//--------------------------------------------

// Настройка SIO
open_port(devname);

// Определяем режим порта и версию dload-протокола

res=dloadversion();
if (res == -1) return -2;
if (res == 0) {
  printf("\nThe modem is already in HDLC mode.");
  goto hdlc;
}

// Если надо, отправляем команду цифровой подписи
if (gflag != -1) send_signver();

// Входим в HDLC-режим

usleep(100000);
enter_hdlc();

// Вошли в HDLC
//------------------------------
hdlc:

// получаем версию протокола и идентификатор устройства
protocol_version();
dev_ident();


printf("\n----------------------------------------------------\n");

if ((optind>=argc)&rflag) {
  // перезагрузка без указания файла
  restart_modem();
  exit(0);
}  

// Записываем всю флешку
flash_all();
printf("\n");

port_timeout(1);

// выходим из режима HDLC и перезагружаемся
if (rflag || !kflag) restart_modem();
// выход из HDLC без перезагрузки
else leave_hdlc();
} 
