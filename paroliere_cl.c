//librerie del C
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

//librerie personali
//NB: fare riferimento ai rispettivi file per le documentazioni
#include "client_utils.h"
#include "macros.h"
#include "shared_functions.h"

//definisco il massimo input che leggo dallo stdin per i comandi
#define MAX_INPUT_LENGTH 100

//definisco alcune variabili statiche per la condivisione tra threads

//asyncMsg,syncMsg contengono i messaggi ricevuti passati al lato principale del client dal receiver
//sockInBuffer e sockOutBuffer sono semplici buffer per lettura o scrittura su socket
//syncMsgType facilita il passaggio di informazioni quando l'unica parte rilevante del messaggio è il tipo
static char *asyncMsg, *syncMsg, *sockInBuffer, *sockOutBuffer, syncMsgType;
//serverFd è il fd del socket
//writePrompt e printExtraTab servono puramente ragioni estetiche nell'output delle classifiche
static int serverFd, writePrompt = 1, printExtraTab = 1; 
//i semafori e la mutex servono a gestire la sincronizzazione tra threads
static sem_t asyncMsgReceived, syncMsgReceived;
static pthread_mutex_t stdOutMutex;
//i pthread_t permettono una corretta chiusura dei threads nella fase del cleanup quando il client si disconnette via CTRLC oppure fine
static pthread_t receiverTid, mainTid;



//la funzione si occupa del cleanup quando viene ricevuto SIGINT oppure viene chiamato il comando fine
void cleanupRoutine(int sig) {
    int retval;
    //salvo il tid del thread corrente che sta eseguendo la funzione
    pthread_t myTid = pthread_self();
    printf("\n\nProcedo con la routine di cleanup...");
    
    //con questa serie di comparison chiudo tutti i threads tranne quello che sta eseguendo la funzione
    if(myTid != receiverTid){
        SYSC(retval, pthread_cancel(receiverTid), "pthread_cancel:");
        SYSC(retval, pthread_join(receiverTid,NULL), "pthread_join:");
    }
    if(myTid != mainTid){
        SYSC(retval, pthread_cancel(mainTid), "pthread_cancel:");
        SYSC(retval, pthread_join(mainTid,NULL), "pthread_join:");
    }

    //faccio la pulizia di tutto ciò che è stato allocato dinamicamente
    if(asyncMsg)
        free(asyncMsg);
    if(sockInBuffer)
        free(sockInBuffer);
    if(sockOutBuffer)
        free(sockOutBuffer);
    //distruggo semafori e mutexes
    SYSC(retval, sem_destroy(&asyncMsgReceived), "sem_destroy:");
    SYSC(retval, sem_destroy(&syncMsgReceived), "sem_destroy:");
    SYSC(retval, pthread_mutex_destroy(&stdOutMutex), "sem_destroy:");
    //chiudo il socket
    SYSC(retval, close(serverFd), "close:");
    printf("Routine di cleanup finita.\n\nTi aspettiamo presto!\n");
    pthread_exit(0); 
}

//questa macro permette di gestire correttamente le chiamate di sistema effettuate sul socket
//nel caso il socket sia stato chiuso, sia in maniera corretta che forzata, chiama la routine di cleanup per
#define sockSYSC(value, command, message){                                              \
    value = command;                                                                    \
    if(((value == -1) && (errno == EPIPE || errno == ECONNRESET)) || value == 0){       \
    printf("\n\nIl server si è disconnesso.");                                          \
        cleanupRoutine(0);                                                              \
    }                                                                                   \
        if(errno == -1){                                                                \
        perror(message);                                                                \
        exit(errno);                                                                    \
    }                                                                                   \
}


