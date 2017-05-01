/* Import (extern) header for application - AppBuilder 2.03  */

#include "abdefine.h"

extern ApWindowLink_t base;
extern ApWidget_t AbWidgets[ 39 ];


#ifdef __cplusplus
extern "C" {
#endif
int startGame( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int moveDown( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int moveLeft( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int moveRight( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int toggleButton( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int restartGame( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int activateTimer( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int realizedDrop( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int changeDiff( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
int activateDropHard( PtWidget_t *widget, ApInfo_t *data, PtCallbackInfo_t *cbinfo );
#ifdef __cplusplus
}
#endif
