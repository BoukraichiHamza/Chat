/* version 0 (PM, 16/4/17) :
	Le client de conversation
	- crée deux tubes (fifo) d'E/S, nommés par le nom du client, suffixés par _C2S/_S2C
	- demande sa connexion via le tube d'écoute du serveur (nom supposé connu),
		 en fournissant le pseudo choisi (max TAILLE_NOM caractères)
	- attend la réponse du serveur sur son tube _C2S
	- effectue une boucle : lecture sur clavier/S2C.
	- sort de la boucle si la saisie au clavier est "au revoir"
	Protocole
	- les échanges par les tubes se font par blocs de taille fixe TAILLE_MSG,
	- le texte émis via C2S est préfixé par "[pseudo] ", et tronqué à TAILLE_MSG caractères
Notes :
	-le  client de pseudo "fin" n'entre pas dans la boucle : il permet juste d'arrêter 
		proprement la conversation.
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <strings.h>

#define TAILLE_MSG 128		/* nb caractères message complet (nom+texte) */
#define TAILLE_NOM 25		/* nombre de caractères d'un pseudo */
#define NBDESC FD_SETSIZE-1  /* pour le select (macros non definies si >= FD_SETSIZE) */
#define TAILLE_TUBE 512		/*capacité d'un tube */
#define NB_LIGNES 20
#define TAILLE_SAISIE 1024

char pseudo [TAILLE_NOM];
char buf [TAILLE_TUBE];
char discussion [NB_LIGNES] [TAILLE_MSG]; /* derniers messages reçus */

/*====================================================================*/

void afficher(int depart) {
    int i;
    for (i=1; i<=NB_LIGNES; i++) {
        printf("%s\n", discussion[(depart+i)%NB_LIGNES]);
    }
    printf("=========================================================================\n");
}

/*====================================================================*/

/*Ecrire un message de taille buf dans le descripteur de fichier desc.*/
/* Parametres : desc : descripteur de fichier
 * 			  : mes : message a envoyer
 * 			  : buf : taille du message
 * Pré : /
 * Post : /
 * Exc : /
 */
 
 
void ecrire(int desc, char * mes, int buf) {
	int necrits;
	char *buffer;
	
	/*Récupération de la taille de fichier à ecrire.*/
	int taille = strlen(mes);
	buffer = mes;
	
	/*Ecriture dans desc*/
	do {
		necrits = write(desc,buffer,buf);
		taille = taille - necrits;
		buffer = buffer +necrits;
	} while(taille > 0);
}

/*====================================================================*/
/*Ferme un descripteur de fichier.*/
void fermer_desc( int desc) {
	if (close(desc) == -1) {
		perror("Close :");
		exit(2);
	}
}		
/*====================================================================*/
/*Suprime un tube*/
void supprimer_tube( char * tube) {
	if (unlink(tube) == -1) {
		perror("Unlink : ");
		exit(2);
	}
}

/*====================================================================*/
/*Deconnecte le participant en fermant et supprimant les descripteurs 
 * de fichiers et les tubes passé en paramètres.*/
 
