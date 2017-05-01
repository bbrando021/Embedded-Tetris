
/* Y o u r   D e s c r i p t i o n                       */
/*                            AppBuilder Photon Code Lib */
/*                                         Version 2.03  */

/* Standard headers */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Local headers */
#include "ablibs.h"
#include "abimport.h"
#include "proto.h"
#include <time.h>
#include "globals.h"
#include <errno.h>
#include <gulliver.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/termio.h>
#include <sys/types.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <pthread.h>
#include <sys/asoundlib.h>
#include "playerState.h"

const char *RIFF_Id = "RIFF";
const char *WAVE_Id = "WAVE";
float progress = 0;
int intProgress, totalSize;
snd_pcm_t *pcm_handle;
snd_mixer_t *mixer_handle;
FILE *waveFile;
char *musicDir = "/PianoNotes/";
char *songs[] = {"Tetris.wav","piano-b.wav", "piano-bb.wav", "piano-c.wav","piano-c#.wav","piano-d.wav","piano-e.wav","piano-eb.wav","piano-f#.wav","piano-f.wav","piano-g#.wav","piano-g.wav"};
int media_index = 0;
int stopped = 0;
int playing = 0;

typedef struct {
	char tag[4];
	long length;
} RIFF_Tag;

typedef struct {
	char Riff[4];
	long Size;
	char Wave[4];
} RIFF_Header;

typedef struct {
	short FormatTag;
	short Channels;
	long SamplesPerSec;
	long AvgBytesPerSec;
	short BlockAlign;
	short BitsPerSample;
} WAVE_Header;

int err(char *msg) {
	perror(msg);
	return -1;
}

int FindTag(FILE * fp, const char *tag) {
	int retVal;
	RIFF_Tag tagBfr = { "", 0 };

	retVal = 0;

	// Keep reading until we find the tag or hit the EOF.
	while (fread((unsigned char *) &tagBfr, sizeof(tagBfr), 1, fp)) {

		// If this is our tag, set the length and break.
		if (strncmp(tag, tagBfr.tag, sizeof tagBfr.tag) == 0) {
			retVal = ENDIAN_LE32 (tagBfr.length);
			break;
		}
		// Skip ahead the specified number of bytes in the stream
		fseek(fp, tagBfr.length, SEEK_CUR);
	}

	totalSize = tagBfr.length;

	// Return the result of our operation
	return (retVal);
}

int CheckFileHeader(FILE * fp) {
	RIFF_Header riffHdr = { "", 0 };

	// Read the header and, if successful, play
	// the WAVE file.
	if (fread((unsigned char *) &riffHdr, sizeof(RIFF_Header), 1, fp) == 0)
		return 0;

	if (strncmp(riffHdr.Riff, RIFF_Id, strlen(RIFF_Id)) || strncmp(
			riffHdr.Wave, WAVE_Id, strlen(WAVE_Id)))
		return -1;

	return 0;
}
void onStopped() {
	snd_pcm_plugin_flush(pcm_handle, SND_PCM_CHANNEL_PLAYBACK);

	snd_mixer_close(mixer_handle);
	snd_pcm_close(pcm_handle);
	fclose(waveFile);
}

