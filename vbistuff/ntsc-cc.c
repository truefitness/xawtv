/* cc.c -- closed caption decoder
 * Mike Baker (mbm@linux.com)
 * (based on code by timecop@japan.co.jp)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

#ifndef X_DISPLAY_MISSING
# include <X11/X.h>
# include <X11/Xlib.h>
# include <X11/Xutil.h>
# include <X11/Xproto.h>
Display *dpy;
Window Win,Root;
char dpyname[256] = "";
GC WinGC;
GC WinGC0;
GC WinGC1;
int x;
#endif


//XDSdecode
char	info[8][25][256];
char	newinfo[8][25][256];
char	*infoptr=newinfo[0][0];
int	mode,type;
char	infochecksum;

//ccdecode
char    *ratings[] = {"(NOT RATED)","TV-Y","TV-Y7","TV-G","TV-PG","TV-14","TV-MA","(NOT RATED)"};
int     rowdata[] = {11,-1,1,2,3,4,12,13,14,15,5,6,7,8,9,10};
char	*specialchar[] = {"�","�","�","�","(TM)","�","�","o/~ ","�"," ","�","�","�","�","�","�"};
char	*modes[]={"current","future","channel","miscellaneous","public service","reserved","invalid","invalid","invalid","invalid"};
int	lastcode;
int	ccmode=1;		//cc1 or cc2
char	ccbuf[3][256];		//cc is 32 columns per row, this allows for extra characters
int	keywords=0;
char	*keyword[32];


//args (this should probably be put into a structure later)
char useraw=0;
char semirawdata=0;
char usexds=0;
char usecc=0;
char plain=0;
char usesen=0;
char debugwin=0;

char rawline=-1;

int sen;
int inval;

static int parityok(int n)	/* check parity for 2 bytes packed in n */
{
    int mask=0;
    int j, k;
    for (k = 1, j = 0; j < 7; j++) {
	  if (n & (1<<j))
	    k++;
	}
    if ((k & 1) == ((n>>7)&1))
	  mask|=0x00FF;
    for (k = 1, j = 8; j < 15; j++) {
	  if (n & (1<<j))
	    k++;
	}
    if ((k & 1) == ((n>>15)&1))
	    mask|=0xFF00;
   return mask;
}

static int decodebit(unsigned char *data, int threshold)
{
    int i, sum = 0;
    for (i = 0; i < 23; i++)
	  sum += data[i];
    return (sum > threshold*23);
}

static int decode(unsigned char *vbiline)
{
    int max[7], min[7], val[7], i, clk, tmp, sample, packedbits = 0;

    for (clk=0; clk<7; clk++)
	  max[clk] = min[clk] = val[clk] = -1;
    clk = tmp = 0;
    i=30;

    while (i < 600 && clk < 7) {	/* find and lock all 7 clocks */
	sample = vbiline[i];
	if (max[clk] < 0) { /* find maximum value before drop */
	    if (sample > 85 && sample > val[clk])
		(val[clk] = sample, tmp = i);	/* mark new maximum found */
	    else if (val[clk] - sample > 30)	/* far enough */
		(max[clk] = tmp, i = tmp + 10);
	} else { /* find minimum value after drop */
	    if (sample < 85 && sample < val[clk])
		(val[clk] = sample, tmp = i);	/* mark new minimum found */
	    else if (sample - val[clk] > 30)	/* searched far enough */
		(min[clk++] = tmp, i = tmp + 10);
	}
	i++;
    }

   i=min[6]=min[5]-max[5]+max[6];

    if (clk != 7 || vbiline[max[3]] - vbiline[min[5]] < 45)		/* failure to locate clock lead-in */
	return -1;

#ifndef X_DISPLAY_MISSING
    if (debugwin) {
      for (clk=0;clk<7;clk++)
	{
	  XDrawLine(dpy,Win,WinGC,min[clk]/2,0,min[clk]/2,128);
	  XDrawLine(dpy,Win,WinGC1,max[clk]/2,0,max[clk]/2,128);
	}
      XFlush(dpy);
    }
#endif


    /* calculate threshold */
    for (i=0,sample=0;i<7;i++)
	    sample=(sample + vbiline[min[i]] + vbiline[max[i]])/3;

    for(i=min[6];vbiline[i]<sample;i++);

#ifndef X_DISPLAY_MISSING
    if (debugwin) {
      for (clk=i;clk<i+57*18;clk+=57)
	XDrawLine(dpy,Win,WinGC,clk/2,0,clk/2,128);
      XFlush(dpy);
    }
#endif


    tmp = i+57;
    for (i = 0; i < 16; i++)
	if(decodebit(&vbiline[tmp + i * 57], sample))
	    packedbits |= 1<<i;
    return packedbits&parityok(packedbits);
} /* decode */

