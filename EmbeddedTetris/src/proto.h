
/* abmain.c */

/* activateDropHard.c */
int activateDropHard ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* activateTimer.c */
int activateTimer ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* changeDiff.c */
int changeDiff ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* moveDown.c */
int moveDown ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* moveLeft.c */
int moveLeft ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* moveRight.c */
int moveRight ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* realizedDrop.c */
int realizedDrop ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* restartGame.c */
int restartGame ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );

/* startGame.c */
int err ( char *msg );
int FindTag ( FILE *fp , const char *tag );
int CheckFileHeader ( FILE *fp );
void onStopped ( void );
void playingWave ( void );
int startGame ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );
void initialize ( void );
void updateArrayScreen ( void );
void shiftBoardDown ( int board [][10 ], int row );
void check1s ( int whichRow [4 ], int board [][10 ]);
void clearScreen ( void );
void startGameDropPiece ( void );
void rotate ( void );
void buildZ ( PtWidget_t *window );
void buildS ( PtWidget_t *window );
void buildO ( PtWidget_t *window );
void buildI ( PtWidget_t *window );
void buildT ( PtWidget_t *window );
void buildL ( PtWidget_t *window );
void buildJ ( PtWidget_t *window );
void pieceDown ( void );
void pieceLeft ( void );
void pieceRight ( void );
int checkGrid ( int x , int y );
void setGrid ( int x1 , int y1 , int x2 , int y2 , int x3 , int y3 , int x4 , int y4 );
PtWidget_t *createBlock ( PtWidget_t *window , int posX , int posY , PgColor_t color , PgColor_t outline );
int changeDifficulty ( void );
int checkForLoser ( void );

/* toggleButton.c */
int toggleButton ( PtWidget_t *widget , ApInfo_t *apinfo , PtCallbackInfo_t *cbinfo );