void playingWave() {
	stopped = 0;
	int card = -1;
	int dev = 0;
	WAVE_Header wavHdr1;
	int mSamples;
	int mSampleRate;
	int mSampleChannels;
	int mSampleBits;
	char *mSampleBfr1;
	int fragsize = -1;

	int rtn;
	snd_pcm_channel_info_t pi;
	snd_mixer_group_t group;
	snd_pcm_channel_params_t pp;
	snd_pcm_channel_setup_t setup;
	int bsize, sizeRtn, N = 0;
	fd_set rfds, wfds;

	if (card == -1) {
		if ((rtn = snd_pcm_open_preferred(&pcm_handle, &card, &dev,
				SND_PCM_OPEN_PLAYBACK)) < 0)
			return err("device open");
	} else {
		if ((rtn = snd_pcm_open(&pcm_handle, card, dev, SND_PCM_OPEN_PLAYBACK))
				< 0)
			return err("device open error");
	}

	char songName[50];
	strcpy(songName, musicDir);
	strcat(songName, songs[0]);


	//open the appropriate wav file
	if ((waveFile = fopen(songName, "r")) == 0)
		return err("file open #1");

	if (CheckFileHeader(waveFile) == -1)
		return err("CheckHdr #1");

	mSamples = FindTag(waveFile, "fmt ");
	fread(&wavHdr1, sizeof(wavHdr1), 1, waveFile);
	fseek(waveFile, (mSamples - sizeof(WAVE_Header)), SEEK_CUR);

	mSampleRate = ENDIAN_LE32 (wavHdr1.SamplesPerSec);
	mSampleChannels = ENDIAN_LE16 (wavHdr1.Channels);
	mSampleBits = ENDIAN_LE16 (wavHdr1.BitsPerSample);


	/* disabling mmap is not actually required in this example but it is included to
	 * demonstrate how it is used when it is required.
	 */
	if ((rtn = snd_pcm_plugin_set_disable(pcm_handle, PLUGIN_DISABLE_MMAP)) < 0) {
		return -1;
	}

	memset(&pi, 0, sizeof(pi));
	pi.channel = SND_PCM_CHANNEL_PLAYBACK;
	if ((rtn = snd_pcm_plugin_info(pcm_handle, &pi)) < 0) {
		return -1;
	}

	memset(&pp, 0, sizeof(pp));

	pp.mode = SND_PCM_MODE_BLOCK;
	pp.channel = SND_PCM_CHANNEL_PLAYBACK;
	pp.start_mode = SND_PCM_START_FULL;
	pp.stop_mode = SND_PCM_STOP_STOP;

	pp.buf.block.frag_size = pi.max_fragment_size;
	if (fragsize != -1)
		pp.buf.block.frag_size = fragsize;
	pp.buf.block.frags_max = 1;
	pp.buf.block.frags_min = 1;

	pp.format.interleave = 1;
	pp.format.rate = mSampleRate;
	pp.format.voices = mSampleChannels;

	if (ENDIAN_LE16(wavHdr1.FormatTag) == 6)
		pp.format.format = SND_PCM_SFMT_A_LAW;
	else if (ENDIAN_LE16(wavHdr1.FormatTag) == 7)
		pp.format.format = SND_PCM_SFMT_MU_LAW;
	else if (mSampleBits == 8)
		pp.format.format = SND_PCM_SFMT_U8;
	else if (mSampleBits == 24)
		pp.format.format = SND_PCM_SFMT_S24;
	else
		pp.format.format = SND_PCM_SFMT_S16_LE;

	if ((rtn = snd_pcm_plugin_params(pcm_handle, &pp)) < 0) {
		return -1;
	}

	if ((rtn = snd_pcm_plugin_prepare(pcm_handle, SND_PCM_CHANNEL_PLAYBACK))< 0)


	memset(&setup, 0, sizeof(setup));
	memset(&group, 0, sizeof(group));
	setup.channel = SND_PCM_CHANNEL_PLAYBACK;
	setup.mixer_gid = &group.gid;
	if ((rtn = snd_pcm_plugin_setup(pcm_handle, &setup)) < 0) {
		return -1;
	}
	bsize = setup.buf.block.frag_size;

	if (group.gid.name[0] == 0) {
		exit(-1);
	}
	if ((rtn = snd_mixer_open(&mixer_handle, card, setup.mixer_device)) < 0) {
		return -1;
	}

	mSamples = FindTag(waveFile, "data");

	mSampleBfr1 = malloc(bsize);
	FD_ZERO (&rfds);
	FD_ZERO (&wfds);
	sizeRtn = 1;
	while (N < mSamples && sizeRtn > 0 && stopped == 0) {
		playing = 1;
		if (tcgetpgrp(0) == getpid())
			FD_SET (STDIN_FILENO, &rfds);
		FD_SET (snd_mixer_file_descriptor (mixer_handle), &rfds);
		FD_SET (snd_pcm_file_descriptor (pcm_handle, SND_PCM_CHANNEL_PLAYBACK), &wfds);

		rtn = max (snd_mixer_file_descriptor (mixer_handle),
				snd_pcm_file_descriptor (pcm_handle, SND_PCM_CHANNEL_PLAYBACK));

		if (select(rtn + 1, &rfds, &wfds, NULL, NULL) == -1)
			return err("select");

		if (FD_ISSET (snd_mixer_file_descriptor (mixer_handle), &rfds)) {
			snd_mixer_callbacks_t callbacks = { 0, 0, 0, 0 };

			snd_mixer_read(mixer_handle, &callbacks);
		}

		if (FD_ISSET (snd_pcm_file_descriptor (pcm_handle, SND_PCM_CHANNEL_PLAYBACK), &wfds)) {
			snd_pcm_channel_status_t status;
			int written = 0;

			if ((sizeRtn = fread(mSampleBfr1, 1, min (mSamples - N, bsize),
					waveFile)) <= 0)
				continue;
			written = snd_pcm_plugin_write(pcm_handle, mSampleBfr1, sizeRtn);
			if (written < sizeRtn) {
				memset(&status, 0, sizeof(status));
				status.channel = SND_PCM_CHANNEL_PLAYBACK;
				if (snd_pcm_plugin_status(pcm_handle, &status) < 0) {
					exit(1);
				}

				if (status.status == SND_PCM_STATUS_READY || status.status
						== SND_PCM_STATUS_UNDERRUN) {
					if (snd_pcm_plugin_prepare(pcm_handle,
							SND_PCM_CHANNEL_PLAYBACK) < 0) {
						exit(1);
					}
				}
				if (written < 0)
					written = 0;
				written += snd_pcm_plugin_write(pcm_handle, mSampleBfr1
						+ written, sizeRtn - written);
			}

			N += written;
		}
	} // END WHILE LOOP

	sizeRtn = snd_pcm_plugin_flush(pcm_handle, SND_PCM_CHANNEL_PLAYBACK);

	rtn = snd_mixer_close(mixer_handle);
	rtn = snd_pcm_close(pcm_handle);
	fclose(waveFile);
	stopped = 0;
	playing  = 0;
}

/*****************************************************************************************************/

PtWidget_t* b1;
PtWidget_t* b2;
PtWidget_t* b3;
PtWidget_t* b4;
PtWidget_t* temp;

validDown = 1;
validLeft = 1;
validRight = 1;

mode = 0;
startCurrent = 0;
int r, rotateCounter = 0;
int init = 0;
int x0,y0,x1,y1,x2,y2,x3,y3;
int grid[22][10];
int whichRow[4] = {0,0,0,0};
int score=0;
char scoreLabel[10] = "";
int clicked = 0;