static int XDSdecode(int data)
{
	int b1, b2, length;

	if (data == -1)
		return -1;

	b1 = data & 0x7F;
	b2 = (data>>8) & 0x7F;

	if (b1 < 15) // start packet
	{
		mode = b1;
		type = b2;
		infochecksum = b1 + b2 + 15;
		if (mode > 8 || type > 20)
		{
//			printf("%% Unsupported mode %s(%d) [%d]\n",modes[(mode-1)>>1],mode,type);
			mode=0; type=0;
		}
		infoptr = newinfo[mode][type];
	}
	else if (b1 == 15) // eof (next byte is checksum)
	{
#if 0 //debug
		if (mode == 0)
		{
			length=infoptr - newinfo[0][0];
			infoptr[1]=0;
			printf("LEN: %d\n",length);
			for (y=0;y<length;y++)
				printf(" %03d",newinfo[0][0][y]);
			printf(" --- %s\n",newinfo[0][0]);
		}
#endif
		if (mode == 0) return 0;
		if (b2 != 128-((infochecksum%128)&0x7F)) return 0;

		length = infoptr - newinfo[mode][type];

		//don't bug the user with repeated data
		//only parse it if it's different
		if (strncmp(info[mode][type],newinfo[mode][type],length-1))
		{
			infoptr = info[mode][type];
			memcpy(info[mode][type],newinfo[mode][type],length+1);
			if (!plain)
				printf("\33[33m");
			putchar('%');
			switch ((mode<<8) + type)
			{
				case 0x0101:
					printf(" TIMECODE: %d/%02d %d:%02d",
					infoptr[3]&0x0f,infoptr[2]&0x1f,infoptr[1]&0x1f,infoptr[0]&0x3f);
				case 0x0102:
					if ((infoptr[1]&0x3f)>5)
						break;
					printf("   LENGTH: %d:%02d:%02d of %d:%02d:00",
					infoptr[3]&0x3f,infoptr[2]&0x3f,infoptr[4]&0x3f,infoptr[1]&0x3f,infoptr[0]&0x3f);
					break;
				case 0x0103:
					infoptr[length] = 0;
					printf("    TITLE: %s",infoptr);
					break;
				case 0x0105:
					printf("   RATING: %s (%d)",ratings[infoptr[0]&0x07],infoptr[0]);
					if ((infoptr[0]&0x07)>0)
					{
						if (infoptr[0]&0x20) printf(" VIOLENCE");
						if (infoptr[0]&0x10) printf(" SEXUAL");
						if (infoptr[0]&0x08) printf(" LANGUAGE");
					}
					break;
				case 0x0501:
					infoptr[length] = 0;
					printf("  NETWORK: %s",infoptr);
					break;
				case 0x0502:
					infoptr[length] = 0;
					printf("     CALL: %s",infoptr);
					break;
				case 0x0701:
					printf(" CUR.TIME: %d:%02d %d/%02d/%04d UTC",infoptr[1]&0x1F,infoptr[0]&0x3f,infoptr[3]&0x0f,infoptr[2]&0x1f,(infoptr[5]&0x3f)+1990);
					break;
				case 0x0704: //timezone
					printf(" TIMEZONE: UTC-%d",infoptr[0]&0x1f);
					break;
				case 0x0104: //program genere
					break;
				case 0x0110:
				case 0x0111:
				case 0x0112:
				case 0x0113:
				case 0x0114:
				case 0x0115:
				case 0x0116:
				case 0x0117:
					infoptr[length+1] = 0;
					printf("     DESC: %s",infoptr);
					break;
			}
			if (!plain)
				printf("\33[0m");
			putchar('\n');
			fflush(stdout);
		}
		mode = 0; type = 0;
	}
	else if( (infoptr - newinfo[mode][type]) < 250 ) // must be a data packet, check if we're in a supported mode
	{
		infoptr[0] = b1; infoptr++;
		infoptr[0] = b2; infoptr++;
		infochecksum += b1 + b2;
	}
	return 0;
}