//funzione di thread in costante attesa di messaggi ricevuti dal socket
//si occupa poi di "smistarli" tra quelli sincroni e asincroni
//quelli sincroni sono rimandati al gestore in attesa
//quelli asincroni sono gestiti personalmente
void* receiver() {
    int retval;
    char msgType;

    while (1) {
        //leggo il message type e lo salvo
        SYSCN(sockInBuffer, (char*)realloc(sockInBuffer, 2*sizeof(char)), "realloc:");
        sockSYSC(retval, read(serverFd, sockInBuffer, 1), "read:");
        msgType = sockInBuffer[0];

        if (msgType == MSG_PUNTI_FINALI) {
            //ho ricevuto la lista finale dei punteggi. leggo dal socket la lunghezza del campo length e rialloco il buffer di lettura
            sockSYSC(retval, read(serverFd, sockInBuffer, 1), "read:");
            SYSCN(sockInBuffer, (char*)realloc(sockInBuffer, (atoi(sockInBuffer) + 1)*sizeof(char)), "realloc:");
            //leggo ora il campo length e rialloco il buffer
            sockSYSC(retval, read(serverFd, sockInBuffer, atoi(sockInBuffer)), "read:");
            SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
            if(atoi(sockInBuffer)>0){
                SYSCN(asyncMsg, (char*)realloc(asyncMsg, (atoi(sockInBuffer) + 1)*sizeof(char)), "realloc:");
                //posso ora leggere il messaggio e comunicare ad async_handler che il messaggio e' arrivato
                sockSYSC(retval, read(serverFd, asyncMsg, atoi(sockInBuffer)), "read:");
                asyncMsg[atoi(sockInBuffer)] = '\0';
                //stampa e formatta correttamente la stringa CSV ricevuta
                if(printExtraTab)
                    printf("\n\nLa partita precedentemente in corso è appena terminata.\nEcco la classifica finale.\n");
                else{
                    printf("\nEcco la classifica della partita precedente.\n");
                    printExtraTab = 1;
                }
                printCSVValues(asyncMsg);
            }else{
                printf("\nErrore! Al momento non è disponibile alcuna classifica\n");
                fflush(stdout);
                if(printExtraTab==0)
                    printExtraTab=1;
            }
            //faccio una tcflush per ragioni estetiche
            tcflush(0, TCIFLUSH);
            //riscrive il prompt e rilascia la mutex
            SYSC(retval, write(1, "\n[PROMPT PAROLIERE]--> ", 23), "write:");
            SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
        } else {
            //ho letto un messaggio dei tipi sincroni
            //leggo ora la lunghezza del campo length e rialloco il buffer di lettura
            sockSYSC(retval, read(serverFd, sockInBuffer, 1), "read:");
            SYSCN(sockInBuffer, (char*)realloc(sockInBuffer, (atoi(sockInBuffer) + 1)*sizeof(char)), "realloc:");
            //leggo ora il campo length
            sockSYSC(retval, read(serverFd, sockInBuffer, atoi(sockInBuffer)), "read:");
            SYSCN(syncMsg, (char*)realloc(syncMsg, (atoi(sockInBuffer) + 1)*sizeof(char)), "realloc:");
            if (atoi(sockInBuffer) > 0) 
                sockSYSC(retval, read(serverFd, syncMsg, atoi(sockInBuffer)), "read:");
            //imposto il syncMsgType e avverto che la risposta è arrivata
            syncMsgType = msgType;
            SYSC(retval, sem_post(&syncMsgReceived), "sem_post:");   
        }
        //azzero la memoria per evitare sovrascritture che causano comportamenti inaspettati
        memset(sockInBuffer, 0, strlen(sockInBuffer));
    }
    return NULL;
}



