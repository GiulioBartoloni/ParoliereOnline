#DICHIARAZIONE DI PARTI CONDIVISE DEI COMANDI DI COMPILAZIONE

#compiler
CC = cc

#flags del compiler
CFLAGS = -Wall -g -pedantic -pthread
#------------------------------------------------------------------------------------
#DICHIARAZIONE DI NOMI DA UTILIZZARE PER I COMANDI DI COMPILAZIONE

#nomi dei file di libreria
LFNMS2 = shared_functions
LFNMS3 = serverUtils
LFNMS4 = trie
LFNMS5 = timer
LFNMS6 = matrix
LFNMS7 = client_utils

#file eseguibili da creare
EXE1 = paroliere_srv
EXE2 = paroliere_cl

#file oggetto dei file di libreria e da creare
EOBJS1 = $(EXE1).o
EOBJS2 = $(EXE2).o
LOBJS2 = $(LFNMS2).o
LOBJS3 = $(LFNMS3).o
LOBJS4 = $(LFNMS4).o
LOBJS5 = $(LFNMS5).o
LOBJS6 = $(LFNMS6).o
LOBJS7 = $(LFNMS7).o

#file sorgente dei file di libreria e da creare
ESRCS1 = $(EXE1).c
ESRCS2 = $(EXE2).c
LSRCS2 = $(LFNMS2).c
LSRCS3 = $(LFNMS3).c
LSRCS4 = $(LFNMS4).c
LSRCS5 = $(LFNMS5).c
LSRCS6 = $(LFNMS6).c
LSRCS7 = $(LFNMS7).c
#------------------------------------------------------------------------------------
#REGOLE DI COMPILAZIONE

#regola di compilazione di default
all: $(EXE1) $(EXE2)

#regola di compilazione per il server
server: $(EXE1)

$(EXE1): $(EOBJS1) $(LOBJS2) $(LOBJS3) $(LOBJS4) $(LOBJS5) $(LOBJS6)
	$(CC) $(CFLAGS) -o $(EXE1) $(EOBJS1) $(LOBJS2) $(LOBJS3) $(LOBJS4) $(LOBJS5) $(LOBJS6)

$(EOBJS1): $(ESRCS1) 
	$(CC) $(CFLAGS) -c $(ESRCS1)  

#cregola di compilazione per il client
client: $(EXE2) $(LOBJS7)

$(EXE2): $(EOBJS2) $(LOBJS2) $(LOBJS7)
	$(CC) $(CFLAGS) -o $(EXE2) $(EOBJS2) $(LOBJS2) $(LOBJS7)

$(EOBJS2): $(ESRCS2) 
	$(CC) $(CFLAGS) -c $(ESRCS2) 

	
#regole di compilazione per le librerie
$(LOBJS7): $(LSRCS7) 
	$(CC) $(CFLAGS) -c $(LSRCS7) 

$(LOBJS6): $(LSRCS6) 
	$(CC) $(CFLAGS) -c $(LSRCS6) 

$(LOBJS5): $(LSRCS5) 
	$(CC) $(CFLAGS) -c $(LSRCS5) 

$(LOBJS4): $(LSRCS4) 
	$(CC) $(CFLAGS) -c $(LSRCS4) 

$(LOBJS3): $(LSRCS3) 
	$(CC) $(CFLAGS) -c $(LSRCS3)  

$(LOBJS2): $(LSRCS2) 
	$(CC) $(CFLAGS) -c $(LSRCS2) 

#regola per pulire tutto
cleanall:
	rm -f $(LOBJS2) $(LOBJS3) $(LOBJS4) $(LOBJS5) $(LOBJS6) $(LOBJS7) $(EOBJS1) $(EXE1) $(EOBJS2) $(EXE2) 
#regola per pulire il server
cleanserver:
	rm -f $(LOBJS2) $(LOBJS3) $(LOBJS4) $(LOBJS5) $(LOBJS6) $(EOBJS1) $(EXE1) 
#regola per pulire il client
cleanclient:
	rm -f $(LOBJS2) $(LOBJS3) $(LOBJS7) $(EOBJS2) $(EXE2) 

	

