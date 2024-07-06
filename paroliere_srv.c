// librerie del C
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// librerie personali
//NB: fare riferimento ai rispettivi file per le documentazioni
#include "macros.h"
#include "matrix.h"
#include "serverUtils.h"
#include "shared_functions.h"
#include "timer.h"
#include "trie.h"

//definisco la struct che contiene tutte le informazioni relative a ciascun player
//queste sono tutte le variaibli che devono essere condivise tra i due thread per client
//contiene inoltre tutto ciò che è dichiarato dinamicamente, così da poter liberare la memoria più facilmente
//contiene anche il puntatore al prossimo e precedente giocatore, per facilitare la creazione della double linked list
typedef struct PlayerInfo{
  int isActive;
  int isRegistered;
  char nickname[13];
  int score;
  char *asyncMsg;
  char *sockReadBuffer;
  char *sockWriteBuffer;
  char **usedWords;
  int nUsedWords;
  pthread_mutex_t asyncMsgMutex;
  pthread_mutex_t sockOutMutex;
  pthread_cond_t isAsyncMsgReady;
  int clientFd;
  int isAsyncMsgPresent;
  pthread_t clientHandlerTid;
  pthread_t asyncMsgSenderTid;
  struct PlayerInfo *prev;
  struct PlayerInfo *next;
}PlayerInfo;

//la struct contiene le informazioni necessarie di un giocatore per la creazione della coda dei punteggi
typedef struct scoreQueueNode{
    char nickname[13];
    int score;
    struct scoreQueueNode *prev;
    struct scoreQueueNode *next;
}scoreQueueNode;


//definisco alcune variabili statiche per essere condivise tra threads

//listHead indica la testa della lista dei giocatori
static PlayerInfo *listHead = NULL;
//listLock fornisce un accesso in mutua esclusione alla lista
//scoreQueueMutex fornisce accesso in mutua esclusione alla coda dei punteggi
//finalScoresMutex permette accesso in mutua esclusione alla classifica finale in csv
static pthread_mutex_t listLock, scoreQueueMutex, finalScoresMutex; 
//queste pthread_cond_t regolano la sincronizzazione tra AsyncMsgSender e il thread scorer
static pthread_cond_t isQueueReadyForScore, isQueueReady;
//gameTimer e' la struct che contiene il timer corrente, insieme a isGameOngoing definisce se ci troviamo in pausa o meno e il tempo rimanente
static Timer gameTimer;
//isGameOngoing indica se la partita è in corso oppure siamo in pausa
//isMatrixFromFile indica il comportamento della generazione delle matrici
//gameLength indica la durata della partita in minuti, modificabile dal parametro opzionale
//checkedInThreads indica il numero di threads che al momento hanno fatto il checkin nella simil-barriera per lo scorer
//nClientsConnected indica il numero di clients connessi, gestito dallo scorer 
//isScorerReady è la variabile su cui si sincronizzano scorer e AsyncMsgSender
//serverFd indica il socket per accettare le connessioni
static int isGameOngoing, isMatrixFromFile = 0, gameLength = 3, checkedInThreads=0, nClientsConnected=0, isScorerReady=0, serverFd;
//gamematrix indica la matrice di gioco
static char gameMatrix[4][4];
//matrixFilename ontiene il nome del file da cui eventualmente vengono prese le matrici
//finalScores è la stringa csv della classifica finale
static char *matrixFilename, *finalScores;
//vocabulary è la radice dalla Trie che contiene il file del dizionario
static TrieNode *vocabulary;
//scoreQueueHead è il primo elemento della coda dei punteggi, è NULL e deallocato se non attiva
static scoreQueueNode *scoreQueueHead = NULL;
//file per la lettura delle matrici
static  FILE *matrixFile;
//TID del main thread
static pthread_t mainTID;