static int webtv_check(char * buf,int len)
{
	unsigned long   sum;
	unsigned long   nwords;
	unsigned short  csum=0;
	char temp[9];
	int nbytes=0;

	while (buf[0]!='<' && len > 6)  //search for the start
	{
		buf++; len--;
	}

	if (len == 6) //failure to find start
		return 0;


	while (nbytes+6 <= len)
	{
		//look for end of object checksum, it's enclosed in []'s and there shouldn't be any [' after
		if (buf[nbytes] == '[' && buf[nbytes+5] == ']' && buf[nbytes+6] != '[')
			break;
		else
			nbytes++;
	}
	if (nbytes+6>len) //failure to find end
		return 0;

	nwords = nbytes >> 1; sum = 0;

	//add up all two byte words
	while (nwords-- > 0) {
		sum += *buf++ << 8;
		sum += *buf++;
	}
	if (nbytes & 1) {
		sum += *buf << 8;
	}
	csum = (unsigned short)(sum >> 16);
	while(csum !=0) {
		sum = csum + (sum & 0xffff);
		csum = (unsigned short)(sum >> 16);
	}
	sprintf(temp,"%04X\n",(int)~sum&0xffff);
	buf++;
	if(!strncmp(buf,temp,4))
	{
		buf[5]=0;
		if (!plain)
			printf("\33[35mWEBTV: %s\33[0m\n",buf-nbytes-1);
		else
			printf("WEBTV: %s\n",buf-nbytes-1);
		fflush(stdout);
	}
	return 0;
}

static int CCdecode(int data)
{
	int b1, b2, len, x,y;
	if (data == -1) //invalid data. flush buffers to be safe.
	{
		memset(ccbuf[1],0,255);
		memset(ccbuf[2],0,255);
		return -1;
	}
	b1 = data & 0x7f;
	b2 = (data>>8) & 0x7f;
	len = strlen(ccbuf[ccmode]);

	if (b1&0x60 && data != lastcode) // text
	{
		ccbuf[ccmode][len++]=b1;
		if (b2&0x60) ccbuf[ccmode][len++]=b2;
		if (b1 == ']' || b2 == ']')
			webtv_check(ccbuf[ccmode],len);
	}
	else if ((b1&0x10) && (b2>0x1F) && (data != lastcode)) //codes are always transmitted twice (apparently not, ignore the second occurance)
	{
		ccmode=((b1>>3)&1)+1;
		len = strlen(ccbuf[ccmode]);

		if (b2 & 0x40)	//preamble address code (row & indent)
		{
			if (len!=0)
				ccbuf[ccmode][len++]='\n';

			if (b2&0x10) //row contains indent flag
				for (x=0;x<(b2&0x0F)<<1;x++)
					ccbuf[ccmode][len++]=' ';
		}
		else
		{
			switch (b1 & 0x07)
			{
				case 0x00:	//attribute
					printf("<ATTRIBUTE %d %d>\n",b1,b2);
					fflush(stdout);
					break;
				case 0x01:	//midrow or char
					switch (b2&0x70)
					{
						case 0x20: //midrow attribute change
							switch (b2&0x0e)
							{
								case 0x00: //italics off
									if (!plain)
									  strcat(ccbuf[ccmode],"\33[0m ");
									break;
								case 0x0e: //italics on
									if (!plain)
									  strcat(ccbuf[ccmode],"\33[36m ");
									break;
							}
							if (b2&0x01) { //underline
									if (!plain)
									  strcat(ccbuf[ccmode],"\33[4m");
							} else {
							  if (!plain)
								strcat(ccbuf[ccmode],"\33[24m");
							}
							break;
						case 0x30: //special character..
							strcat(ccbuf[ccmode],specialchar[b2&0x0f]);
							break;
					}
					break;
				case 0x04:	//misc
				case 0x05:	//misc + F
//					printf("ccmode %d cmd %02x\n",ccmode,b2);
					switch (b2)
					{
						case 0x21: //backspace
							ccbuf[ccmode][len--]=0;
							break;

						/* these codes are insignifigant if we're ignoring positioning */
						case 0x25: //2 row caption
						case 0x26: //3 row caption
						case 0x27: //4 row caption
						case 0x29: //resume direct caption
						case 0x2B: //resume text display
						case 0x2C: //erase displayed memory
							break;

						case 0x2D: //carriage return
							if (ccmode==2)
								break;
						case 0x2F: //end caption + swap memory
						case 0x20: //resume caption (new caption)
							if (!strlen(ccbuf[ccmode]))
									break;
							for (x=0;x<strlen(ccbuf[ccmode]);x++)
								for (y=0;y<keywords;y++)
									if (!strncasecmp(keyword[y], ccbuf[ccmode]+x, strlen(keyword[y])))
										printf("\a");
							if (!plain)
								printf("%s\33[m\n",ccbuf[ccmode]);
							else
								printf("%s\n",ccbuf[ccmode]);
							fflush(stdout);
							/* FALL */
						case 0x2A: //text restart
						case 0x2E: //erase non-displayed memory
							memset(ccbuf[ccmode],0,255);
							break;
					}
					break;
				case 0x07:	//misc (TAB)
					for(x=0;x<(b2&0x03);x++)
						ccbuf[ccmode][len++]=' ';
					break;
			}
		}
	}
	lastcode=data;
	return 0;
}