int
startGame( PtWidget_t *widget, ApInfo_t *apinfo, PtCallbackInfo_t *cbinfo )

	{

	/* eliminate 'unreferenced' warnings */
	widget = widget, apinfo = apinfo, cbinfo = cbinfo;
	
	stopped = 1;
	while(playing == 1);
	//start a new thread to play the wave
	pthread_create(NULL, NULL, (void*) playingWave, NULL);
	

	if(startCurrent == 0){
		memset(grid, 0, sizeof(grid[0][0]) * 21 * 10);
		score = 0;
		PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(score, scoreLabel, 10), 0);
		initialize();
		init = 1;
		//startGameDropPiece();
	}
		startCurrent = 1;
	if(clicked ==0)
		startGameDropPiece();
	clicked = 1;
	return( Pt_CONTINUE );
		
	}

void initialize(){
	int k = 0, p = 0;
		for(k; k < 21; k++){
			for(p; p < 10; p++){
				grid[k][p] = 0;
			}
		}
		k = 21; p = 0;
			for(k; k < 22; k++){
			for(p; p < 10; p++){
				grid[k][p] = 1;
			}
		}
}

void updateArrayScreen(){
	char buffer[30]="";
	int k = 0, p = 0, count = 0;
		
			for(p; p < 10; p++){
				buffer[count] = ((grid[19][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel00, Pt_ARG_TEXT_STRING, buffer , 0);
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[20][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel0, Pt_ARG_TEXT_STRING, buffer , 0);
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[18][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel01, Pt_ARG_TEXT_STRING, buffer , 0);
	
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[16][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel02, Pt_ARG_TEXT_STRING, buffer , 0);
		
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[17][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel03, Pt_ARG_TEXT_STRING, buffer , 0);
}
void shiftBoardDown(int board[][10], int row){
    int i,j;
    for (i = row; i > 0; i--) {
        /* code */
        for (j = 0; j < 10; j++) {
            /* code */
            if( i != 0){
                board[i][j] = board[i-1][j];
                //PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "Shifted!", 0);
            }else{
                board[i][j] = 0;
            }
        }
    }
}

void check1s(int whichRow[4], int board[][10]){
    int i,m, oneCount, whichRowCount=0;
    for(i=20; i>0; i--){
        oneCount=0;
        for(m=0; m<10; m++){
            if(board[i][m] > 0){
                oneCount++;
            }
            
            if(oneCount == 10){
            	score+=100;
    			PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(score, scoreLabel, 10), 0);
            	//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "WHOLE LINE", 0);
            	clearScreen();
            	shiftBoardDown(grid, i);
            	int k, p;
            	for(k=0; k<21; k++){
            	     for(p=0;p<10;p++){
            	     		if(grid[k][p] > 0){
            	     			// Draw Block
            	     			PtWidget_t* temp;
            	     			int y = (k*13) -8;
            	     			int x = (p*25) + 9;
            	     			
            	     			if(grid[k][p] == 7 || grid[k][p] == 5){
            	     				temp = createBlock(ABW_base, x, y, Pg_GREEN, Pg_DGREEN);
            	     			}
            	     			else if(grid[k][p] == 6 || grid[k][p] == 4){
            	     				temp = createBlock(ABW_base, x, y, Pg_BLUE, Pg_DBLUE);
            	     			}
            	     			else if(grid[k][p] == 2 || grid[k][p] == 3){
            	     				temp = createBlock(ABW_base, x, y, Pg_YELLOW, Pg_YELLOW);
            	     			}
            	     			else if(grid[k][p] == 1){
            	     				temp = createBlock(ABW_base, x, y, Pg_RED, Pg_VGA4);
            	     			}
            	     			
								PtRealizeWidget(temp);
            	     		}
            	     }
            	}
            	
            	/*
                if(whichRowCount < 4){
                    whichRow[whichRowCount] = i;
                    whichRowCount++;
                }*/
            }
        }
    }    
}

void clearScreen(){
	PhPoint_t pos;
	PhArea_t area;
	PhDim_t dim;
	PtArg_t argt[2];
	
	pos.x = 9;
	pos.y = 5;
	area.pos = pos;
	dim.h = 260;
	dim.w = 250;
	area.size = dim;
	PtSetArg(&argt[0],Pt_ARG_AREA, &area,0);
	PtSetArg(&argt[1],Pt_ARG_FILL_COLOR, Pg_BLACK, 0);
	
	PtWidget_t* thing;
	thing = PtCreateWidget(PtPane,ABW_base,2,argt);
	PtRealizeWidget(thing);
	clicked = 0;
}


void startGameDropPiece(){
	char buffer[30]="";
	int k = 0, p = 0, count = 0;
		
			for(p; p < 10; p++){
				buffer[count] = ((grid[19][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel00, Pt_ARG_TEXT_STRING, buffer , 0);
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[20][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel0, Pt_ARG_TEXT_STRING, buffer , 0);
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[18][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel01, Pt_ARG_TEXT_STRING, buffer , 0);
	
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[16][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel02, Pt_ARG_TEXT_STRING, buffer , 0);
		
	buffer[30] = "";
	k = 0; p =0; count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[17][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel03, Pt_ARG_TEXT_STRING, buffer , 0);
	
	validDown = 1;
	validLeft = 1;
	validRight = 1;
	
	checkForLoser();
	
	if(startCurrent == 1){
	
	r = 0;
	rotateCounter = 0;
	
	srand(time(NULL));
	r = rand() % 7 + 1;
	
	if(r == 1){
		buildI(ABW_base);
	}
	else if(r == 2){
		buildO(ABW_base);
	}
	else if(r == 3){
		buildT(ABW_base);
	}
	else if(r == 4){
		buildS(ABW_base);
	}
	else if(r == 5){
		buildZ(ABW_base);
	}
	else if(r == 6){
		buildL(ABW_base);
	}
	else if(r == 7){
		buildJ(ABW_base);
	}
	}
}
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//

void rotate(){
	PhPoint_t  *pos;
	int x, y;
	
	//I PIECE
	if(r == 1){
		if(rotateCounter % 2 == 1){
	 //   	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "straight", 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x+ 50;
			y = pos->y - 26;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
				
			if(x <= 9){
				validLeft = 0;
			}
				if(x != 234)
			validRight = 1;	
				
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b2, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x+ 25;
			y = pos->y - 13;			
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
				
			if(x <= 9){
				validLeft = 0;
			}
				if(x != 234)
			validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b2, Pt_ARG_POS, &point1, 0);
			
			//Piece 3 is the pivot
			
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
							
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
			
			rotateCounter++;
		}
		else{
	//		PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "side", 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x- 50;
			y = pos->y + 26;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
			
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
			validRight = 1;
							
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b2, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
							
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b2, Pt_ARG_POS, &point1, 0);
			
			//Piece 3 is the pivot
			
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y - 13;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
			
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
							
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
			
			rotateCounter++;
		}
	}
	
	//O PIECE
	if(r == 2){
		//NO ROTATION
	}
	
	//T PIECE
	char str[10];
	
	if(r ==3){
		if(rotateCounter % 4 == 0){
		//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y + 13;
					
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
			
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
			
						
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y + 26;
					
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;		
				
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
			
			rotateCounter++;
		}
		else if(rotateCounter % 4 == 1){
		//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter% 4, str, 10), 0);
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y - 13;
					
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
			
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
							
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point0, 0);
			
			rotateCounter++;
		}		
		else if(rotateCounter % 4 == 2){
		//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING,  itoa(rotateCounter% 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			rotateCounter++;
		}		
		else if(rotateCounter % 4 == 3){
		//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING,  itoa(rotateCounter% 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y -26;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y - 13;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);	
			
			rotateCounter++;	
		}
	}
	
	//S PIECE
	if(r == 4){
			
		if(rotateCounter % 2 == 0){
		//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "straight", 0);
			PtGetResource(b2, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 50;
			y = pos->y - 13;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b2, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y - 13;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point1, 0);
			
			rotateCounter++;
		}
		else{
		//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "go", 0);
			PtGetResource(b2, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 50;
			y = pos->y + 13;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
								
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b2, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y + 13;
					
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
					
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
						
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point1, 0);	
			
			rotateCounter++;	
		}
    }
    
    		//Z PIECE
		if(r == 5){
		
			if(rotateCounter % 2 == 0){
			
			//	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "straight", 0);
				PtGetResource(b3, Pt_ARG_POS, &pos, 0);
				x = 0; y = 0;
				x = pos->x;
				y = pos->y - 26;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
									
			if(x < 5 || x > 259 || y > 224)
				return;			
			
				PhPoint_t point0 = {x,y};
				PtSetResource(b3, Pt_ARG_POS, &point0, 0);
		
				PtGetResource(b4, Pt_ARG_POS, &pos, 0);
				x = 0; y = 0;
				x = pos->x - 50;
				y = pos->y;
					
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
			
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
									
			if(x < 5 || x > 259 || y > 224)
				return;			
			
				PhPoint_t point1 = {x,y};
				PtSetResource(b4, Pt_ARG_POS, &point1, 0);
			
				rotateCounter++;
			}
		    else{
		    
		    //	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "go", 0);
				PtGetResource(b3, Pt_ARG_POS, &pos, 0);
				x = 0; y = 0;
				x = pos->x;
				y = pos->y + 26;
						
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
									
			if(x < 5 || x > 259 || y > 224)
				return;			
			
				PhPoint_t point0 = {x,y};
				PtSetResource(b3, Pt_ARG_POS, &point0, 0);
		
				PtGetResource(b4, Pt_ARG_POS, &pos, 0);
				x = 0; y = 0;
				x = pos->x + 50;
				y = pos->y;
				
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
									
			if(x < 5 || x > 259 || y > 224)
				return;			
			
				PhPoint_t point1 = {x,y};
				PtSetResource(b4, Pt_ARG_POS, &point1, 0);
			
				rotateCounter++;
			}

	}
	
	// L PIECE
	if(r == 6){
		if(rotateCounter % 4 == 0){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x -25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
				
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y - 13;
			PhPoint_t point2 = {x,y};
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
			
			rotateCounter++;
		}
		else if(rotateCounter % 4 == 1){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y - 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y -26;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y + 13;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
			
			rotateCounter++;
		}		
		else if(rotateCounter % 4 == 2){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y + 13;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y - 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
		
			rotateCounter++;
		}		
		else if(rotateCounter % 4 == 3){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y + 13;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;
							
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);

			rotateCounter++;	
		}		
	}
		
	// J PIECE
	if(r == 7){
		if(rotateCounter % 4 == 0){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y - 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y - 13;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;
			
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
			
			rotateCounter++;
		}
		else if(rotateCounter % 4 == 1){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y - 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
			
			rotateCounter++;
		}		
		else if(rotateCounter % 4 == 2){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y;

			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
				
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);
		
			rotateCounter++;
		}		
		else if(rotateCounter % 4 == 3){
			//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(rotateCounter % 4, str, 10), 0);
			PtGetResource(b1, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x + 25;
			y = pos->y -13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;		
			
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point0 = {x,y};
			PtSetResource(b1, Pt_ARG_POS, &point0, 0);
		
			PtGetResource(b3, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 25;
			y = pos->y + 13;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point1 = {x,y};
			PtSetResource(b3, Pt_ARG_POS, &point1, 0);
		
			PtGetResource(b4, Pt_ARG_POS, &pos, 0);
			x = 0; y = 0;
			x = pos->x - 50;
			y = pos->y;
			
			if(x == 234){
				validRight = 0;
			}
			if(x != 9)
				validLeft = 1;	
		
			if(x <= 9){
				validLeft = 0;
			}
			if(x != 234)
				validRight = 1;
					
			if(x < 5 || x > 259 || y > 224)
				return;			
			
			PhPoint_t point2 = {x,y};
			PtSetResource(b4, Pt_ARG_POS, &point2, 0);

			rotateCounter++;	
		}		
	}
}

	
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//

void buildZ(PtWidget_t *window)
{
	b1 = createBlock(window, 109, 5, Pg_GREEN, Pg_DGREEN);
	b2 = createBlock(window, 134, 5, Pg_GREEN, Pg_DGREEN);
	b3 = createBlock(window, 134, 18, Pg_GREEN, Pg_DGREEN);
	b4 = createBlock(window, 159, 18, Pg_GREEN, Pg_DGREEN);

	//isCurrent = 0;
	//currentPiece = 3;
	
	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4);

}

void buildS(PtWidget_t *window)
{
	b1 = createBlock(window, 109, 18, Pg_BLUE, Pg_DBLUE);
	b2 = createBlock(window, 134, 5, Pg_BLUE, Pg_DBLUE);
	b3 = createBlock(window, 109, 5, Pg_BLUE, Pg_DBLUE);
	b4 = createBlock(window, 84, 18, Pg_BLUE, Pg_DBLUE);

	//isCurrent = 0;
	//currentPiece = 3;
	
	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4);
	}

