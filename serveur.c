/* version 0 (PM, 16/4/17) :
	Le serveur de conversation
	- cr�e un tube (fifo) d'�coute (avec un nom fixe : ./ecoute)
	- g�re un maximum de maxParticipants conversations : 
		* accepter les demandes de connexion tube d'�coute) de nouveau(x) participant(s)
			 si possible
			-> initialiser et ouvrir les tubes de service (entr�e/sortie) fournis 
				dans la demande de connexion
		* messages des tubes (fifo) de service en entr�e 
			-> diffuser sur les tubes de service en sortie
	- d�tecte les d�connexions lors du select
	- se termine � la connexion d'un client de pseudo "fin"
	Protocole
	- suppose que les clients ont cr�� les tube d'entr�e/sortie avant
		la demande de connexion, nomm�s par le nom du client, suffix�s par _C2S/_S2C.
	- les �changes par les tubes se font par blocs de taille fixe, dans l'id�e d'�viter
	  le mode non bloquant.
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <strings.h>
#include <signal.h>


#define NBPARTICIPANTS 5 	/* seuil au del� duquel la prise en compte de nouvelles
								 connexions sera diff�r�e */
#define TAILLE_MSG 128		/* nb caract�res message complet (nom+texte) */
#define TAILLE_NOM 25		/* nombre de caract�res d'un pseudo */
#define NBDESC FD_SETSIZE-1	/* pour un select �ventuel
								 (macros non definies si >= FD_SETSIZE) */
#define TAILLE_TUBE 512		/*capacit� d'un tube */

typedef struct ptp {
    bool actif;
    char nom [TAILLE_NOM];
    int in;		/* tube d'entr�e */
    int out;	/* tube de sortie */
} participant;

typedef struct dde {
    char nom [TAILLE_NOM];
} demande;

static const int maxParticipants = NBPARTICIPANTS+1+TAILLE_TUBE/sizeof(demande);

participant participants [NBPARTICIPANTS+1+TAILLE_TUBE/sizeof(demande)]; /*maxParticipants*/
char buf[TAILLE_TUBE];
int nbactifs = 0;

char * buf0;   /* pour parcourir le contenu d'un tube, si besoin */
int ecoute;		/* descripteur d'�coute */

/*====================================================================*/
/*Ecrire un message de taille buf dans le descripteur de fichier desc.*/
/* Parametres : desc : descripteur de fichier
 * 			  : mes : message a envoyer
 * 			  : buf : taille du message
 * Pr� : /
 * Post : /
 * Exc : /
 */
 
void ecrire(int desc, char * mes, int buf) {
	int necrits;
	char *buffer;
	
	/*R�cup�ration de la taille de fichier � ecrire.*/
	int taille = strlen(mes);
	buffer = mes;
	
	/*Ecriture dans desc*/
	do {
		necrits = write(desc,buffer,buf);
		taille = taille - necrits;
		buffer = buffer +necrits;
	} while(taille >= 0);
}
/*====================================================================*/
void effacer(int i) {
    participants[i].actif = false;
    bzero(participants[i].nom, TAILLE_NOM*sizeof(char));
    participants[i].in = -1;
    participants[i].out = -1;
}
/*====================================================================*/
void diffuser(char *dep) {
	int i; /*Indice de parcours de la liste de participant.*/
	
	for(i=0;i<nbactifs;i++){
		/*Envoie du message au participant.*/
		ecrire(participants[i].out,dep,TAILLE_MSG);
	}
}
/*====================================================================*/
void fermer_desc( int desc) {
	if (close(desc) == -1) {
		perror("Close :");
		exit(2);
	}
}		
/*====================================================================*/

void desactiver (int p) {
/* traitement d'un participant d�connect� */
int i; /*Indice de parcours de la liste.*/
char  msg[TAILLE_MSG]; /*msg de d�connexion.*/
	
	/*Mise a zero.*/
	bzero(msg, (TAILLE_MSG)*sizeof(char));
    
    /*Cr�ation du msg de  d�connexion.*/
    strncpy(msg, "[service] ",11);
    strncat(msg, participants[p].nom, strlen(participants[p].nom));
    strncat(msg, " quitte la conversation",23);
    
    /*Fermeture des tubes associ�s.*/
    fermer_desc(participants[p].in);
    fermer_desc(participants[p].out);
    
    /*Effacer le participant.*/
    effacer(p);
    
    /*D�calage des autres participants.*/
     for (i = p; i < nbactifs-1; i++) {
        participants[p] = participants[p+1];
    }
    
    /*Suppresion du dernier membre actif*/
    if (nbactifs > 1) {
		effacer(nbactifs-1);
	}
	
	/*Gestion de nombre d'actif.*/
	nbactifs--;
	
	/*Diffusion du msg aux autres participants.*/
	diffuser(msg);
}
/*====================================================================*/

