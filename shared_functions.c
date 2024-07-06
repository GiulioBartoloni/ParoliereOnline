// il file contiene alcune funzioni condivise tra paroliere_cl e paroliere_srv
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "macros.h"
#include "shared_functions.h"

// semplice funzione che controlla se una stringa e' un mumero
// ritorna 1 se la stringa e' un numero
// 0 altrimenti
int isNumber(char *string) {
  for (int i = 0; i < strlen(string); i++) {
    if (string[i] > '9' || string[i] < '0')
      return 0;
  }
  return 1;
}

// funzione per il controllo dei parametri obbligatori
void requiredArgs(char *arg2, int *port) {
  // controllo che la porta del server sia un numero accettabile
  if (strlen(arg2) > 6) {
    printf("Errore nel numero di porta! Devono essere 1-6 caratteri!\n");
    exit(EXIT_FAILURE);
  }
  if (!isNumber(arg2)) {
    printf("Errore nel numero di porta! Non e' un intero corretto!\n");
    exit(EXIT_FAILURE);
  }
  *port = atoi(arg2);
  if (*port > 65535 || *port < 0) {
    printf("Errore nel numero di porta! Fuori dal range permesso!\n");
    exit(EXIT_FAILURE);
  }
}

//la funzione prende in input un buffer di messaggio, il tipo del messaggio e una stringa contenente il messaggio da inviare
//rialloca correttamente il buffer, aggiungendo il tipo di messaggio, il numero di caratteri della lunghezza, la lunghezza e il messaggio
//costruisce essenzialmente il messaggio da inviare
void composeMessage(char **buffer, char messageType, char *message){
    int messageLength = strlen(message);

    char lengthToString[5];
    sprintf(lengthToString, "%d", messageLength);

    int lengthChars = strlen(lengthToString);

    int totalLength = 1 + lengthChars + strlen(lengthToString) + messageLength + 1;

    SYSCN(*buffer, (char*)realloc(*buffer,totalLength*sizeof(char)), "realloc:");

    sprintf(*buffer, "%c%d%s%s", messageType, lengthChars, lengthToString, message);
}