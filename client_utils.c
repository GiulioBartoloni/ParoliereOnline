//la libreria contiene alcune funzioni utilizzate dal client
#include <stdio.h>
#include <string.h>
#include <unistd.h>

//la funzione controlla semplicemente se una parola è "legale"
//riduce il potenziale numero di richieste inutili fatte al server
int isWordLegal(char* word){
    for(int i = 0; i < strlen(word); i++)
        if(word[i] > 'z' || word[i] < 'a')
            return 0;
    return 1;
}

//a partire da una stringa che rappresenta una matrice, questa funzione la printa gestendo correttamente il caso del @=qu
void printMatrixFromString(char *matrixString){
    printf("+----+----+----+----+\n");
    for(int i = 0; i < 4; i++){
        for(int j = 0; j < 4; j++){
            if(matrixString[i*4+j] == '@')
                printf("| qu ");
            else
                printf("| %c  ", matrixString[i*4+j]);
        }
        printf("|\n");
        printf("+----+----+----+----+\n");
    }
    fflush(stdout);
}

//la funzione prende in input una stringa CSV di "nickname,punteggio,..." e la printa in stdout
void printCSVValues(char *csv){
    char *token = strtok(csv, ",");
    printf("Congratulazioni a %s, il vincitore dello scorso round!\n", token);
    fflush(stdout);
    write(1, "\nPUNTEGGI FINALI\n", 17);
    //continuo ad iterare fino ad avere un token NULL
    while (token != NULL) {
        //recupero il giocatore, lo scrivo aggiungendo degli spazi per chiarezza
        write(1, "   ", 3);
        write(1, token, strlen(token));
        for(int i = 0; i < 18 - strlen(token); i++)
            write(1, " ", 1);

        //faccio il token per il punteggio e lo scrivo
        token = strtok(NULL, ",");
        write(1, token, strlen(token));
        write(1, "\n", 1);
        
        //faccio il token per il prossimo giocatore
        token = strtok(NULL, ",");
    }
}