void ajouter(char *dep) {
/*  Pr� : nbactifs < maxParticpants
	
	Ajoute un nouveau participant de pseudo dep.
	Si le participant est "fin", termine le serveur.*/

	char msg[TAILLE_MSG]; /* message de connexion */
	
	char tubeC2S [TAILLE_NOM+5];	/* pour le nom du tube C2S */
    char tubeS2C [TAILLE_NOM+5];	/* pour le nom du tube S2C */
	
	 /*Mise a zero.*/
	bzero(msg, (TAILLE_MSG)*sizeof(char));
    
    /*Cr�ation du message de  connexion.*/
    strncpy(msg, "[service] ",11);
    strncat(msg, dep, strlen(dep));
    strncat(msg, " rejoint la conversation",24);
	 
	 
	 /* cr�ation des tubes de service */
       
       /*Mise a zero.*/
       bzero(tubeC2S,TAILLE_NOM+5);
       bzero(tubeS2C,TAILLE_NOM+5);
       
       /*Concat�nation avec le pseudo.*/
		strncpy(tubeC2S,dep,strlen(dep));
		strncpy(tubeS2C,dep,strlen(dep));
		
		strncat(tubeC2S,"_C2S",4);
		strncat(tubeS2C,"_S2C",4);
		
		/*Gestion du nom.*/
		strncpy(participants[nbactifs].nom,dep,strlen(dep));
		
		/*Gestion d'activit�.*/
		participants[nbactifs].actif = true;
		
		/*Ouverture des tubes.*/
		if((participants[nbactifs].out = open(tubeS2C, O_WRONLY)) == -1) {
			perror("Open : ");
			exit(2);
		}
		
		if((participants[nbactifs].in = open(tubeC2S, O_RDONLY)) == -1) {
			perror("Open : ");
			exit(2);
		}
		
		/*Envoie de confirmation de connexion.*/
		ecrire(participants[nbactifs].out,"OK",TAILLE_MSG);
		
		/*Gestion de nbactifs*/
		nbactifs++;
		
		/*Diffusion du message.*/
		diffuser(msg);
}/*====================================================================*/
/*Ferme le serveur en d�connectant au pr�alable tous les participants.*/

void FermetureServeur () {
	int i;
/*Diffusion de message de deconnexion afin de deconnecter tous les participants.*/
		diffuser("[service] Fermeture Serveur");
		diffuser("deconnexion");

		/*Suppression de tous les participants.*/
		for(i=0;i<nbactifs;i++) {
			effacer(i);
		}
		
		/*Lib�ration de m�moire.*/
		free(buf0);
		
		/*Fermeture tube d'ecoute.*/
		fermer_desc(ecoute);
		
		/*Suppression tube d'�coute.*/	
		if (unlink("./ecoute") == -1) {
		perror("Unlink : ");
		exit(2);
		}
	}

/*====================================================================*/
/*Initialise le Traitant pour SIGSTP et SIGINT*/
void Initraitant() {
		signal(SIGTSTP,FermetureServeur);
		signal(SIGINT,FermetureServeur);
	}
/*====================================================================*/

int main () {
    int i,nlus;
    fd_set readfds; /* ensemble de descripteurs �cout�s par un select �ventuel */
    
    char  pseudo[TAILLE_NOM]; /*Pseudo de l'utilisateur actuel.*/

    /* cr�ation (puis ouverture) du tube d'�coute */
    mkfifo("./ecoute",S_IRUSR|S_IWUSR); /* mmn�moniques sys/stat.h: S_IRUSR|S_IWUSR = 0600*/
    ecoute=open("./ecoute",O_RDWR);

    for (i=0; i<maxParticipants; i++) {
        effacer(i);
    }
    
    /*Allocation m�moire.*/
    if ((buf0 = malloc((TAILLE_MSG)*sizeof(char))) == NULL) {
		perror("Malloc :");
		exit(1);
	}
	
	/*Mise a zero.*/
	bzero(pseudo,TAILLE_NOM);

    while (strcmp(pseudo,"fin") != 0) {
		Initraitant();
        printf("participants actifs : %d\n",nbactifs);
		/* boucle du serveur : traiter les requ�tes en attente 
			sur le tube d'�coute et les tubes d'entr�e*/
		
		/*Vider la liste de descripteur en lecture.*/
		FD_ZERO(&readfds);	
		
		/*Ecoute des participants actifs.*/
		for(i =0; i<nbactifs;i++) {
			FD_SET(participants[i].in,&readfds);
		}
		
		/*Ecoute des requetes de connexions si possibles.*/
		if (nbactifs < NBPARTICIPANTS) {
			FD_SET(ecoute,&readfds);
		}
		
		/*Selections des tubes � �couter.*/
		if (select(NBDESC,&readfds,NULL,NULL,NULL) < 0) {
				printf("Erreur SELECT .\n");
				exit(1);
		}
		
		/*Lecture sur les descripteurs.*/
			/*Cas message participant.*/
			for (i=0;i<nbactifs;i++) {
				if (FD_ISSET(participants[i].in,&readfds)) {
					/*Lecture du message.*/
					
					if ((nlus = read(participants[i].in,buf0,TAILLE_MSG)) <0) {
						perror("Read : ");
						exit(2);
					}
					/*Cas demande de deconnexion.*/
					if (strcmp(buf0,"au revoir")==0) {
						desactiver(i);
					} else {
						/*Cas message.*/
						diffuser(buf0);
					}
				}
			}
			/*Cas message d'ecoute.*/
			if (FD_ISSET(ecoute, &readfds) && nbactifs < NBPARTICIPANTS) {
				
				/*Lecture sur ecoute.*/
				if (read(ecoute, pseudo, TAILLE_NOM) > 0) {
					
					/*Traitement du message si non terminaison du serveur.*/
					if (strcmp(pseudo,"fin")!= 0) {
						
						/*Connexion de l'utilisateur.*/
						ajouter(pseudo);
					}
				}
			}
		}
		FermetureServeur(ecoute,buf0);			
	return 0;				
}

