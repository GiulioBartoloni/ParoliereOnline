// la libreria contiene le funzioni relative alla gestione della matrice di gioco per il server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//la funzione "formatta" una matrice per essere messa in una stringa
void retrieveMatrix(char matrix[4][4], char *result){
    int index = 0;
    for(int i = 0; i < 4; i++){
        for(int j = 0; j < 4; j++){
            result[index] = matrix[i][j];
            index++;
        }
    }
    result[16] = '\0';
}

//la funzione legge una linea dal file presentato e la mette nella matrice
//se arrivati alla fine del file, facciamo rewind
void readLine(FILE *matrixFile, char matrix[4][4]){
    char word[3];
    for(int i = 0; i < 4; i++){
        for(int j = 0; j < 4; j++){
            fscanf(matrixFile, "%s", word);
            if(strcmp(word, "Qu") == 0)
                matrix[i][j] = '@';
            else
                matrix[i][j] = word[0] + 32;
        }
    }
    //testo per capire se alla fine del file
    int nRead = fscanf(matrixFile, "%s", word);
    if(feof(matrixFile))
        rewind(matrixFile);
    else
        fseek(matrixFile, ftell(matrixFile) - nRead - 1, SEEK_SET);
}

//funzione che printa in output la matrice gestendo in maniera corretta il caso di @=qu
void printMatrix(char matrix[4][4]){
    printf("+----+----+----+----+\n");
    for(int i = 0; i < 4 ; i++){
        for(int j = 0; j < 4; j++){
            if(matrix[i][j] == '@')
                printf("| qu ");
            else
                printf("| %c  ", matrix[i][j]);
            
        }
        printf("|\n");
        printf("+----+----+----+----+\n");
    }
}

//la funzione randomizza la matrice
void randomizeMatrix(char matrix[4][4]){
    char randomCharacter;
    for(int i = 0; i < 4; i++){
        for(int j = 0; j < 4; j++){
            if((randomCharacter = 'a' + rand() %26) == 'q')
                randomCharacter = '@';
            matrix[i][j] = randomCharacter;
        }
    }
}

//controlla semplicemente se la posizione proposta è legale e non è già stata marcata come visitata
int isCellValid(int i, int j, int visited[4][4]){
    return !(i<0 || i>3 || j<0 || j>3 || visited[i][j]);
}

//la funzione implementa una simil-DFS per la ricerca della parola nella matrice
int crawlMatrix(char matrix[4][4], char *word, int wordIndex, int row, int col, int visited[4][4]){
    
    if(wordIndex == strlen(word))
        return 1;
    if(!isCellValid(row, col, visited) || matrix[row][col] != word[wordIndex])
        return 0;

    visited[row][col] = 1;
    int rowOptions[] = {1,-1,0,0};
    int colOptions[] = {0,0,1,-1};

    for(int i = 0; i < 4; i++){
        if(crawlMatrix(matrix, word, wordIndex + 1, row + rowOptions[i], col + colOptions[i], visited))
            return 1;
    }

    visited[row][col] = 0;
    return 0;
}

//per ogni possibile iniziale accettabile, cerco in simil-DFS la parola
int searchWord(char matrix[4][4], char* word){
    int visited[4][4] = {0};

    for(int i = 0; i < 4; i++){
        for(int j = 0; j < 4; j++){
            if(matrix[i][j] == word[0])
                if(crawlMatrix(matrix, word, 0, i, j, visited))
                    return 1;
        }
    }
    return 0;
}

//la funzione prende in input una stringa e la modifica sostituendo tutte le occorrenze di '@' con "qu"
//ritorna il numero di sostituzioni effettate, questo torna comodo per il calcolo dei punteggi delle parole
int quParse(char *string){
    int length = strlen(string), substitutions=0;

    for(int i = 0; i < length - 1; i++){
        if (string[i] == 'q' && string[i + 1] == 'u') {
            string[i] = '@';
            for (int j = i + 1; j < length - 1; j++) 
                string[j] = string[j + 1];
            length--;
            substitutions++;
        }   
    }
    string[length] = '\0';
    return substitutions;
}