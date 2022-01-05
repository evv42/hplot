//hplot: HPGL naïve plotter driver for HP plotters.
//Tested only on an HPIB 7440A with Inkscape-generated files, so:
//////////////////////////////////////////////////////////////////////////////
//  This software is provided 'AS-IS', without any express or implied       //
//  warranty.  In no event will the authors be held liable for any damages  //
//  arising from the use of this software.                                  //
//////////////////////////////////////////////////////////////////////////////
//See LICENSE file for copyright and license details.
//Copyright 2021,2022 evv42.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

//Delay in µs to send any command (see your controller doc)
#define DELAY_MIN 32000

//Global variables (too lazy to make a struct or put pointers everywhere)
//Default position for A4/Letter on 7440A;
int px = 0;
int py = 7650;
long waitpoints = 0;

void gtfo(const char* msg){perror(msg);exit(1);}
void p_usage(char* name){fprintf(stderr,"Outputs timed HPGL to stdout. Syntax: %s file.hpgl\n",name);exit(2);}


long get_file_size(char* file){
	struct stat st;
	int t = stat(file, &st);
	if(t == -1)gtfo("stat");
	return st.st_size;
}

char* open_and_bufferize(char* file, long* bs){
	//open file
	int fd = open(file,O_RDONLY,0);
	if(fd == -1)gtfo("open");
	//allocate buffer memory
	*bs = get_file_size(file);
	char* buf = malloc(*bs);
	if(buf == NULL)gtfo("malloc");
	//tranfer shit
	char* p = buf;
	int t;
	while((t = read(fd,p,64)) >0)p+=t;
	if(t == -1)gtfo("read");
	close(fd);
	return buf;
}

int pos(int a){return a>0 ? a : a*-1;}

int stoptime(char* nxs, char* nxe, char* nys, char* nye){
	char strx[8] = { 0 };
	char stry[8] = { 0 };
	memcpy(strx,nxs,nxe-nxs);
	memcpy(stry,nys,nye-nys);
	int x = atof(strx);
	int y = atof(stry);
	//Approximation of distance to avoid using math.h (We can't fail going slower)
	int dist = pos(px-x) + pos(py-y);
	px = x;
	py = y;
	//Step size * steps / pen speed µs
	return ((0.025*(double)dist)/200.0)*1000000.0;
}

//Splits long commands, unsupported by 7440A
//IN:
//PD555,222,666,444;
//OUT:
//PD555,222\n
//PD666,444\n
void split_command(char* start, char* end, FILE* out){
	char mnemonic[]={start[0],start[1]};
	char* p = start+2;

	while(p < end){
		char* nxs = p;
		char* nxe = strchr(p,',');
		char* nys = nxe+1;
		char* nye = strchr(nys,',');
		if(nye > end)nye = strchr(nys,';');

		int wait = stoptime(nxs,nxe,nys,nye);

		fwrite(mnemonic,1,2,out);
		fwrite(nxs,1,nxe-nxs+1,out);
		fwrite(nys,1,nye-nys,out);
		putc('\n',out);
		usleep(DELAY_MIN+wait);
		waitpoints += 1;
//		fprintf(stderr,"%d\n",DELAY_MIN+wait);

		p=nye+1;
	}
}

//Splits PU command, if nessesary.
//IN:
//PU42,24;
//OUT:
//PU\n
//PA42,24;
void split_penup(char* start, char* end, FILE* out){
	char mnemonic[]={start[0],start[1]};
	char* p = start+2;
	while(p < end){
		char* nxs = p;
		char* nxe = strchr(p,',');
		char* nys = nxe+1;
		char* nye = strchr(nys,';');

		if(nxe > end){
			fwrite(start,1,end-start,out);
			putc('\n',out);
			usleep(DELAY_MIN);
			return;
		}

		int wait = stoptime(nxs,nxe,nys,nye);

		fwrite(mnemonic,1,2,out);
		putc('\n',out);
		usleep(DELAY_MIN);
		waitpoints += 1;
		putc('P',out);
		putc('A',out);
		fwrite(nxs,1,nxe-nxs+1,out);
		fwrite(nys,1,nye-nys,out);
		putc('\n',out);
		usleep(DELAY_MIN+wait+DELAY_MIN);
		waitpoints += 2;
//		fprintf(stderr,"%d\n",DELAY_MIN+wait);
		p=nye+1;
	}
}

int main(int argc, char** argv){

	if(argc != 2)p_usage(argv[0]);

	FILE* out = stdout;
	FILE* info = stderr;


	fprintf(info,"hplot started. HPGL file: %s.\n",argv[1]);

	long bs = 0;
	char* buf = open_and_bufferize(argv[1],&bs);

	fprintf(info,"File opened in buffer.\n");

	char* pos = buf;

	fprintf(info,"Now plotting data.\n");


	while(pos < buf+bs-2){
		char* endcmd = strchr(pos,';');
		if(pos[0] == 'P' && pos[1] == 'D'){
			split_command(pos,endcmd,out);
		}else if(pos[0] == 'P' && pos[1] == 'A'){
			split_command(pos,endcmd,out);
		}else if(pos[0] == 'P' && pos[1] == 'U'){
			split_penup(pos,endcmd,out);
		}else if(pos[0] == 'S' && pos[1] == 'P'){
			if(pos[2] != 0){
				fprintf(info," Please put pen %c and press [ENTER]                                         \r",pos[2]);
				getchar(); // wait for ENTER
			}
		}else if(pos[0] == 'I' && pos[1] == 'N'){//7440A is slow to init.
			fwrite(pos,1,endcmd-pos,out);
			putc('\n',out);
			usleep(2200000);
		}else{
			fwrite(pos,1,endcmd-pos,out);
			putc('\n',out);
			usleep(150000);
		}
		pos = endcmd + 1;
		fprintf(info," Progress: %d bytes of %d total (%3.2f%%). Press Ctrl+C to stop.\r", pos-buf, bs, ((double)(pos-buf)/(double)bs)*100.0 );
	}
	fprintf(info,"\nPlotting done, %ld waitpoints.\n",waitpoints);
	return 0;

}