int main(int argc, char** argv){
    int portNumber, retval, isPlayerRegistered=0;

    //effettuo un controllo sul numero dei parametri di input 
    if (argc != 3) {
    printf("Errore nel numero degli argomenti!\nUtilizzo corretto: ./paroliere_cl nome_server porta_server\n");
    exit(EXIT_FAILURE);
    }

    //--------------------------------------------------------------------------
    //controllo se gli argomenti sono stati inseriti in maniera corretta
    requiredArgs(argv[2], &portNumber);

    //aggiungo la gestione del SIGINT, momentaneamente maschero tutti i segnali per riattivarlo poi nel main che se ne occuperà
    sigset_t set;
    struct sigaction sa;
    
    sigfillset(&set);

    SYSC(retval, pthread_sigmask(SIG_BLOCK, &set,NULL), "pthread_sigmask");

    struct addrinfo hints, *serverInfo, *rp;
    //inizializzo la memoria di hints a 0 e setto alcuni suoi parametri
    //ai_family con AF_UNSPEC indica che la famiglia e' non specificata, sono quindi accettati sia IPV4 che IPV6
    //ai_socktype con SOCK_STREAM indica che il socket e' di tipo stream (TCP)
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    //uso getaddrinfo per ottenere informazioni sull'indirizzo del server
    //argv[1] per indicare che vogliamo connetterci a quell'indirizzo
    //hints permette di specificare i criteri di ricerca degli indirizzi
    //availableAddresses conterra' il primo elemento della lista di indirizzi individuati
    if((retval = getaddrinfo(argv[1], argv[2], &hints, &serverInfo)) != 0){
        printf("getaddrinfo: %s\n", gai_strerror(retval));
        exit(EXIT_FAILURE);
    }
    //ciclo tutta la lista fornita da getaddrinfo
    //cerchiamo di creare un socket e di connetterci al server, se questo non e' possibile passiamo al prossimo elemento della lista
    for(rp=serverInfo; rp != NULL; rp = rp->ai_next){
        serverFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(serverFd == -1)
            continue;
        if(connect(serverFd, rp->ai_addr, rp->ai_addrlen) == -1){
            SYSC(retval, close(serverFd), "close:");
            continue;
        }
        break;
    }
    //se dopo aver visto tutta la lista non siamo riusciti a creare un socket e connetterci, possiamo uscire
    if(rp == NULL){
        printf("Non sono riuscito a fare la bind\n");
        exit(EXIT_FAILURE);
    }
    //una volta finito posso allora liberare la lista di availableAddresses
    freeaddrinfo(serverInfo);

    //inizializzo tutte le mutex, i semaofri e avvio i threads di gestione dei messaggi asincroni e della ricezione dal server
    char messageInput[MAX_INPUT_LENGTH];
    SYSC(retval, sem_init(&asyncMsgReceived,0,0), "sem_init");
    SYSC(retval, sem_init(&syncMsgReceived,0,0), "sem_init");
    SYSC(retval, pthread_mutex_init(&stdOutMutex, NULL), "pthread_mutex_init:");
    SYSC(retval, pthread_create(&receiverTid, NULL,receiver, NULL), "pthread_create:");

    SYSCN(syncMsg, realloc(syncMsg, 1*sizeof(char)), "realloc:");

    // nel thread principale sblocco SIGINT
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    SYSC(retval, pthread_sigmask(SIG_UNBLOCK, &set, NULL), "pthread_sigmask:");
    sa.sa_handler = cleanupRoutine;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    SYSC(retval, sigaction(SIGINT, &sa,NULL), "sigaction:");
    mainTid = pthread_self();

    while(1){
        memset(syncMsg, 0, strlen(syncMsg));
        //scrivo il prompt paroliere e mi metto a leggere il messaggio
        //una volta fatto elimino il /n dalla fine dell'input letto per evitare problemi
        if(writePrompt){
            SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
            SYSC(retval, write(1,"[PROMPT PAROLIERE]--> ",22), "write:");
            SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
        }else 
            writePrompt = 1;
        SYSC(retval, read(1, messageInput, MAX_INPUT_LENGTH), "read:");
        messageInput[strcspn(messageInput, "\n")] = '\0';
        if(strlen(messageInput) == 0)
            continue;
        //effettuo una tokenizzazione del messaggio letto
        //command conterra' il comando richiesto
        //argumento conterra' un eventuale argomento del comando richiesto
        //extra e' utilizzato per controllare che non sia stato utilizzata sintassi scorretta dei comandi
        char *command = strtok(messageInput, " "), *argument = strtok(NULL, " "), *extra = strtok(NULL, " ");
        //inizio a fare controlli sugli input per determinare quale comportamento devo avere
        if(!argument && !extra && strcmp(command,"aiuto") == 0){
            //l'utente ha richiesto il comando di aiuto, printo allora il messaggio
            SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
            printf("\n   --------------------------------------------------------\n"
            "   I comandi a tua disposizione sono:\n"
            "   Comando                        Descrizione\n"
            "\n"
            "   aiuto                          Mostra questo messaggio di aiuto\n"
            "   registra_utente nome_utente    Registrati al gioco\n"
            "   matrice                        Mostra matrice e tempo rimanente\n"
            "   p parola_indicata              Proponi una nuova parola\n"
            "   classifica                     Mostra la precedente classifica\n"
            "   fine                           Esci dal gioco\n"
            "   --------------------------------------------------------\n\n");
            fflush(stdout);
            SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
        } else if(argument && !extra && strcmp(command,"registra_utente") == 0){
            //l'utente ha richiesto di registrarsi, controllo prima se e' gia' registrato e dopo se il nome non e' troppo lungo
            if(isPlayerRegistered){
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                printf("\nSei già registrato!\n\n");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                fflush(stdout);
            }else if(strlen(argument) > 12){
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                printf("\nIl nome scelto è troppo lungo! Il numero massimo di caratteri è 12.\n\n");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                fflush(stdout);
            }else{
                //altrimenti, preparo il messaggio, lo scrivo al server e attendo la sua risposta che verra' letta dal receiver thread
                composeMessage(&sockOutBuffer, MSG_REGISTRA_UTENTE, argument);
                sockSYSC(retval, write(serverFd, sockOutBuffer, strlen(sockOutBuffer)),"write:");
                SYSC(retval, sem_wait(&syncMsgReceived), "sem_wait:");
                //in base alla risposta ricevuta fornisco il risultato all'utente
                //se la registrazione è avvenuta in maniera corretta, procedo a richiedere anche la matrice di gioco
                //se ricevo il tempo di attesa, avverto il giocatore, altrimenti printo la matrice e richiedo il tempo di partita
                if(syncMsgType == MSG_OK){
                    SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                    SYSC(retval, write(1, "\nTi sei registrato correttamente! Ora puoi iniziare a giocare!\n", 63), "write:");
                    SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                    isPlayerRegistered = 1;
                    sockSYSC(retval, write(serverFd,"M10",3), "write:");
                    SYSC(retval, sem_wait(&syncMsgReceived), "sem_wait:");
                    if(syncMsgType == MSG_MATRICE){
                        SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                        SYSC(retval, write(1, "Questa è la matrice di gioco. Buona fortuna!\n", 46), "write:");
                        SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                        printMatrixFromString(syncMsg);
                        sockSYSC(retval, write(serverFd, "T10", 3), "write:");
                        SYSC(retval, sem_wait(&syncMsgReceived), "sem_wait:");
                        char msgInOut[100];
                        sprintf(msgInOut, "Mancano %d secondi al termine della partita attualmente in corso\n\n", atoi(syncMsg));
                        SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                        SYSC(retval, write(1, msgInOut, strlen(msgInOut)), "write:");
                        SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                    }
                    else{
                        char msgInOut[100];
                        sprintf(msgInOut, "\nNon c'è nessuna partita in corso al momento.\nMancano %d secondi alla prossima partita\n\n", atoi(syncMsg));
                        SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                        SYSC(retval, write(1, msgInOut, strlen(msgInOut)), "write:");
                        SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                    }
                }else{
                    SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                    printf("\nIl nome utente selezionato à già in utilizzo. Prova con qualcos'altro!\n\n");
                    SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                }
            }
            fflush(stdout);
        //l'utente ha richiesto la matrice di gioco, simile al caso di prima, la richiedo e gestisco le varie risposte (partita in corso o no)
        }else if(!argument && !extra && strcmp(command,"matrice") == 0){
            if(!isPlayerRegistered){
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                SYSC(retval, write(1, "\nDevi essere registrato per poter utilizzare questo comando!\n\n", 62), "write:");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                continue;
            }
            sockSYSC(retval, write(serverFd, "M10", 3), "write:");
            SYSC(retval, sem_wait(&syncMsgReceived), "sem_wait:");
            if(syncMsgType == MSG_MATRICE){
                printMatrixFromString(syncMsg);
                sockSYSC(retval, write(serverFd, "T10", 3), "write:");
                SYSC(retval, sem_wait(&syncMsgReceived), "sem_wait:");
                char msgInOut[100];
                sprintf(msgInOut, "Mancano %d secondi al termine della partita attualmente in corso\n\n", atoi(syncMsg));
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                SYSC(retval, write(1, msgInOut, strlen(msgInOut)), "write:");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
            }else{
                char msgInOut[100];
                sprintf(msgInOut, "Non c'è nessuna partita in corso al momento.\nMancano %d secondi alla prossima partita\n\n", atoi(syncMsg));
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                SYSC(retval, write(1, msgInOut, strlen(msgInOut)), "write:");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
            }
            //l'utente ha proposto una parola
        }else if(argument && !extra && strcmp(command,"p") == 0){
            //se questo non è registrato, ritorno subito errore
            if(!isPlayerRegistered){
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                SYSC(retval, write(1, "Devi essere registrato per poter utilizzare questo comando!\n\n", 61), "write:");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                continue;
            }
            //se la parola è legale, procedo a sottoporla al server
            if(isWordLegal(argument)){
                composeMessage(&sockOutBuffer, MSG_PAROLA, argument);
                sockSYSC(retval, write(serverFd, sockOutBuffer, strlen(sockOutBuffer)), "write:");
                SYSC(retval, sem_wait(&syncMsgReceived), "sem_wait:");
                char msgInOut[100];
                //se ho ricevuto un tempo di attesa, vuol dire che siamo in pausa e la parola non è stata processata
                if(syncMsgType == MSG_TEMPO_ATTESA){
                    sprintf(msgInOut, "Non c'è nessuna partita in corso al momento.\nMancano %d secondi alla prossima partita\n\n", atoi(syncMsg));
                    SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                    SYSC(retval, write(1, msgInOut, strlen(msgInOut)), "write:");
                    SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                //se ho ottenuto un errore, questa parola non è presente in matrice o dizionario, lo comunico all'utente
                }else if(syncMsgType == MSG_ERR){
                    SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                    SYSC(retval, write(1, "Parola non riconosciuta\n\n", 25), "write:");
                    SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                }else{
                    //altrimenti, printo i punti ottenuti (saranno 0 se la parola è già stata usata)
                    if(atoi(syncMsg) > 0)
                        sprintf(msgInOut, "Hai ottento %d punti!\n\n", atoi(syncMsg));
                    else
                        sprintf(msgInOut, "Hai ottento %d punti :(\n\n", atoi(syncMsg));
                    SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                    SYSC(retval, write(1, msgInOut, strlen(msgInOut)), "write:");
                    SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                }
            }
        }else  if(!extra && !argument && strcmp(command,"classifica")==0){
            //se l'utente ha richiesto la classifica, se questo è registrato lo inoltro al server e setto di non printare il prossimo promp per ragioni estetiche
            if(!isPlayerRegistered){
                SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
                SYSC(retval, write(1, "\nDevi essere registrato per poter utilizzare questo comando!\n\n", 62), "write:");
                SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
                continue;
            }else{
                writePrompt = 0;
                printExtraTab = 0;
                sockSYSC(retval, write(serverFd, "F10", 3), "write:");
            }
        }else if(!extra && !argument && strcmp(command,"fine")==0){
            //se arrivato qui, l'utente ha richiesto di uscire, chiamo allora la funzione di cleanup
            cleanupRoutine(0);
        }else{
            //se sono arrivato fino a qui, il comando è sbagliato o nella forma incorretta
            //consiglio allora all'utente di utilizzare il comando di aiuto
            SYSC(retval, pthread_mutex_lock(&stdOutMutex), "pthread_mutex_lock:");
            SYSC(retval, write(1, "Errore! Il comando che hai inserito non esiste o l'utilizzo è scorretto.\nConsulta \"aiuto\" per ulteriori dettagli\n\n", 116), "write:");
            SYSC(retval, pthread_mutex_unlock(&stdOutMutex), "pthread_mutex_unlock:");
        }
    }
}