static int print_raw(int data)
{
	int b1, b2;
	if (data == -1)
		return -1;

	// this is just null data with two parity bits
	// 100000010000000 = 0x8080
	if (data == 0x8080)
	  return -1;

	b1 = data & 0x7f;
	b2 = (data>>8) & 0x7f;

	if (!semirawdata) {
	  fprintf(stderr,"%c%c",b1,b2);
	  return 0;
	}

	// semi-raw data output begins here...

	// a control code.
	if ( ( b1 >= 0x10 ) && ( b1 <= 0x1F ) ) {
	  if ( ( b2 >= 0x20 ) && ( b2 <= 0x7F ) )
	    fprintf(stderr,"[%02X-%02X]",b1,b2);
	  return 0;
	}

	// next two rules:
	// supposed to be one printable char
	// and the other char to be discarded
	if ( ( b1 >= 0x0 ) && ( b1 <= 0xF ) ) {
	  fprintf(stderr,"(%02x)%c",b1,b2);
	  //fprintf(stderr,"%c",b2);
	  //fprintf(stderr,"%c%c",0,b2);
	  return 0;
	}
	if ( ( b2 >= 0x0 ) && ( b2 <= 0xF ) ) {
	  fprintf(stderr,"%c{%02x}",b1,b2);
	  //fprintf(stderr,"%c",b1);
	  //fprintf(stderr,"%c%c",b1,1);
	  return 0;
	}

	// just classic two chars to print.
	fprintf(stderr,"%c%c",b1,b2);

	return 0;
}

static int sentence(int data)
{
	int b1, b2;
	if (data == -1)
		return -1;
	b1 = data & 0x7f;
	b2 = (data>>8) & 0x7f;
	inval++;
	if (data==lastcode)
	{
		if (sen==1)
		{
			printf(" ");
			sen=0;
		}
		if (inval>10 && sen)
		{
			printf("\n");
		fflush(stdout);
			sen=0;
		}
		return 0;
	}
	lastcode=data;

	if (b1&96)
	{
		inval=0;
		if (sen==2 && b1!='.' && b2!='.' && b1!='!' && b2!='!' && b1!='?' && b2!='?' && b1!=')' && b2!=')')
		{
			printf("\n");
		fflush(stdout);
			sen=1;
		}
		else if (b1=='.' || b2=='.' || b1=='!' || b2=='!' || b1=='?' || b2=='?' || b1==')' || b2==')')
			sen=2;
		else
			sen=1;
		printf("%c%c",tolower(b1),tolower(b2));

	}
	return 0;
}

#ifndef X_DISPLAY_MISSING
static unsigned long getColor(char *colorName, float dim)
{
	XColor Color;
	XWindowAttributes Attributes;

	XGetWindowAttributes(dpy, Root, &Attributes);
	Color.pixel = 0;

	XParseColor (dpy, Attributes.colormap, colorName, &Color);
	Color.red=(unsigned short)(Color.red/dim);
	Color.blue=(unsigned short)(Color.blue/dim);
	Color.green=(unsigned short)(Color.green/dim);
	Color.flags=DoRed | DoGreen | DoBlue;
	XAllocColor (dpy, Attributes.colormap, &Color);

	return Color.pixel;
}
#endif