void deconnecter (char * s2c, char *c2s, int s2cdesc, int c2sdesc, int ecou) {
	
	printf("Déconnexion. \n");
	
	/*Fermeture des descripteur*/
	fermer_desc(s2cdesc);
	fermer_desc(c2sdesc);
	fermer_desc(ecou);
	
	/*Suppression des fichiers.*/
	supprimer_tube(s2c);
	supprimer_tube(c2s);
}
/*====================================================================*/
int main (int argc, char *argv[]) {
    int nlus;
    char * buf0; 					/* pour parcourir le contenu d'un tube */

    int ecoute, S2C, C2S;			/* descripteurs tubes */
    int curseur = 0;				/* position dernière ligne reçue */

    fd_set readfds; 				/* ensemble de descripteurs écoutés par le select */

    char tubeC2S [TAILLE_NOM+5];	/* pour le nom du tube C2S */
    char tubeS2C [TAILLE_NOM+5];	/* pour le nom du tube S2C */
    char pseudo [TAILLE_NOM];
    char message [TAILLE_MSG];
    char saisie [TAILLE_SAISIE];

    if (!(argc == 2) && (strlen(argv[1]) < TAILLE_NOM*sizeof(char))) {
        printf("utilisation : %s <pseudo>\n", argv[0]);
        printf("Le pseudo ne doit pas dépasser 25 caractères\n");
        exit(1);
    }

    /* ouverture du tube d'écoute */
    ecoute = open("./ecoute",O_WRONLY);
    if (ecoute==-1) {
        printf("Le serveur doit être lance, et depuis le meme repertoire que le client\n");
        exit(2);
    }
    
     /*Récupération du pseudo.*/
    strcpy(pseudo,argv[1]);
    
    /* création des tubes de service */
       
       /*Mise a zero.*/
       bzero(tubeC2S,TAILLE_NOM+5);
       bzero(tubeS2C,TAILLE_NOM+5);
       
       /*Concaténation avec le pseudo.*/
		strncpy(tubeC2S,pseudo,strlen(pseudo));
		strncpy(tubeS2C,pseudo,strlen(pseudo));
		
		strncat(tubeC2S,"_C2S",4);
		strncat(tubeS2C,"_S2C",4);
		
       /*Ouverture des tubes nommées.*/
       if(strcmp(pseudo,"fin") != 0) {
       		
			if( mkfifo(tubeS2C,S_IRUSR|S_IWUSR) == -1) {
				perror("MakeFifo : ");
				exit(1);
			}
	   
			if (mkfifo(tubeC2S,S_IRUSR|S_IWUSR) == -1) {
				perror("MakeFifo : ");
				exit(1);
			}
       }
 

    /* connexion */
    ecrire(ecoute,pseudo,TAILLE_NOM);
    

    if (strcmp(pseudo,"fin")!=0) {
    	/* client " normal " */
			/*Ouverture des tubes.*/
			if ((S2C = open(tubeS2C, O_RDONLY, 00666)) == -1) {
				perror("OPEN : ");
				exit(1);
			}
			
			if ((C2S = open(tubeC2S, O_WRONLY, 00666)) == -1) {
				perror("OPEN : ");
				exit(1);
			}
			
			/*Réponse du serveur.*/
			if((buf0 = malloc(TAILLE_MSG*sizeof(char))) == NULL) {
				perror("Malloc :");
				exit(2);
			}
			
			if (read(S2C,buf0,TAILLE_MSG) <= 0) {
				printf("Connexion refusée \n");
				exit(1);
			}
			
			free(buf0);
		/* initialisations*/
			
			/*Création du pseudo sous forme de : "[nom]")*/
		bzero(pseudo, (TAILLE_NOM)*sizeof(char));
        strncpy(pseudo, "[", 1);
        strncat(pseudo, argv[1], strlen(argv[1]));
        strncat(pseudo, "] ", 2);
        
			/*Remise a zero de saisie.*/
		bzero(saisie,(TAILLE_SAISIE)*sizeof(char));
	}
		
		
        while ((strcmp(saisie,"au revoir")!=0) && (strcmp(pseudo,"fin")!=0)) {
        /* boucle principale*/
			
        
			/*Vider la liste des descripteur en lecture.*/
			FD_ZERO(&readfds);
			
			/*Ajout des descripteur.*/
			FD_SET(0,&readfds);
			FD_SET(S2C,&readfds);
			
			/*Selection des descripteurs à écouter.*/
			if (select(NBDESC,&readfds,NULL,NULL,NULL) < 0) {
				printf("Erreur SELECT .\n");
				exit(1);
			}
			
			/*Lecture sur les descripteurs.*/
				/*Cas message serveur.*/
			if (FD_ISSET(S2C,&readfds)) {
				/*Lecture du message*/
				if ((nlus = read(S2C, message, TAILLE_MSG)) < 0) {
					perror("READ : ");
					exit(2);
				}
				
				/*Cas instructions de déconnexion serveur.*/
				if (strcmp(message,"deconnexion") == 0) {
					deconnecter(tubeS2C,tubeC2S,S2C,C2S,ecoute);
					exit(0);
				} else { 				
					/*Message normal*/
				
					/*Mise à jour de la liste de message.*/
					bzero(discussion[curseur],(TAILLE_MSG)*sizeof(char));
					strncpy(discussion[curseur],message,nlus);
				
					/*Affichage.*/
					afficher(curseur);
				
					/*Mise à jour du curseur.*/
					curseur = (curseur + 1) % NB_LIGNES;				
				}
			}
				
				/*Cas lecture sur clavier.*/
			if FD_ISSET(0,&readfds) {
				
				/*Remise a zero de saisie.*/
				bzero(saisie,(TAILLE_SAISIE)*sizeof(char));
				
				/*Lecture sur l'entrée standard.*/
				if ((nlus = read(0,saisie,TAILLE_SAISIE-strlen(pseudo))) < 0) {
					perror("Read : ");
					exit(1);
				}
				
				/* Suppression retour chariot */
				saisie[strlen(saisie)-1] = '\0';
				 
				if(strcmp(saisie,"au revoir") != 0) {
					/*Ecriture du message sous forme "[pseudo] : message ".*/
				
					/*Remise a zero.*/
					bzero(message,(TAILLE_MSG)*sizeof(char));
				
					/*Concaténation.*/
					strncpy(message,pseudo,strlen(pseudo));
					strncat(message,saisie,strlen(saisie));
				
					/*Envoie du message au serveur.*/
					ecrire(C2S,message,TAILLE_MSG);
				}
			}
					
		}
    /*Deconnexion.*/
    if (strcmp(argv[1],"fin") == 0){
		printf("Fermeture serveur. \n");
		exit(0);
	} else {
    ecrire(C2S,"au revoir",TAILLE_MSG);
    deconnecter(tubeS2C,tubeC2S,S2C,C2S,ecoute);
     printf("fin client\n");
	}
	return 0;
}