//la funzione svolge una routine di cleanup
void cleanupRoutine(int sig) {
  int retval;
  printf("\n\nAvvio la routine di cleanup...\n");
  fflush(stdout);

  //prima chiude i thread principali e il socket per accettare nuove connessioni
  SYSC(retval, close(serverFd), "close:");
  SYSC(retval, pthread_cancel(mainTID), "pthread_cancel:");
  SYSC(retval, pthread_join(mainTID, NULL), "pthread_cancel");
  printf("Cancellato il thread delle connessioni...\n");
  fflush(stdout);

  //scorro la lista dei player e chiudo tutti i threads
  PlayerInfo *current = listHead;
  while(current){
    SYSC(retval, pthread_cancel(current->clientHandlerTid), "pthread_cancel:");
    SYSC(retval, pthread_cancel(current->asyncMsgSenderTid), "pthread_cancel:");
    SYSC(retval, close(current->clientFd), "close:");
    current = current->next;
  }

  printf("Cancellati tutti i threads e chiusi i sockets.\n");
  fflush(stdout);
  //passo ora a deallocare tutta la memoria allocata precedentemente
  current = listHead;
  PlayerInfo *next = current;
  while(current){
    next = current->next;

    if(current->asyncMsg)
      free(current->asyncMsg);
    if(current->sockReadBuffer)
      free(current->sockReadBuffer);
    if(current->sockWriteBuffer)
      free(current->sockWriteBuffer);
    for(int i = 0;i < current->nUsedWords; i++){
      free(current->usedWords[i]);
    }
    free(current->usedWords);

    if(scoreQueueHead){
      scoreQueueNode *current = scoreQueueHead;
      while(current){
          scoreQueueNode *temp = current;
          current = current->next;
          free(temp);
      }
      scoreQueueHead = NULL;
    }
    
    //distruggo le mutex e pthread_cond
    SYSC(retval, pthread_mutex_destroy(&current->asyncMsgMutex), "pthread_mutex_destroy");
    SYSC(retval, pthread_mutex_destroy(&current->sockOutMutex), "pthread_mutex_destroy");
    SYSC(retval, pthread_mutex_destroy(&gameTimer.timerMutex), "pthread_mutex_destroy");
    SYSC(retval, pthread_cond_destroy(&current->isAsyncMsgReady), "pthread_mutex_destroy");

    free(current);
    current = next;
  }
  //libero i final scores
  if(finalScores)
    free(finalScores);
  //libero il nome del file delle matrici
  if(matrixFilename)
    free(matrixFilename);
  //libero la Trie con la funzione di libreria
  freeTrie(vocabulary);
  //distruggo le mutex e pthread_cond
  SYSC(retval, pthread_mutex_destroy(&listLock), "pthread_mutex_destroy");
  SYSC(retval, pthread_mutex_destroy(&scoreQueueMutex), "pthread_mutex_destroy");
  SYSC(retval, pthread_mutex_destroy(&finalScoresMutex), "pthread_mutex_destroy");
  SYSC(retval, pthread_cond_destroy(&isQueueReadyForScore), "pthread_mutex_destroy");
  SYSC(retval, pthread_cond_destroy(&isQueueReadyForScore), "pthread_mutex_destroy");

  printf("Liberata tutta la memoria.\n");
  fflush(stdout);

  printf("Routine di cleanup finita.\nEsco...\n");
  fflush(stdout);
  exit(0);
}

//funzione per l'aggiunta di un player alla lista
//acquisisce la lock e inserisce player come primo elemento
//una volta fatto rilascia la lock
void addPlayer(PlayerInfo *player){
    int retval;

    SYSC(retval,pthread_mutex_lock(&listLock),"pthread_mutex_lock:");
    player->isActive=1;
    player->next = listHead;
    if(listHead){
        listHead->prev = player;
    }

    listHead = player;

    SYSC(retval,pthread_mutex_unlock(&listLock),"pthread_mutex_lock:");
}

//funzione per rimuovere un player dalla lista
//acquisisce la lock e agisce in base ai valori di prev e next 
void removePlayerFromList(PlayerInfo *player, PlayerInfo **listHead){
    int retval;

    SYSC(retval, pthread_mutex_lock(&listLock), "pthread_mutex_lock:");

    if(player->prev)
        player->prev->next = player->next;
    else
        *listHead = player->next;
    if(player->next)
        player->next->prev = player->prev;

    SYSC(retval, pthread_mutex_unlock(&listLock), "pthread_mutex_lock:");
    free(player);
}