int main(int argc,char **argv)
{
   char *vbifile = "/dev/vbi0";
   unsigned char buf[65536];
   int arg;
   int args=0;
   int vbifd;
   fd_set rfds;

   int x;

   for (;;) //commandline parsing
   {
	   if (-1 == (arg = getopt(argc, argv, "?hxsckpwRr:d:")))
		   break;
	   switch (arg)
	   {
		   case '?':
		   case 'h':
			   printf("CCDecoder 0.9.1 (mbm@linux.com)\n"
				  "\tx \t decode XDS info\n"
				  "\ts \t decode by sentences \n"
				  "\tc \t decode Closed Caption (includes webtv)\n"
				  "\tk word \t keywords to break line at [broken???]\n"
				  "\tp \t plain output. do not display underline and italic\n"
				  "\tw \t open debugging window (used with -r option)\n"
				  "\tR \t semi-raw data (used with -r option)\n"
				  "\tr #\t raw dump of data (use 11 or 27 as line number)\n"
				  "\td dev \t file to open (default: /dev/vbi0)\n"
				  );
			   exit(0);
		   case 'x':
			   usexds=1; args++;
			   break;
		   case 's':
			   usesen=1; args++;
			   break;
		   case 'c':
			   usecc=1; args++;
			   break;
		   case 'k':
			   //args++;
			   keyword[keywords++]=optarg;
			   break;
		   case 'p':
			   plain=1; args++;
			   break;
		   case 'w':
			   debugwin=1;
			   break;
		   case 'R':
			   semirawdata=1;
			   break;
		   case 'r':
			   useraw=1; args++;
			   rawline=atoi(optarg);
			   break;
		   case 'd':
			   vbifile = optarg;
			   break;
	   }
   }

   if ((vbifd = open(vbifile, O_RDONLY)) < 0) {
	perror(vbifile);
	exit(1);
   }

   else if (!args)
	   exit(0);
   for (x=0;x<keywords;x++)
	printf("Keyword(%d): %s\n",x,keyword[x]);


#ifndef X_DISPLAY_MISSING
   if (debugwin) {
     dpy=XOpenDisplay(dpyname);
     Root=DefaultRootWindow(dpy);
     Win = XCreateSimpleWindow(dpy, Root, 10, 10, 1024, 128,0,0,0);
     WinGC = XCreateGC(dpy, Win, 0, NULL);
     WinGC0 = XCreateGC(dpy, Win, 0, NULL);
     WinGC1 = XCreateGC(dpy, Win, 0, NULL);
     XSetForeground(dpy, WinGC, getColor("blue",1));
     XSetForeground(dpy, WinGC0, getColor("green",1));
     XSetForeground(dpy, WinGC1, getColor("red",1));

     if (useraw)
       XMapWindow(dpy, Win);
   }
#endif


   //mainloop
   while(1){
	FD_ZERO(&rfds);
	FD_SET(vbifd, &rfds);
	select(FD_SETSIZE, &rfds, NULL, NULL, NULL);
	if (FD_ISSET(vbifd, &rfds)) {
	    if (read(vbifd, buf , 65536)!=65536)
		    printf("read error\n");
		if (useraw)
		{
#ifndef X_DISPLAY_MISSING
		  if (debugwin) {
		    XClearArea(dpy,Win,0,0,1024,128,0);
		    XDrawLine(dpy,Win,WinGC1,0,128-85/2,1024,128-85/2);
		    for (x=0;x<1024;x++)
		      if (buf[2048 * rawline+x*2]/2<128 && buf[2048 * rawline+x*2+2]/2 < 128)
			XDrawLine(dpy,Win,WinGC0,x,128-buf[2048 * rawline+x*2]/2,
				  x+1,128-buf[2048 * rawline+x*2+2]/2);
		  }
#endif
		  print_raw(decode(&buf[2048 * rawline]));
#ifndef X_DISPLAY_MISSING
		  if (debugwin) {
		    XFlush(dpy);
		    usleep(100);
		  }
#endif
		}
		if (usexds)
			XDSdecode(decode(&buf[2048 * 27]));
		if (usecc)
			CCdecode(decode(&buf[2048 * 11]));
		if (usesen)
			sentence(decode(&buf[2048 * 11]));
#ifndef X_DISPLAY_MISSING
		if (debugwin) {
			XFlush(dpy);
			usleep(100);
		}
#endif
	}
   }
   return 0;
}
