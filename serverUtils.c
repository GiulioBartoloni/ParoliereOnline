//la libreria contiene alcune funzioni utilizzate dal server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "macros.h"
#include "serverUtils.h"
#include "shared_functions.h"

// la funzione serve a controllare gli argomenti opzionali
// per --matrici controllo (se presente) se e' un file regolare e se .txt
// per --durata controllo se la durata inserita e' un intero
// in base ai casi modifico le variabili di isMatrixFromFile e game_length che poi utilizzero' nel server per soddisfare i parametri richiesti
// lavoro con i loro puntatori per modificare direttamente le variabili
void optionalArgs(char *desc, char *arg, int *isMatrixFromFile, int *game_length, int *seed, char *dizionario, int *matrixCheck, char **matrixFilename) {
  struct stat statbuf;
  int retval;
  // controllo per il parametro --matrici
  // imposto il isMatrixFromFile
  // verifico che il file txt fornito sia corretto
  if (!strcmp(desc, "--matrici")) {
    if (*matrixCheck) {
      printf("Errore nei parametri! I parametri --seed e --matrici non possono essere usati contemporaneamente\n");
      exit(EXIT_FAILURE);
    }
    *isMatrixFromFile = 1;
    *matrixCheck = 1;
    SYSC(retval, stat(arg, &statbuf), "Stat:");
    if (S_ISREG(statbuf.st_mode)) {
      const char *file_extension = strrchr(arg, '.');
      if (file_extension && !strcmp(file_extension, ".txt")) {
        SYSCN(*matrixFilename, malloc(strlen(arg) * sizeof(char)), "Realloc:");
        strcpy(*matrixFilename, arg);
        return;
      }
    }
    printf("Errore nel path del file delle matrici! Il file non e' del formato corretto\n");
    exit(EXIT_FAILURE);
  }
  // controllo per il parametro --durata
  // verifico che la durata sia un numero valido
  else if (!strcmp(desc, "--durata")) {
    if (!isNumber(arg)) {
      printf("Errore nella durata della partita! Non e' un intero corretto!\n");
      exit(EXIT_FAILURE);
    }
    *game_length = atoi(arg);
    return;
  }
  // controllo per il parametro --seed
  // verifico che sia un numero valido e imposto il seed
  else if (!strcmp(desc, "--seed")) {
    if (*matrixCheck) {
      printf("Errore nei parametri! I parametri --seed e --matrici non possono essere usati contemporaneamente\n");
      exit(EXIT_FAILURE);
    }
    *matrixCheck = 1;
    if (!isNumber(arg)) {
      printf("Errore nel seed! Non e' un intero corretto\n");
      exit(EXIT_FAILURE);
    }
    *seed = atoi(arg);
    return;
  }
  // controllo per il parametro --diz
  // preparo la realloc per cambiare il dizionario che prenderemo
  // controllo che il dizionario sia un file regolare e txt
  else if (!strcmp(desc, "--diz")) {
    char *tmp;
    SYSCN(tmp, (char *)realloc(dizionario, strlen(arg) * sizeof(char)), "Realloc:");
    dizionario = tmp;
    strcpy(dizionario, arg);

    SYSC(retval, stat(arg, &statbuf), "Stat:");
    if (S_ISREG(statbuf.st_mode)) {
      const char *file_extension = strrchr(arg, '.');
      if (file_extension && !strcmp(file_extension, ".txt"))
        return;
    }
    printf("Errore nel path del file del dizionario! Il file non e' del formato corretto\n");
    exit(EXIT_FAILURE);
  }
  // comportamento di default nel caso il parametro sia stato inserito in maniera errata
  else {
    printf("Errore negli argomenti!\nUtilizzo corretto: ./paroliere_srv nome_server porta_server [--matrici data_filename] [--durata durata_gioco_in_minuti] [--seed rnd_seed] [--diz dizionario]\n");
    exit(EXIT_FAILURE);
  }
}

//semplice funzione che controlla se una parola è già stata utilizzata dal giocatore
int isWordUsed(char *word, char **UsedWords, int nUsedWords){
    for(int i = 0;i < nUsedWords; i++)
        if(strcmp(word, UsedWords[i]) == 0)
            return 1;
    return 0;
}