//la funzione rimuove un player dalla lista, occupandosi anche di liberare la memoria e distruggere le varie mutex e cond
void removeUser(PlayerInfo *player){
  int retval;
  
  if(player->asyncMsg)
    free(player->asyncMsg);
  if(player->sockReadBuffer)
    free(player->sockReadBuffer);
  if(player->sockWriteBuffer)
    free(player->sockWriteBuffer);

  for(int i=player->nUsedWords-1;i>=0;i--){
    free(player->usedWords[i]);
  }
  free(player->usedWords);

  SYSC(retval, pthread_mutex_destroy(&player->asyncMsgMutex), "sem_destroy:");
  SYSC(retval, pthread_mutex_destroy(&player->sockOutMutex), "sem_destroy:");
  SYSC(retval, pthread_cond_destroy(&player->isAsyncMsgReady), "pthread_cond_destroy:");

  SYSC(retval, close(player->clientFd), "close:");

  removePlayerFromList(player, &listHead);
  printf("Utente rimosso...esco\n");
  fflush(stdout);
  pthread_exit(0);
}

//questa macro permette di gestire correttamente le chiamate di sistema effettuate sul socket
//nel caso il socket sia stato chiuso, sia in maniera corretta che forzata, imposta lo stato del player, provocando la conseguente eliminazione
#define sockSYSC(value, command, message, self)                   \
  if ((value = (command)) == -1) {                                \
    if (errno == EPIPE || errno == ECONNRESET) {                  \
      self->isActive = 0;                                         \
    }                                                             \
    perror(message);                                              \
    exit(errno);                                                  \
  } else if (value == 0) {                                        \
    self->isActive = 0;                                           \
  }

//la funzione scorre la lista dei giocatori dopo aver ottenuto la lock
//se incontra un giocatore che è attivo con lo stesso nome passato come argomento ritorna 1, altrimenti 0
int isNameTaken(char *nickname){
  int retval;

  SYSC(retval, pthread_mutex_lock(&listLock), "pthread_mutex_lock:");
  
  PlayerInfo *current = listHead;
  while(current){
      if(strcmp(current->nickname,nickname) == 0 && current->isActive == 1){
        SYSC(retval, pthread_mutex_unlock(&listLock), "pthread_mutex_lock:");
        return 1;
      }
      current = current->next;
  }

  SYSC(retval, pthread_mutex_unlock(&listLock), "pthread_mutex_lock:");
  return 0;
}

//la funzione converte la coda in una stringa CSV, reallocandola man mano che va avanti
char* queueToCSV(){
    scoreQueueNode *current = scoreQueueHead;
    char *csv = NULL;
    if(current)
      SYSCN(csv, (char*)malloc(1*sizeof(char)), "malloc:");

    while (current) {
        char currentPlayerStats[100];
        if(current->next)
            sprintf(currentPlayerStats, "%s,%d,", current->nickname, current->score);
        else
            sprintf(currentPlayerStats, "%s,%d", current->nickname, current->score);
        SYSCN(csv, realloc(csv, strlen(csv) + strlen(currentPlayerStats) + 1), "realloc:");
        strcat(csv, currentPlayerStats);

        current = current->next;
    }
    return csv;
}

//la funzione aggiunge un nodo alla coda dei punteggi, mantenendo un ordine decrescente
void addQueueNode(char *nickaname, int score){
    scoreQueueNode *newNode;

    SYSCN(newNode, (scoreQueueNode*)malloc(sizeof(scoreQueueNode)), "malloc:");
    strcpy(newNode->nickname, nickaname);
    newNode->score = score;
    newNode->prev = NULL;
    newNode->next = NULL;
    
    if(!scoreQueueHead)
        scoreQueueHead = newNode;
    else{
        scoreQueueNode *current = scoreQueueHead;
        scoreQueueNode *previous = NULL;
        while(current && current->score>score){
            previous = current;
            current = current->next;
        }

        if(!previous){
            newNode->next = scoreQueueHead;
            scoreQueueHead->prev = newNode;
            scoreQueueHead = newNode;
        }else if(!current){
            previous->next = newNode;
            newNode->prev = previous;
        }else{
            previous->next = newNode;
            newNode->prev = previous;
            newNode->next = current;
            current->prev = newNode;
        }
    }
}