void buildO(PtWidget_t *window)
{
	b1 = createBlock(window, 109, 5, Pg_YELLOW,Pg_YELLOW);
	b2 = createBlock(window, 109, 18, Pg_YELLOW, Pg_YELLOW);
	b3 = createBlock(window, 134, 5, Pg_YELLOW, Pg_YELLOW);
	b4 = createBlock(window, 134, 18, Pg_YELLOW, Pg_YELLOW);

	//isCurrent = 0;
	//currentPiece = 3;
	
	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4);
	}

void buildI(PtWidget_t *window)
{
	b1 = createBlock(window, 109, 5, Pg_RED, Pg_VGA4);
	b2 = createBlock(window, 109, 18, Pg_RED, Pg_VGA4);
	b3 = createBlock(window, 109, 31, Pg_RED, Pg_VGA4);
	b4 = createBlock(window, 109, 44, Pg_RED, Pg_VGA4);

	//isCurrent = 0;
	//currentPiece = 3;
	
	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4);
	}
	

void buildT(PtWidget_t *window)
{
	b1 = createBlock(window, 109, 5, Pg_YELLOW, Pg_YELLOW);
	b2 = createBlock(window, 134, 5, Pg_YELLOW, Pg_YELLOW);
	b3 = createBlock(window, 159, 5, Pg_YELLOW, Pg_YELLOW);
	b4 = createBlock(window, 134, 18, Pg_YELLOW, Pg_YELLOW);

	//isCurrent = 0;
	//currentPiece = 3;
	
	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4)	;
	}

