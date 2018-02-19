/* version 0.1 (PM, 23/4/17) :
	La discussion est un tableau de messages, couplé en mémoire partagée.
	Un message comporte un auteur, un texte et un numéro d'ordre (croissant).
	Le numéro d'ordre permet à chaque participant de détecter si la discussion a évolué
	depuis la dernière fois qu'il l'a affichée.
*/
#define _POSIX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h> /* définit mmap  */
#include <strings.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/time.h>

#define TAILLE_AUTEUR 25
#define TAILLE_TEXTE 128
#define NB_LIGNES 20

#define max( x, y ) ( ((x) > (y)) ? (x) : (y) )

/* message : numéro d'ordre, auteur (25 caractères max), texte (128 caractères max) */
struct message {
  int numero;
  char auteur [TAILLE_AUTEUR];
  char texte [TAILLE_TEXTE];
};

/* discussion (20 derniers messages) */
struct message * discussion;

/* dernier message reçu */
int dernier0 = 0 ;

/* afficher la discussion */

void afficher(int start) {
	int i;
	int toprint;
	
	for (i=1; i<=NB_LIGNES; i++) {
		toprint = (start+i)%NB_LIGNES;
		if (strlen(discussion[toprint].texte) > 0) {
			printf("[%s] : %s\n", discussion[toprint].auteur, discussion[toprint].texte);
		} else {
			printf("\n");
		}
	}
	printf("=========================================================================\n");
}
/*====================================================================*/
/* traitant : rafraichir la discussion, s'il y a lieu */
void traitantUSR () {
	int i;
	int ancien;
	
	ancien = dernier0;
	
	/*Verification que le dernier message recu est affiché.*/
	for (i = 0; i < NB_LIGNES; i++) {
        dernier0 = max(discussion[i].numero, dernier0);
    }
    
    /*Affichage en cas de message manquant.*/
    if (dernier0 > ancien) {
		afficher(dernier0);
	}
	signal(SIGUSR1,traitantUSR);
}
/*====================================================================*/
/* traitant : signal alarm */
void traitantAlm () {
	kill(getpid(),SIGUSR1);
	signal(SIGALRM,traitantAlm);
	alarm(1);
}

/*====================================================================*/
/*Ecrire un message dans le premier emplacement libre de la discussion.
 * Parametres : contenu : contenu du message. 
 * 			  : auteur; auteur du message.
 * Pré : /
 * Post : /
 * Exc : /
 */

void EcrireMessage ( char * contenu , char * auteur ) {
	int i;
	struct message msg; /*Le message a créer et envoyer.*/
	
	/*Récupération du numéro du dernier message envoyé.*/
	
	for(i=0; i<NB_LIGNES; i++) {
		dernier0 = max(discussion[i].numero,dernier0);
	 }
	 
	 /*Création du message.*/
	 msg.numero = dernier0+1;
	 
	 /*Remise a zero des champs de discussions.*/
	 bzero(discussion[msg.numero%NB_LIGNES].texte,TAILLE_TEXTE);
	 bzero(discussion[msg.numero%NB_LIGNES].auteur,TAILLE_AUTEUR);
	 
	/*Remise a zero du message.*/
	bzero(msg.texte,TAILLE_TEXTE);
	bzero(msg.auteur,TAILLE_AUTEUR);
	
	strncpy(msg.texte,contenu,strlen(contenu));
	strncpy(msg.auteur,auteur,strlen(auteur));
	
	/*Ajout du message a la discussion.*/
	discussion[msg.numero%(NB_LIGNES)] = msg;
	
	/*Remise a zeros.*/
	bzero(contenu,TAILLE_TEXTE);
}

/*====================================================================*/
int main (int argc, char *argv[]) { 
	char  m [TAILLE_TEXTE];
	int taille,fdisc;
 	char qq [1];
	
	if (argc != 3) {
		printf("usage: %s <discussion> <participant>\n", argv[0]);
		exit(1);
	}

	 /* ouvrir et coupler discussion */
	if ((fdisc = open (argv[1], O_RDWR | O_CREAT , 0666)) == -1) {
		printf("erreur ouverture discussion\n");
		exit(2);
	}
	
	/*	mmap ne spécifie pas quel est le resultat d'une ecriture *apres* la fin d'un 
		fichier couple (SIGBUS est une possibilite, frequente). Il faut donc fixer la 
		taille du fichier destination à la taille du fichier source *avant* le couplage. 
		On utilise ici lseek (a la taille du fichier source) + write d'un octet, 
		qui sont deja connus.
	*/
	qq[0]='x';
	taille = sizeof(struct message)*NB_LIGNES;
 	lseek (fdisc, taille, SEEK_SET);
 	write (fdisc, qq, 1);
 	
 
 	/* Couplage Mémoire */
 	
 	if ((discussion = mmap(0,NB_LIGNES * TAILLE_TEXTE * sizeof(char),PROT_READ | PROT_WRITE, MAP_SHARED, fdisc, 0))== MAP_FAILED) {
		close(fdisc);
		perror("MMAP :");
		exit(1);
	}
	
	/*Initialisation Traitant.*/
	signal(SIGUSR1,traitantUSR);
	signal(SIGALRM,traitantAlm);
	
	/*Rendre les lectures non bloquantes*/
	fcntl(0,F_SETFL, fcntl(0,F_GETFL)|O_NONBLOCK);
	
	/*Création et envoi du message de connexion.*/
	
		/*Remise a zero.*/
	bzero(m,TAILLE_TEXTE*sizeof(char));
		
		/*Concaténation du message et du pseudo.*/
	strncpy(m, argv[2], strlen(argv[2]));
	strncat(m, " vient de se connecter.",23);
	
	
	/*Envoi du message de service.*/
	EcrireMessage(m,"service");
	
	/*Gestion du timer.*/
		alarm(1);
	
	/*Lecture du message sur l'entrée standard.*/
	while(strcmp(m,"au revoir") != 0) {
		if (read(0,m, TAILLE_TEXTE) > 0 ) {
			/*Retrait du retour chariot.*/
			m[strlen(m)-1] = '\0';
		
			/*Ajout du message aux discussions.*/
			if (strcmp(m,"au revoir") != 0) {
			
			/*Envoi du message.*/
			EcrireMessage(m,argv[2]);
			kill(getpid(),SIGUSR1);
			}
		}
	}
	
	/*Création et envoi du message de déconnexion.*/
	
		/*Remise a zero.*/
	bzero(m,TAILLE_TEXTE*sizeof(char));
		
		/*Concaténation du message et du pseudo.*/
	strncpy(m, argv[2], strlen(argv[2]));
	strncat(m, " vient de se déconnecter.",25);
	
		/*Envoi du message de service.*/
		EcrireMessage(m,"service");
		
	/*Mise a jour des discussions.*/
	kill(getpid(),SIGUSR1);
	
	/*Libération de mémoire.*/
	if (munmap(discussion,NB_LIGNES * TAILLE_TEXTE * sizeof(char)) == -1) {
		perror("MUNMAP :");
		exit(1);
	}
	
	/*Fermeture du fichier de discussion.*/
	if (close(fdisc) == -1) {
		perror("Close :");
		exit(1);
	}
	return 0;
}
