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
#include "globals.h"

int
restartGame( PtWidget_t *widget, ApInfo_t *apinfo, PtCallbackInfo_t *cbinfo )

	{

	/* eliminate 'unreferenced' warnings */
	widget = widget, apinfo = apinfo, cbinfo = cbinfo;


	 startCurrent = 0;

	
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

		
		
	return( Pt_CONTINUE );

	}

