#ifndef SERVER_UTILS_H
#define SERVER_UTILS_H

#include <stdio.h>

void optionalArgs(char *desc, char *arg, int *matrix_behaviour, int *game_length, int *seed, char *dizionario, int *matrixCheck, char **matrixFile);
//semplice funzione che controlla se una parola è già stata utilizzata dal giocatore
int isWordUsed(char *word, char **UsedWords, int nUsedWords);


#endif