void buildL(PtWidget_t *window)
{
	b1 = createBlock(window, 109, 5, Pg_BLUE, Pg_DBLUE);
	b2 = createBlock(window, 109, 18, Pg_BLUE, Pg_DBLUE);
	b3 = createBlock(window, 109, 31, Pg_BLUE, Pg_DBLUE);
	b4 = createBlock(window, 134, 31, Pg_BLUE, Pg_DBLUE);

	//isCurrent = 0;
	//currentPiece = 3;
	
	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4);
	}
	

void buildJ(PtWidget_t *window)
{
	b1 = createBlock(window, 134, 5, Pg_GREEN, Pg_DGREEN);
	b2 = createBlock(window, 134, 18, Pg_GREEN,  Pg_DGREEN);
	b3 = createBlock(window, 134, 31, Pg_GREEN,   Pg_DGREEN);
	b4 = createBlock(window, 109, 31, Pg_GREEN,   Pg_DGREEN);
	
	//isCurrent = 0;
	//currentPiece = 3;

	PtRealizeWidget(b1);
	PtRealizeWidget(b2);
	PtRealizeWidget(b3);
	PtRealizeWidget(b4);
	}
	
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//

void pieceDown(){
	int pieceCantMove = 0;
	int xNextDown0, yNextDown0, xNextDown1, yNextDown1, xNextDown2, yNextDown2, xNextDown3, yNextDown3;
	int xCur0, yCur0, xCur1, yCur1, xCur2, yCur2, xCur3, yCur3;
	int gridOK;
	int doIt;
	
	PhPoint_t *pos;
	
	PtGetResource(b1, Pt_ARG_POS, &pos, 0);
	x0= 0;
	y0 = 0;
	
	x0 = pos->x;
	y0 = pos->y + 13;

	xCur0 = ((pos->x - 9)/25);
	yCur0 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextDown0 =((x0 - 9)/25);
	yNextDown0 = ((y0 - 5)/13)+1;
	
	char str[10] = "";
	//PtSetResource(ABW_scoreLabel022, Pt_ARG_TEXT_STRING, itoa(xCur0, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel023, Pt_ARG_TEXT_STRING, itoa(yCur0, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel0210, Pt_ARG_TEXT_STRING, itoa(xNextDown0, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0215, Pt_ARG_TEXT_STRING, itoa(yNextDown0, str, 10), 0);
	
	//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(yNextDown, buffer, 10)-1 , 0);
	
	//checks next grid move
	gridOK =	checkGrid(xNextDown0, yNextDown0);

	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validDown = 0;
	}

	
	//----------------------------------------------------------
	PtGetResource(b2, Pt_ARG_POS, &pos, 0);
	x1 = 0; y1 = 0;
	
	x1 = pos->x;
	y1 = pos->y + 13;
	
	xCur1 = ((pos->x - 9)/25);
	yCur1 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextDown1 =((x1 - 9)/25);
	yNextDown1 = ((y1 - 5)/13)+1;

	str[10] = "";
	PtSetResource(ABW_scoreLabel024, Pt_ARG_TEXT_STRING, itoa(xCur1, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel025, Pt_ARG_TEXT_STRING, itoa(yCur1, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel026, Pt_ARG_TEXT_STRING, itoa(xNextDown1, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0214, Pt_ARG_TEXT_STRING, itoa(yNextDown1, str, 10), 0);
	
	
	//checks next grid move
	gridOK =	checkGrid(xNextDown1, yNextDown1);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validDown = 0;
	}


	//----------------------------------------------------------
	PtGetResource(b3, Pt_ARG_POS, &pos, 0);
	x2 = 0; y2 = 0;
	
	x2 = pos->x;
	y2 = pos->y + 13;
	
	xCur2 = ((pos->x - 9)/25);
	yCur2 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextDown2 =((x2 - 9)/25);
	yNextDown2 = ((y2 - 5)/13)+1;
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel027, Pt_ARG_TEXT_STRING, itoa(xCur2, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel028, Pt_ARG_TEXT_STRING, itoa(yCur2, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel029, Pt_ARG_TEXT_STRING, itoa(xNextDown2, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0213, Pt_ARG_TEXT_STRING, itoa(yNextDown2, str, 10), 0);
	
	
	//checks next grid move
	gridOK =	checkGrid(xNextDown2, yNextDown2);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validDown = 0;
	}

	
	//--------------------------------------------------------
	PtGetResource(b4, Pt_ARG_POS, &pos, 0);
	x3 = 0; y3 = 0;
	
	x3 = pos->x;
	y3 = pos->y + 13;

	xCur3 = ((pos->x - 9)/25);
	yCur3 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextDown3 =((x3 - 9)/25);
	yNextDown3 = ((y3 - 5)/13)+1;
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel0211, Pt_ARG_TEXT_STRING, itoa(xCur3, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel021, Pt_ARG_TEXT_STRING, itoa(yCur3, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel020, Pt_ARG_TEXT_STRING, itoa(xNextDown3, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0212, Pt_ARG_TEXT_STRING, itoa(yNextDown3, str, 10), 0);
	
	
	//checks next grid move
	gridOK =	checkGrid(xNextDown3, yNextDown3);
		if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validDown = 0;
	}

	
	if(pieceCantMove == 1){
		//PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, "cant move" , 0);
		setGrid(xCur0, yCur0, xCur1, yCur1, xCur2, yCur2, xCur3, yCur3);     // need to set all of the pices places to 1
		pieceCantMove = 0;
		validDown = 1;
		memset(whichRow, 0, sizeof whichRow);
		check1s(whichRow, grid);
		int q;
		for(q=0; q<4;q++){
        	//shiftBoardDown(grid, whichRow[0]);
        	memset(whichRow, 0, sizeof whichRow);
        	check1s(whichRow, grid);
    	}
    	startGameDropPiece();
    	score+=10;
    	PtSetResource(ABW_scoreLabel, Pt_ARG_TEXT_STRING, itoa(score, scoreLabel, 10), 0);
		return;
	}
	else{
		PhPoint_t point = {x0,y0};
		PtSetResource(b1, Pt_ARG_POS, &point, 0);
		
		PhPoint_t point1 = {x1,y1};
		PtSetResource(b2, Pt_ARG_POS, &point1, 0);
		
		PhPoint_t point2 = {x2,y2};
		PtSetResource(b3, Pt_ARG_POS, &point2, 0);
		
		PhPoint_t point4 = {x3,y3};
		PtSetResource(b4, Pt_ARG_POS, &point4, 0);
	}
}

	
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//

void pieceLeft(){
int gridOK, pieceCantMove = 0;
int x0,x1,x2,x3,y0,y1,y2,y3;
int xCur0, xCur1, xCur2, xCur3, yCur0, yCur1, yCur2, yCur3;
int xNextLeft0, xNextLeft1, xNextLeft2, xNextLeft3, yNextLeft0, yNextLeft1, yNextLeft2, yNextLeft3;

	PhPoint_t *pos;
	
	PtGetResource(b1, Pt_ARG_POS, &pos, 0);
	
	x0 = pos->x - 25;
	y0 = pos->y;
	
	xCur0 = ((pos->x - 9)/25);
	yCur0 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextLeft0 =((x0 - 9)/25);
	yNextLeft0 = ((y0 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextLeft0, yNextLeft0);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validLeft = 0;
	}
	
	char str[10] = "";
	PtSetResource(ABW_scoreLabel022, Pt_ARG_TEXT_STRING, itoa(xCur0, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel023, Pt_ARG_TEXT_STRING, itoa(yCur0, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel0210, Pt_ARG_TEXT_STRING, itoa(xNextLeft0, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0215, Pt_ARG_TEXT_STRING, itoa(yNextLeft0, str, 10), 0);
	
	
	if(x0 <= 9){
		validLeft = 0;
	}
	if(x0 != 234)
		validRight = 1;

	
	//----------------------------------------------------------
	PtGetResource(b2, Pt_ARG_POS, &pos, 0);

	
	x1 = pos->x - 25;
	y1 = pos->y;
	
	xCur1 = ((pos->x - 9)/25);
	yCur1 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextLeft1 =((x1 - 9)/25);
	yNextLeft1 = ((y1 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextLeft1, yNextLeft1);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validLeft = 0;
	}
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel024, Pt_ARG_TEXT_STRING, itoa(xCur1, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel025, Pt_ARG_TEXT_STRING, itoa(yCur1, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel026, Pt_ARG_TEXT_STRING, itoa(xNextLeft1, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0214, Pt_ARG_TEXT_STRING, itoa(yNextLeft1, str, 10), 0);
	
	
	if(x1 <= 9){
		validLeft = 0;
	}
	if(x1 != 234)
		validRight = 1;
		



	//----------------------------------------------------------
	PtGetResource(b3, Pt_ARG_POS, &pos, 0);

	
	x2 = pos->x - 25;
	y2 = pos->y;

	xCur2 = ((pos->x - 9)/25);
	yCur2 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextLeft2 =((x2 - 9)/25);
	yNextLeft2 = ((y2 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextLeft2, yNextLeft2);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validLeft = 0;
	}
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel027, Pt_ARG_TEXT_STRING, itoa(xCur2, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel028, Pt_ARG_TEXT_STRING, itoa(yCur2, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel029, Pt_ARG_TEXT_STRING, itoa(xNextLeft2, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0213, Pt_ARG_TEXT_STRING, itoa(yNextLeft2, str, 10), 0);

	if(x2 == 9){
		validLeft = 0;
	}
	if(x2 != 234)
		validRight = 1;

	
	//--------------------------------------------------------
	PtGetResource(b4, Pt_ARG_POS, &pos, 0);

	
	x3 = pos->x - 25;
    y3 = pos->y;
	
	xCur3 = ((pos->x - 9)/25);
	yCur3 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextLeft3 =((x3 - 9)/25);
	yNextLeft3 = ((y3 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextLeft3, yNextLeft3);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validLeft = 0;
	}	
	
		str[10] = "";
	PtSetResource(ABW_scoreLabel0211, Pt_ARG_TEXT_STRING, itoa(xCur3, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel021, Pt_ARG_TEXT_STRING, itoa(yCur3, str, 10), 0);
	
	str[10] = "";
	PtSetResource(ABW_scoreLabel020, Pt_ARG_TEXT_STRING, itoa(xNextLeft3, str, 10), 0);
	str[10] = "";
	PtSetResource(ABW_scoreLabel0212, Pt_ARG_TEXT_STRING, itoa(yNextLeft3, str, 10), 0);
	
	if(x3 == 9){
		validLeft = 0;
	}
	if(x3 != 234)
		validRight = 1;

	
	if(pieceCantMove == 1){
		pieceCantMove = 0;
		return;
	}
	else{
			
	PhPoint_t point = {x0,y0};
	PtSetResource(b1, Pt_ARG_POS, &point, 0);
	PhPoint_t point1 = {x1,y1};
	PtSetResource(b2, Pt_ARG_POS, &point1, 0);
	PhPoint_t point2 = {x2,y2};
	PtSetResource(b3, Pt_ARG_POS, &point2, 0);
	PhPoint_t point4 = {x3,y3};
	PtSetResource(b4, Pt_ARG_POS, &point4, 0);
	
	}
}

	
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//
//&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&//

void pieceRight(){
int gridOK, pieceCantMove = 0;
int x0,x1,x2,x3,y0,y1,y2,y3;
int xCur0, xCur1, xCur2, xCur3, yCur0, yCur1, yCur2, yCur3;
int xNextRight0, xNextRight1, xNextRight2, xNextRight3, yNextRight0, yNextRight1, yNextRight2, yNextRight3;


	PhPoint_t *pos;
	
	PtGetResource(b1, Pt_ARG_POS, &pos, 0);
	
	x0 = pos->x + 25;
	y0 = pos->y ;
	
	xCur0 = ((pos->x - 9)/25);
	yCur0 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextRight0 =((x0 - 9)/25);
	yNextRight0 = ((y0 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextRight0, yNextRight0);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validRight = 0;
	}
	
	if(x0 == 234){
		validRight = 0;
	}
	if(x0 != 9)
		validLeft = 1;
		
	
		//----------------------------------------------------------
	PtGetResource(b2, Pt_ARG_POS, &pos, 0);
	
	x1 = pos->x + 25;
	y1 = pos->y;
	
	xCur1 = ((pos->x - 9)/25);
	yCur1 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextRight1 =((x1 - 9)/25);
	yNextRight1 = ((y1 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextRight1, yNextRight1);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validRight = 0;
	}
	
	if(x1 == 234){
		validRight = 0;
	}
	if(x1 != 9)
		validLeft = 1;
			



	//----------------------------------------------------------
	PtGetResource(b3, Pt_ARG_POS, &pos, 0);
	
	x2 = pos->x + 25;
	y2 = pos->y;
	
	xCur2 = ((pos->x - 9)/25);
	yCur2 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextRight2 =((x2 - 9)/25);
	yNextRight2 = ((y2 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextRight2, yNextRight2);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validRight = 0;
	}
	
	if(x2 == 234){
		validRight = 0;
	}
	if(x2 != 9)
		validLeft = 1;
	
	//--------------------------------------------------------
	PtGetResource(b4, Pt_ARG_POS, &pos, 0);
	
	
	x3 = pos->x + 25;
	y3 = pos->y;
	
	xCur3 = ((pos->x - 9)/25);
	yCur3 = ((pos->y - 5)/13)+1;
	
	//Grid coordinates
	xNextRight3 =((x3 - 9)/25);
	yNextRight3 = ((y3 - 5)/13)+1;
	
	//checks next grid move
	gridOK =	checkGrid(xNextRight3, yNextRight3);
	
	if(gridOK == 0){ //FALSE
		pieceCantMove = 1;
		validRight = 0;
	}
	
	if(x3 == 234){
		validRight = 0;
	}
	if(x3 != 9)
		validLeft = 1;
		
    if(pieceCantMove == 1){
		pieceCantMove = 0;
		return;
	}
	else{

		PhPoint_t point = {x0,y0};
		PtSetResource(b1, Pt_ARG_POS, &point, 0);
		PhPoint_t point1 = {x1,y1};
		PtSetResource(b2, Pt_ARG_POS, &point1, 0);
		PhPoint_t point2 = {x2,y2};
		PtSetResource(b3, Pt_ARG_POS, &point2, 0);
		PhPoint_t point4 = {x3,y3};
		PtSetResource(b4, Pt_ARG_POS, &point4, 0);
	}

}


int checkGrid(int x, int y){
	if(grid[y][x] > 0){
		return 0 ;
	}
	else
		return 1;

}

void setGrid(x1,y1,x2,y2,x3,y3,x4,y4){
	grid[y1][x1] = r;
	grid[y2][x2] = r;
	grid[y3][x3] = r;
	grid[y4][x4] = r;
}

PtWidget_t *createBlock(PtWidget_t *window, int posX, int posY, PgColor_t color, PgColor_t outline){
	PhPoint_t pos;
	PhArea_t area;
	PhDim_t dim;
	PtArg_t argt[5];
	
	pos.x = posX;
	pos.y = posY;
	area.pos = pos;
	dim.h = 13;
	dim.w = 25;
	area.size = dim;
	PtSetArg(&argt[0],Pt_ARG_AREA, &area,0);
	PtSetArg(&argt[1],Pt_ARG_FILL_COLOR, color, 0);
	PtSetArg(&argt[2],Pt_ARG_BEVEL_WIDTH,5 , 0);
	PtSetArg(&argt[3], Pt_ARG_BORDER_CONTRAST, 40, 0);
	PtSetArg(&argt[4], Pt_ARG_OUTLINE_COLOR, outline, 0);
	
	return (PtCreateWidget(PtPane,window,5,argt));
}

int toggleDiff = 0, init, repeat;

changeDifficulty(){
	mode = (mode + 1) % 2;

}

checkForLoser(){

	char buffer[30] = "";
	int k = 0, p =0, count = 0;
			for(p; p < 10; p++){
				buffer[count] = ((grid[1][p])+48);
				count++;
			}
	PtSetResource(ABW_scoreLabel022, Pt_ARG_TEXT_STRING, buffer , 0);

if(grid[1][4] > 0  || grid[1][5] > 0 || grid[1][3] > 0  || grid[1][6] > 0 ){
	clicked = 0;
	PhPoint_t pos;
	PhArea_t area;
	PhDim_t dim;
	PtArg_t argt[2];
	
	pos.x = 9;
	pos.y = 5;
	area.pos = pos;
	dim.h = 260;
	dim.w = 250;
	area.size = dim;
	PtSetArg(&argt[0],Pt_ARG_AREA, &area,0);
	PtSetArg(&argt[1],Pt_ARG_FILL_COLOR, Pg_BLACK, 0);
	
	PtWidget_t* thing;
	thing = PtCreateWidget(PtPane,ABW_base,2,argt);
	PtRealizeWidget(thing);
	
	startCurrent = 0;
	}
}