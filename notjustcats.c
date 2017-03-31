#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>

#define EMPTY 0x00
#define PADDING 0x020
#define DELETED 0xe5

//file structure for files we find
typedef struct file_t {
  char* name;
  char* path;
  int directoryLocation;
  int size;
  int deleted, directory;
  int attribute;
} file_t;

//various functions to assist the entirety of the program
void findFile(file_t* f);
void recoverFile(file_t* f);

int swapBits(int startLocation);

void searchDir(file_t* f);
void getFileName(char* str, file_t* file, int entry);

//array for FAT entries
uint8_t *map;
int fileCount = 0;
char* outputDirectory;

uint16_t newFAT[9*512];
static unsigned char mask[] = {1, 2, 4, 8, 16, 32, 64, 128};

int main(int argc, char* argv[]) {
  int fd;

  //process the inputs
  char* imgFilename = argv[1];
  outputDirectory = argv[2];

  //get the size of the file
  struct stat s;
  fd = open(imgFilename, O_RDONLY);
  fstat(fd, &s);

  map = mmap(NULL, s.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);

  //process the FAT
  uint8_t *bits = malloc(sizeof(uint8_t));
  int i = 0, j = 0;
  for(i = 512; i < 4608; ++i){
    bits[i] = map[i];
  }   

  uint16_t *fat = newFAT;

  //loop through to handle the fat while we are less then the specified size
  for(i = 512+2,  j = 0; i < 4608; i += 3, j += 2)
  {
    fat[j] = (uint16_t)((uint16_t)((bits[i-1] & 0x0f)) << 8) | ((uint16_t)bits[i-2]);
    fat[j + 1] = (uint16_t)((uint16_t)bits[i] << 4) | (bits[i-1] >> 4);
  }

  //make a directory
  if(stat(outputDirectory, &s) == -1)
    mkdir(outputDirectory, 0700);
	
    //search the root dir
    int entry;  //for the loop
    uint32_t size;

    //start at the root location, go through the sectors
    for(i = 9728; i < 9728 + (14*512); i+=512) {
      for(j = 0; j < 16; ++j) {
        entry = i+(j*32);

        file_t* file;
	char* str = (char*)malloc(11);

	if(map[entry] == EMPTY) {
          break;
	}

	//create a file & info
	file = (file_t*)malloc(sizeof(file_t));
	memcpy(&size, &map[entry + 28], sizeof(uint32_t));
	file->size = size;			
	file->directoryLocation = entry;
	file->path = "/";

	//get the file name
	getFileName(str, file, entry);
	file->name = str;

	//skip over long file names and print out the info for grader
	if(file->attribute != 0x0F){
	  if(file->deleted != 1) {
	    if(file->directory != 1) {
	      printf("FILE\tNORMAL\t%s%s\t%d\n",file->path, file->name, file->size);
	      findFile(file);
	    }else{
	      searchDir(file);
	    }
	  }else{
	    printf("FILE\tDELETED\t%s%s\t%d\n", file->path, file->name, file->size);
	    recoverFile(file);
	  }
	}

	free(str);
      }
    }

  return 0;
}

//get the file name
void getFileName(char* str, file_t* file, int entry) {
  int index;
  int k =0;

  for(index = entry; index < entry + 11; ++index) {
    if(index == (entry + 8)) { 
      str[k++] = '.';	
    }
	
    if(map[index] == PADDING) {
      //check the attribute
      unsigned char attribute = map[entry + 11];

      file->attribute = attribute;
      if((attribute & mask[4]) != 0) {
          //the file is deleted
          if(map[entry] == DELETED) {
            file->deleted = 1;
          }
            file->directory = 1;
            return;
      }
      continue;

    }else {
      str[k] = map[index];
    }

    k++;
  }

  if(map[entry] == DELETED) {
    file->deleted = 1;
    str[0] = '_';
  }
}

//search a directory for a file
void searchDir(file_t* f) {
  //go to the directory, swap the bits
  int cluster = swapBits(f->directoryLocation + 26);

  //beginning of the directory
  int sector = (31 + cluster)*512;
  uint32_t size;

  //go until empty
  while(map[sector] != EMPTY) {
    char* str = (char*)malloc(11);
    if(map[sector] != 0x2E) {

      //get all the file information again
      file_t* file = (file_t*)malloc(sizeof(file_t));
      memcpy(&size, &map[sector + 28], sizeof(uint32_t));
      file->size = size;
      file->directoryLocation = sector;
      file->path = malloc(512);
      sprintf(file->path, "%s%s%s", f->path, f->name, "/");

      getFileName(str, file, sector);
      file->name = str;

      //again print the stats for the grader
      if(file->attribute != 0x0F) {
        if(file->deleted != 1) {
          if(file->directory != 1) {
	    printf("FILE\tNORMAL\t%s%s\t%d\n",file->path, file->name, file->size);
	    findFile(file);
	  }else{
	    if(f->deleted != 1)
	      searchDir(file);
	  }
        }else{
  	  printf("FILE\tDELETED\t%s%s\t%d\n",file->path, file->name, file->size);
	  recoverFile(file);
        }
      }
    }
    sector+=32;
  }
}

//find a file
void findFile(file_t* f) {
  //initial cluster
  uint16_t cluster = swapBits(f->directoryLocation + 26);

  //create path for the file
  char* directory = malloc(strlen(outputDirectory) + strlen(f->name));
  char* extension = strrchr(f->name, '.');
  sprintf(directory, "%s/file%d%s", outputDirectory, fileCount, extension);
  fileCount++;

  //open the file
  FILE* file; 
  if((file = fopen(directory, "wb+")) == NULL) {
    printf("borked\n");
    exit(1);
  }
	
  uint16_t i=0, j=0;
  uint8_t c;
  int sector;	

  //write to the file (we just opened)
  while(cluster != 0xFFF) {
    sector = (31 + cluster)*512;
    for(i = 0; i < 512; ++i) {
      if(j >= f->size){
	return;
      }
      c = map[sector + i];
      fwrite(&c, sizeof(c), 1, file);
      j++;
    }

    cluster = newFAT[cluster];
  }
	
  fclose(file);
}

//recover a deleted file
void recoverFile(file_t* f) {
  int cluster = swapBits(f->directoryLocation + 26);

  if(f->directory != 1) {
    //get the file directory/extension 
    char* directory = malloc(strlen(outputDirectory) + strlen(f->name));
    char* extension = strrchr(f->name, '.');
    sprintf(directory, "%s/file%d%s", outputDirectory, fileCount, extension);

    //open the file
    FILE* file; 
    if((file = fopen(directory, "wb+")) == NULL) {
      printf("borked\n");
      exit(1);
    }

    fileCount++;

    //process through the FAT
    int sector;
    int i, j = 0;
    uint8_t k;

    while(newFAT[cluster] == EMPTY && j < f->size) {
      sector = (31 + cluster)*512;

      for(i = 0; i < 512; ++i) {
	if(j >= f->size) {
	  break;
	}

	k = map[sector + i];
	fwrite(&k, sizeof(k), 1, file);
	j++;
      }
			
      cluster++;
    }

    fclose(file);

  }else{
    //the deleted file is a directory, just recover files
    searchDir(f);
  }
}

//Swap bits to read cluster
int swapBits(int startLocation) {
  int i;
  uint8_t bits[2];
  int retVal;

  for(i = 0; i < 2; ++i) {
    bits[i] = map[startLocation+i];
  }

  //Swap the nibbles
  retVal = ((bits[1] & 0x0F)<<8) | (bits[0] & 0xFF);

  return retVal;
}