//funzione che gestisce tutti gli aspetti SINCRONI della connessione con il client
void* clientHandler(void *selfPtr){
  int retval;
  char msgType;
  PlayerInfo *self = (PlayerInfo*)selfPtr;
  //inizializzo alcuni puntatori da reallocare a NULL e alcune variabili del player
  self->sockReadBuffer = NULL;
  self->sockWriteBuffer = NULL;
  self->asyncMsg = NULL;

  self->usedWords = NULL;
  self->nUsedWords = 0;

  self->isRegistered = 0;
  self->score = 0;
  //ciclo principale che ascolta messaggi
  //si interrompe quando il player viene impostato a stato di inattivo da sockSYSC
  while(self->isActive){
    //leggo il campo del tipo di messaggio dal socket e lo salvo
    SYSCN(self->sockReadBuffer, (char*)realloc(self->sockReadBuffer,2*sizeof(char)), "realloc:");
    sockSYSC(retval, read(self->clientFd, self->sockReadBuffer, 1), "read:", self);
    msgType=self->sockReadBuffer[0];
    //leggo la lunghezza del campo length per facilitare la lettura successiva e rialloco il buffer della dimensione corretta
    sockSYSC(retval, read(self->clientFd, self->sockReadBuffer, 1),"read:",self);
    SYSCN(self->sockReadBuffer, (char*)realloc(self->sockReadBuffer, (atoi(self->sockReadBuffer) + 1)*sizeof(char)), "realloc:");
    //adesso posso leggere il campo length
    sockSYSC(retval, read(self->clientFd, self->sockReadBuffer, atoi(self->sockReadBuffer)), "read:", self);
    //rialloco correttamente il buffer per contenere il corpo del messaggio e sono pronto ad interpretarlo ed utilizzarlo correttamente
    if(atoi(self->sockReadBuffer) > 0){
      SYSCN(self->sockReadBuffer, (char*)realloc(self->sockReadBuffer, (atoi(self->sockReadBuffer) + 1)*sizeof(char)), "realloc:");
      sockSYSC(retval, read(self->clientFd, self->sockReadBuffer, atoi(self->sockReadBuffer)), "read:", self);
    }
    //lo switch permette di gestire il caso corretto di messaggio ricevuto e generare una risposta appropriata
    switch(msgType){
      //l'utente ha richiesto di essere registrato
      //se il nome è stato preso, allora restituisce errore, altrimenti OK e setta il proprio nickname
      case MSG_REGISTRA_UTENTE:{
        if(isNameTaken(self->sockReadBuffer)){
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          sockSYSC(retval, write(self->clientFd, "E10", 3), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
        }else{
          strcpy(self->nickname, self->sockReadBuffer);
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          sockSYSC(retval, write(self->clientFd, "K10", 3), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
          self->isRegistered = 1;
        }
        break;
      }
      //l'utente ha richiesto la matrice di gioco
      case MSG_MATRICE:{
        char matrixString[17];
        //se la partita è in corso, recupero la matrice e la mando indietro
        if(isGameOngoing){
          retrieveMatrix(gameMatrix, matrixString);
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          composeMessage(&self->sockWriteBuffer, MSG_MATRICE, matrixString);
          sockSYSC(retval, write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
        }else{
          //altrimenti, mando indietro il tempo rimanente alla pausa
          char time[3];
          sprintf(time, "%d", getRemainingTime(&gameTimer));
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          composeMessage(&self->sockWriteBuffer, MSG_TEMPO_ATTESA, time);
          sockSYSC(retval,write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
        }
        break;
      }
      //l'utente ha richiesto quanto tempo manca alla partita
      case MSG_TEMPO_PARTITA:{
        //faccio un semplice accesso al timer e fornisco il tempo
        char time[3];
        sprintf(time, "%d", getRemainingTime(&gameTimer));
        SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
        composeMessage(&self->sockWriteBuffer, MSG_TEMPO_PARTITA, time);
        sockSYSC(retval, write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
        SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
        break;
      }
      //l'utente sta proponendo una parola
      case MSG_PAROLA:{
        //se la partita non è in corso, restituisco il tempo rimanente all'attesa
        if(!isGameOngoing){
          char time[3];
          sprintf(time, "%d", getRemainingTime(&gameTimer));
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          composeMessage(&self->sockWriteBuffer, MSG_TEMPO_ATTESA, time);
          sockSYSC(retval, write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
          break;
        }
        //se la parola non è nel vocabolario, restituisco un errore che gestirà il client
        if(!searchTrie(vocabulary, self->sockReadBuffer)){
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          sockSYSC(retval, write(self->clientFd, "E10", 3), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_lock:");
          break;
        }
        int possiblePoints;
        if(!isWordUsed(self->sockReadBuffer, self->usedWords, self->nUsedWords) && strlen(self->sockReadBuffer)>3){
          //se la parola non è già stata utilizzata, calcolo il punteggio che sarà modificato in base anche alla presenza di 'qu'
          possiblePoints = strlen(self->sockReadBuffer) - quParse(self->sockReadBuffer);
          if(searchWord(gameMatrix, self->sockReadBuffer)){
            self->nUsedWords++;
            SYSCN(self->usedWords, (char**)realloc(self->usedWords, self->nUsedWords*(sizeof(char*))), "realloc:");
            SYSCN(self->usedWords[self->nUsedWords - 1], (char*)malloc((strlen(self->sockReadBuffer) + 1) * sizeof(char)), "malloc:");
            strcpy(self->usedWords[self->nUsedWords - 1], self->sockReadBuffer);
          } else {
            //se la parola non è quindi trovata in matrice ritorno errore che sarà gestito dal client
            SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
            sockSYSC(retval, write(self->clientFd, "E10", 3), "write:", self);
            SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
            break;
          }
        }else 
          //se la parola è già stata utilizzata dall'utente nella partita in corso il punteggio è azzerato
          possiblePoints = 0;
        //restituisco il punteggio al client

        self->score += possiblePoints;
        char actualPoints[3];
        sprintf(actualPoints, "%d", possiblePoints);

        SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
        composeMessage(&self->sockWriteBuffer, MSG_PUNTI_PAROLA, actualPoints);
        sockSYSC(retval, write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
        SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_unlock:");
        break;
      }
      case MSG_PUNTI_FINALI:{
        SYSC(retval, pthread_mutex_lock(&finalScoresMutex)," pthread_mutex_lock:");
        if(finalScores && !isGameOngoing){
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          composeMessage(&self->sockWriteBuffer, MSG_PUNTI_FINALI, finalScores);
          sockSYSC(retval, write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_lock:");
        }
        else{
          SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
          sockSYSC(retval, write(self->clientFd, "F10", 3), "write:", self);
          SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_lock:");
        }
        SYSC(retval, pthread_mutex_unlock(&finalScoresMutex)," pthread_mutex_unlock:");
        fflush(stdout);
      }
    }
    //azzero il buffer di lettura per evitare sovrascritture errate
    memset(self->sockReadBuffer, 0, strlen(self->sockReadBuffer));
  }
  return NULL;
}

//funzione di thread, presenta il proprio punteggo nella coda allo scorer
void* AsyncMsgSender(void *selfPtr){
  int retval;
  PlayerInfo *self = (PlayerInfo*)selfPtr;

  while(1){
    //se lo scorer non è pronto, il client non è registrato mi metto in attesa di essere richiamato mi metto in attesa
    //se invece il giocatore si è disconnesso chiamo la rimozione dell'utente dopo aver rilasciato la mutex
    SYSC(retval, pthread_mutex_lock(&scoreQueueMutex), "pthread_mutex_lock:");
    while(!isScorerReady || !self->isRegistered || !self->isActive){
      if(!self->isActive){
        SYSC(retval, pthread_mutex_unlock(&scoreQueueMutex), "pthread_mutex_lock:");
        removeUser(self);
      }
      SYSC(retval, pthread_cond_wait(&isQueueReadyForScore,&scoreQueueMutex), "pthread_cond_wait:");
    }

    //se il giocatore è attivo e registrato aggiungo il nodo in coda
    if(self->isActive && self->isRegistered)
      addQueueNode(self->nickname, self->score);
    //aggiorno lo stato di checkedInThreads e la presenza del messaggio
    checkedInThreads++;
    self->isAsyncMsgPresent = 0;

    //se sono l'ultimo a fare checkin lo comunico allo scorer
    if(checkedInThreads == nClientsConnected)
      SYSC(retval, pthread_cond_signal(&isQueueReady), "pthread_cond_signal:");

    SYSC(retval, pthread_mutex_unlock(&scoreQueueMutex), "pthread_mutex_unlock:");

    SYSC(retval, pthread_mutex_lock(&self->asyncMsgMutex), "pthread_mutex_lock:");
    //mi metto in attesa fino a quando il messaggio non è pronto per essere inviato
    while(!self->isAsyncMsgPresent)
      SYSC(retval ,pthread_cond_wait(&self->isAsyncMsgReady, &self->asyncMsgMutex), "pthread_cond_wait:");
    //prendo la lock, compongo il messaggio e lo invio al client
    SYSC(retval, pthread_mutex_lock(&self->sockOutMutex), "pthread_mutex_lock:");
    composeMessage(&self->sockWriteBuffer, MSG_PUNTI_FINALI, self->asyncMsg);
    sockSYSC(retval, write(self->clientFd, self->sockWriteBuffer, strlen(self->sockWriteBuffer)), "write:", self);
    SYSC(retval, pthread_mutex_unlock(&self->sockOutMutex), "pthread_mutex_lock:");
    SYSC(retval, pthread_mutex_unlock(&self->asyncMsgMutex), "pthread_mutex_unlock:");
    //azzero lo score del player e ripulisco le parole utilizzate
    self->score = 0;
    for(int i = self->nUsedWords - 1; i >= 0; i--){
      free(self->usedWords[i]);
    }
    free(self->usedWords);
    self->usedWords = NULL;
    self->nUsedWords = 0;
    memset(self->sockWriteBuffer, 0, strlen(self->sockWriteBuffer));
  }
  return NULL;
}

//il thread si occupa di gestire la partita
//avvia i timer, gestisce le pause e la matrice di gioco
//si occupa inoltre di creare il CVS per gli score finali e quindi di gestire la coda dei punteggi
void* scorer(void* arg){
  int gameDuration = *((int*)arg)*60;
  int retval;
  //come nel client, riattivo la gestione del CTRLC
  sigset_t set;
  struct sigaction sa;

  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  SYSC(retval, pthread_sigmask(SIG_UNBLOCK, &set, NULL), "pthread_sigmask:");
  sa.sa_handler = cleanupRoutine;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  SYSC(retval, sigaction(SIGINT, &sa,NULL), "sigaction:");

  //Inizializzo il timer
  initTimer(&gameTimer);
  //se impostato per lettura da file, apro il file della matrice
  if(isMatrixFromFile)
    matrixFile = fopen(matrixFilename, "r");
  //ciclo principale  
  while(1){
    //se da file, prelevo la matrice, altrimenti la genero
    if(isMatrixFromFile){
      readLine(matrixFile, gameMatrix);
    }else
      randomizeMatrix(gameMatrix);
    printf("Generata matrice\n");
    fflush(stdout);
    printMatrix(gameMatrix);
    //avvio il timer di partita in base alla gameDuration impostata, setto che la partita è iniziata e dormo fino al termine del timer
    startTimer(&gameTimer, gameDuration);
    isGameOngoing = 1;
    sleep(getRemainingTime(&gameTimer));
    //comunico che il tempo è scaduto e avvio il timer di pausa
    write(1,"Timer scaduto\n",14);
    isGameOngoing = 0;
    startTimer(&gameTimer, 60);
    //conto il numero di clients connessi al momento
    SYSC(retval, pthread_mutex_lock(&listLock), "pthread_mutex_lock:");
    PlayerInfo *current = listHead;
    nClientsConnected=0;
    while(current){
      if(current->isActive && current->isRegistered)
        nClientsConnected++;
      current = current->next;
    }
    SYSC(retval, pthread_mutex_unlock(&listLock), "pthread_mutex_unlock:");

    //libero completamente la scoreQueue
    SYSC(retval, pthread_mutex_lock(&scoreQueueMutex), "pthread_mutex_lock:");

    if(scoreQueueHead){
      scoreQueueNode *current = scoreQueueHead;
      while(current){
          scoreQueueNode *temp = current;
          current = current->next;
          free(temp);
      }
      scoreQueueHead = NULL;
    }

    //faccio una broadcast per svegliare tutti i threads in attesa dello scorer
    isScorerReady = 1;
    checkedInThreads = 0;
    SYSC(retval, pthread_cond_broadcast(&isQueueReadyForScore), "pthread_cond_broadcast:");
    //attendo che mi venga comunicato lo stato della coda
    while(nClientsConnected != checkedInThreads)
      SYSC(retval, pthread_cond_wait(&isQueueReady, &scoreQueueMutex), "pthread_cond_wait:");
    //converto la coda in stringa in formato CSV
    SYSC(retval, pthread_mutex_lock(&finalScoresMutex)," pthread_mutex_lock:");
    finalScores = queueToCSV();
    SYSC(retval, pthread_mutex_unlock(&scoreQueueMutex), "pthread_mutex_lock:");

    isScorerReady = 0;
    //scorro tutti i player e se attivi e registrati metto il messaggio in AsyncMsgSender e glielo comunico
    current = listHead;
    nClientsConnected = 0;
    while(current){

      if(current->isActive && current->isRegistered){
        SYSC(retval, pthread_mutex_lock(&current->asyncMsgMutex), "pthread_mutex_lock:");
        SYSCN(current->asyncMsg, (char*)realloc(current->asyncMsg, (strlen(finalScores)+1)*sizeof(char)), "realloc:");
        strcpy(current->asyncMsg, finalScores);
        
        current->isAsyncMsgPresent = 1;
        SYSC(retval, pthread_cond_signal(&current->isAsyncMsgReady), "pthread_cond_signal:");
        SYSC(retval, pthread_mutex_unlock(&current->asyncMsgMutex), "pthread_mutex_unlock:");
      }
      current = current->next;
    }
    SYSC(retval, pthread_mutex_unlock(&finalScoresMutex)," pthread_mutex_unlock:");
    //finisco di dormire per il resto del tempo del timer
    sleep(getRemainingTime(&gameTimer));
  }

  return NULL;
}

int main(int argc, char **argv) {
  // il rndSeed indica il seed per la generazione di tutti i numeri casuali
  // lo inizializzo a time(NULL) nel caso non sia impostato dal parametro opzionale
  // matrixCheck serve a controllare se tra i parametri c'e' sia --matrici che --seed
  int portNumber, rndSeed = time(NULL), matrixCheck = 0;
  // inizializzo il dizionario standard nel caso non sia modificato dal parametro opzionale
  char *dictionaryFilename;
  SYSCN(dictionaryFilename, (char *)malloc(19 * sizeof(char)), "Malloc:");
  strcpy(dictionaryFilename, "dictionary_ita.txt");

  // effettuo un controllo sul numero dei parametri di input tenendo conto di quelli opzionali
  if (argc != 3 && argc != 5 && argc != 7 && argc != 9 && argc != 11) {
    printf("Errore nel numero degli argomenti!\nUtilizzo corretto: ./paroliere_srv nome_server porta_server [--matrici data_filename] [--durata durata_gioco_in_minuti] [--seed rnd_seed] [--diz dizionario]\n");
    exit(EXIT_FAILURE);
  }

  //--------------------------------------------------------------------------
  // controllo se gli argomenti sono stati inseriti in maniera corretta
  // controllo prima gli argomenti obbligatori
  requiredArgs(argv[2], &portNumber);
  // controllo gli argomenti facoltativi
  for (int i = 3; i < argc; i = i + 2) {
    optionalArgs(argv[i], argv[i + 1], &isMatrixFromFile, &gameLength, &rndSeed, dictionaryFilename, &matrixCheck, &matrixFilename);
  }
  printf("Server creato con i seguenti parametri:\n\nserverName: %s\nserverPort: %d\nisMatrixFromFile: %d\ngamLength: %d\nseed: %d\ndizionario: %s\n\n", argv[1], portNumber, isMatrixFromFile, gameLength, rndSeed, dictionaryFilename);

  srand(rndSeed);
  
  //---------------------------------------------------------------------------
  // preparo il dizionario per la ricerca delle parole
  // inizializzo le variabili necessarie per leggere dal file e creare la struttura adatta
  char buffer[26];
  vocabulary = createNode('*');
  // creo il FILE e apro il dizionario
  FILE *dictionaryFile;
  SYSCN(dictionaryFile, fopen(dictionaryFilename, "r"), "Fopen:");
  // inizio a leggere le parole e inserirle nella Trie
  while (fscanf(dictionaryFile, "%s", buffer) == 1) {
    insertWord(vocabulary, buffer);
  }
  // ho caricato con successo in memoria il dizionario
  // posso ora chiudere il file
  if (fclose(dictionaryFile) == EOF) {
    perror("Fclose:");
    exit(EXIT_FAILURE);
  }

  mainTID = pthread_self();

  int retval;
  sigset_t set;

  sigfillset(&set);

  SYSC(retval, pthread_sigmask(SIG_BLOCK, &set, NULL),"pthread_sigmask");

  pthread_t scorerTid;
  //avvio il thread che gestisce la partita
  SYSC(retval, pthread_create(&scorerTid, NULL, scorer, &gameLength), "pthread_create:");


  //inizio ad accettare le connessioni
  struct addrinfo hints, *availableAddresses, *rp;

  SYSC(retval, pthread_mutex_init(&listLock, NULL), "pthread_mutex_init:");
  SYSC(retval, pthread_mutex_init(&scoreQueueMutex, NULL), "pthread_mutex_init:");
  SYSC(retval, pthread_mutex_init(&finalScoresMutex, NULL), "pthread_mutex_init:");
  SYSC(retval, pthread_cond_init(&isQueueReadyForScore, NULL), "pthread_mutex_init:");
  SYSC(retval, pthread_cond_init(&isQueueReady, NULL), "pthread_mutex_init:");

  //inizializzo la memoria di hints a 0 e setto alcuni suoi parametri
  //ai_family con AF_UNSPEC indica che la famiglia e' non specificata, sono quindi accettati sia IPV4 che IPV6
  //ai_socktype con SOCK_STREAM indica che il socket e' di tipo stream (TCP)
  //ai_flags su AI_PASSIVE indica che iil socket sara' utilizzato per accettare connessioni
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  //uso getaddrinfo per ottenere informazioni sull'indirizzo del server
  //argv[1] per legare il server al nome fornito dall'input
  //hints permette di specificare i criteri di ricerca degli indirizzi
  //availableAddresses conterra' il primo elemento della lista di indirizzi individuati
  if((retval = getaddrinfo(argv[1], argv[2], &hints, &availableAddresses))!=0){
      printf("getaddrinfo: %s\n", gai_strerror(retval));
      exit(EXIT_FAILURE);
  }
  //ciclo tutta la lista fornita da getaddrinfo
  //cerchiamo di creare un socket e di bindarlo, se questo non e' possibile passiamo al prossimo elemento della lista
  for(rp = availableAddresses; rp != NULL; rp = rp->ai_next){
      serverFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if(serverFd == -1)
          continue;
      if(bind(serverFd, rp->ai_addr, rp->ai_addrlen) == -1){
          SYSC(retval, close(serverFd), "close:");
          continue;
      }
      break;
  }
  //se dopo aver visto tutta la lista non siamo riusciti a creare un socket e bindarlo, possiamo uscire
  if(rp == NULL){
      printf("Non sono riuscito a fare la bind\n");
      exit(EXIT_FAILURE);
  }
  //una volta finito posso allora liberare la lista di availableAddresses
  freeaddrinfo(availableAddresses);
  //faccio la listen e specifico il max pending
  SYSC(retval, listen(serverFd,10), "listen");
  //utilizzo sockaddr_storage per contenere l'indirizzo del client piu' flessibilmente
  //questo supporta ogni tipo di socket
  //prendo la dimensione del clientAddress e faccio la accept castando a sockaddr*
  //per ogni connessione avvenuta inizializzo la struct PlayerInfo e la popolo
  //creo il thread per la gestione e passo alla prossima connessione
  struct sockaddr_storage clientAddress;
  socklen_t addrSize = sizeof(clientAddress);
  while(1){
    
    PlayerInfo *newPlayer;
    SYSCN(newPlayer, (PlayerInfo*)malloc(sizeof(PlayerInfo)), "malloc:");
    newPlayer->next = NULL;
    newPlayer->prev = NULL;

    SYSC(retval,write(1, "Sto accettando connessioni...\n",30), "write:");

    SYSC(newPlayer->clientFd, accept(serverFd, (struct sockaddr*)&clientAddress, &addrSize),"accept:");

    addPlayer(newPlayer);

    SYSC(retval, pthread_mutex_init(&newPlayer->asyncMsgMutex, NULL), "pthread_mutex_init:");
    SYSC(retval, pthread_mutex_init(&newPlayer->sockOutMutex, NULL), "pthread_mutex_init:");
    SYSC(retval, pthread_cond_init(&newPlayer->isAsyncMsgReady ,NULL), "pthread_mutex_init:");
    
    SYSC(retval, pthread_create(&newPlayer->clientHandlerTid, NULL, clientHandler, newPlayer), "pthread_create:");
    SYSC(retval, pthread_detach(newPlayer->clientHandlerTid), "pthread_detatch:");
    SYSC(retval, pthread_create(&newPlayer->asyncMsgSenderTid, NULL, AsyncMsgSender, newPlayer), "pthread_create:");
    SYSC(retval, pthread_detach(newPlayer->asyncMsgSenderTid), "pthread_detatch:");

    SYSC(retval, write(1, "Nuova connessione stabilita\n\n", 29), "write:");
  } 
  return 0;
}