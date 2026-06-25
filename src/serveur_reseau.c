/*
 * ============================================================
 *  PUISSANCE 4 — MODULE RÉSEAU SERVEUR (serveur_reseau.c)
 * ============================================================
 * Ce programme est le PONT de communication côté Joueur 1.
 * Il tourne EN PARALLÈLE de puissance4.c sur la même machine
 * et remplit deux rôles simultanément :
 *
 *   1. Surveiller la mémoire partagée IPC pour détecter les coups
 *      joués localement et les envoyer via TCP à l'adversaire.
 *
 *   2. Recevoir les coups de l'adversaire via TCP et les déposer
 *      dans la mémoire partagée pour que puissance4.c les applique.
 *
 * POSITION DANS L'ARCHITECTURE :
 *
 *   [MACHINE J1]
 *   puissance4.c  <====IPC====>  serveur_reseau.c  <====TCP====> client_reseau.c
 *   (jeu local)   mém. partagée  (ce fichier)       réseau       (machine J2)
 *
 * Lancement : ./serveur_reseau
 *   (après avoir lancé ./puissance4 1 dans un autre terminal)
 * ============================================================
 */

#include <stdio.h>     /* printf(), perror() */
#include <stdlib.h>    /* exit() */
#include <string.h>    /* Manipulation de chaînes (inclus par bonne pratique) */
#include <unistd.h>    /* close(), usleep() */
#include <arpa/inet.h> /* API réseau : socket(), bind(), listen(), accept(), send(), recv() */
#include <sys/ipc.h>   /* IPC System V : ftok() */
#include <sys/shm.h>   /* Mémoire partagée : shmget(), shmat(), shmdt() */
#include "common.h"    /* Structures partagées : SharedMemory, TcpMessage */

int main()
{
    /* ============================================================
     * PARTIE 1 : ATTACHEMENT À LA MÉMOIRE PARTAGÉE (IPC)
     * ============================================================
     * On se connecte au segment IPC déjà créé par ./puissance4 1.
     * Les deux processus (jeu + réseau) tournent sur la MÊME machine
     * et partagent physiquement la même zone de RAM.
     *
     * Ce module n'utilise PAS IPC_CREAT : il accède à un segment
     * EXISTANT créé par puissance4.c. Si puissance4.c n'a pas encore
     * été lancé, shmget() retourne -1 et on quitte avec un message.
     * ============================================================
     */

    /*
     * ftok() : génère la clé IPC.
     * DOIT utiliser exactement les mêmes arguments que dans puissance4.c
     * pour accéder au même segment de mémoire. "/tmp" et 65 sont
     * le "mot de passe" commun entre les deux processus.
     */
    key_t key = ftok("/tmp", 65);

    /*
     * shmget() SANS le flag IPC_CREAT : accès à un segment existant.
     * Si puissance4.c n'est pas lancé, le segment n'existe pas → erreur.
     */
    int shm_id = shmget(key, sizeof(SharedMemory), 0666);
    if (shm_id == -1)
    {
        printf("Erreur : Lancez d'abord le jeu avec './puissance4 1'\n");
        return 1;
    }

    /*
     * shmat() : attache le segment à notre espace d'adressage.
     * Après cet appel, mem pointe vers la même zone RAM que le pointeur
     * mem dans puissance4.c. Toute modification est instantanément
     * visible par les deux processus, sans copie ni message.
     */
    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);

    /* ============================================================
     * PARTIE 2 : CRÉATION DU SERVEUR TCP
     * ============================================================
     * CONCEPT — Sockets TCP (côté serveur) :
     *   La communication réseau en TCP suit un protocole en 4 étapes :
     *   1. socket()  → créer un point de communication (le socket)
     *   2. bind()    → associer le socket à un port sur cette machine
     *   3. listen()  → mettre le socket en écoute (attente de clients)
     *   4. accept()  → accepter la connexion d'un client (bloquant)
     *
     * Analogie : ouvrir un bureau (socket), afficher l'adresse
     * (bind), ouvrir la porte (listen), accueillir le visiteur (accept).
     * ============================================================
     */

    /*
     * socket() : crée un socket et retourne un descripteur de fichier (int).
     *   - AF_INET     : famille d'adresses Internet IPv4
     *   - SOCK_STREAM : type de socket TCP (flux fiable, ordonné, sans perte).
     *                   L'alternative SOCK_DGRAM serait UDP (plus rapide, moins fiable).
     *   - 0           : protocole par défaut pour SOCK_STREAM → TCP automatiquement
     *
     * Un socket est traité comme un fichier sous Unix : on peut utiliser
     * read()/write() dessus, et il faut le fermer avec close() à la fin.
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    /*
     * setsockopt() : configure une option sur le socket AVANT de le lier.
     *   - SOL_SOCKET   : niveau de l'option (ici : options générales du socket)
     *   - SO_REUSEADDR : autorise la réutilisation immédiate du port après fermeture.
     *
     * SANS cette option : si on relance le serveur rapidement après une fermeture,
     * le système garde le port "réservé" quelques minutes (état TIME_WAIT du TCP)
     * et bind() échouerait avec "Address already in use".
     * AVEC cette option : on peut relancer immédiatement sans erreur.
     */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /*
     * struct sockaddr_in : structure décrivant une adresse réseau IPv4.
     * On la remplit pour indiquer sur quelle interface et quel port écouter.
     */
    struct sockaddr_in server_addr;
    server_addr.sin_family      = AF_INET;     /* Protocole IPv4 */
    server_addr.sin_addr.s_addr = INADDR_ANY;  /* Écouter sur TOUTES les interfaces
                                                * (Wi-Fi, Ethernet, loopback...) */
    server_addr.sin_port        = htons(8080); /* Port d'écoute : 8080.
                                                * htons() = "Host TO Network Short"
                                                * Convertit l'ordre des octets (endianness)
                                                * de celui du processeur (little-endian sur x86)
                                                * vers l'ordre réseau standard (big-endian). */

    /*
     * bind() : associe le socket à l'adresse/port définis dans server_addr.
     * C'est l'étape qui "réserve" le port 8080 pour ce programme.
     *
     * Le cast (struct sockaddr *) est nécessaire car bind() accepte
     * plusieurs familles d'adresses (IPv4, IPv6, Unix domain sockets...).
     * On lui passe notre struct sockaddr_in en la castant en type générique.
     */
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur bind"); /* perror() affiche le message d'erreur système */
        close(server_fd);
        return 1;
    }

    /*
     * listen() : met le socket en mode écoute passive.
     * Il attend maintenant les demandes de connexion entrantes.
     *   - server_fd : le socket à mettre en écoute
     *   - 1         : taille de la file d'attente (backlog) de connexions en attente.
     *                 Ici 1 suffit car on n'attend qu'un seul client.
     */
    if (listen(server_fd, 1) < 0)
    {
        perror("Erreur listen");
        close(server_fd);
        return 1;
    }

    printf("--- APPLICATION RESEAU (J1) ---\n");
    printf("En attente de la connexion du Joueur 2 sur le port 8080...\n");

    /*
     * accept() : BLOQUE l'exécution jusqu'à ce qu'un client se connecte.
     * Quand une connexion arrive, accept() retourne un NOUVEAU socket
     * dédié exclusivement à cette connexion. Le socket server_fd continue
     * d'écouter d'éventuelles nouvelles connexions (mais on n'en attend plus).
     *
     * Les deux arguments NULL : on ne récupère pas l'adresse IP du client.
     * Dans un vrai serveur multi-clients, on les utiliserait pour logger qui se connecte.
     */
    int client_socket = accept(server_fd, NULL, NULL);
    if (client_socket < 0)
    {
        perror("Erreur accept");
        close(server_fd);
        return 1;
    }

    printf("Joueur 2 connecte ! La synchronisation commence.\n\n");

    /* ============================================================
     * PARTIE 3 : BOUCLE DE SYNCHRONISATION
     * ============================================================
     * Ce module tourne en parallèle du jeu (puissance4.c).
     * Son unique rôle est de relayer les coups entre les deux machines.
     *
     * Logique à chaque itération :
     *   - Si c'est MON tour (J1) → surveiller local_move_to_send
     *     Dès que puissance4.c joue → envoyer le coup via TCP à J2
     *
     *   - Si c'est le tour de J2 → appeler recv() et attendre son coup
     *     Dès qu'un coup arrive → le déposer en IPC pour puissance4.c
     * ============================================================
     */

    TcpMessage msg; /* Structure du paquet réseau envoyé/reçu sur TCP */
    int my_id = 1;  /* Ce module appartient au Joueur 1 */

    /*
     * La boucle s'arrête dès que game_state != 0
     * (victoire ou match nul, décidé par puissance4.c).
     */
    while (mem->game_state == 0)
    {
        /* === MON TOUR (J1) : envoyer le coup local via TCP === */
        if (mem->player_turn == my_id)
        {
            /*
             * Surveillance du drapeau local_move_to_send.
             * Quand puissance4.c lève ce drapeau à 1, c'est le signal
             * que le joueur local vient de jouer et qu'il faut transmettre le coup.
             */
            if (mem->local_move_to_send == 1)
            {
                /* On lit la colonne jouée dans la mémoire partagée */
                msg.column     = mem->last_played_column;
                msg.win_status = mem->game_state;

                /*
                 * send() : envoie des données sur le socket TCP.
                 *   - client_socket : le socket connecté au Joueur 2
                 *   - &msg          : pointeur vers le buffer à envoyer
                 *                     (&msg = adresse de la structure msg)
                 *   - sizeof(msg)   : nombre d'octets à envoyer
                 *                     (sizeof calcule la taille à la compilation)
                 *   - 0             : flags (aucun flag spécial ici)
                 *
                 * TCP garantit que les données arrivent complètes, dans l'ordre,
                 * et sans duplication. C'est le protocole approprié pour ce jeu.
                 */
                send(client_socket, &msg, sizeof(msg), 0);
                printf("[Reseau] J'ai envoye la colonne %d a l'adversaire.\n", msg.column);

                /*
                 * On remet le drapeau à 0 pour signaler à puissance4.c
                 * que le coup a bien été transmis et qu'il peut continuer.
                 * C'est l'acquittement de l'envoi.
                 */
                mem->local_move_to_send = 0;
            }
            else
            {
                /* puissance4.c n'a pas encore joué → on attend */
                usleep(100000); /* Pause de 100 ms pour ne pas saturer le CPU */
            }
        }
        /* === TOUR ADVERSE (J2) : recevoir son coup via TCP et le déposer en IPC === */
        else
        {
            /*
             * recv() : reçoit des données depuis le socket TCP.
             *
             *   Arguments :
             *   - client_socket : le socket connecté à J2
             *   - &msg          : buffer où stocker les données reçues
             *   - sizeof(msg)   : nombre d'octets maximum à lire
             *   - 0             : flags (aucun flag spécial)
             *
             *   COMPORTEMENT BLOQUANT : recv() suspend l'exécution du programme
             *   jusqu'à ce que des données arrivent. Le processus ne consomme
             *   pas de CPU pendant cette attente (il est mis en "sommeil" par l'OS).
             *
             *   Valeurs de retour possibles :
             *   > 0 : nombre d'octets effectivement reçus → données valides
             *   = 0 : connexion fermée proprement par l'adversaire (Ctrl+C ou fin)
             *   < 0 : erreur réseau (câble débranché, timeout, etc.)
             */
            int bytes_received = recv(client_socket, &msg, sizeof(msg), 0);

            if (bytes_received == 0)
            {
                /* Déconnexion propre : J2 a fermé sa connexion */
                printf("[Reseau] L'adversaire s'est deconnecte.\n");
                mem->game_state = -1; /* On force la fin de partie */
                break;                /* On sort de la boucle while */
            }
            if (bytes_received < 0)
            {
                /* Erreur réseau inattendue */
                perror("[Reseau] Erreur de reception");
                mem->game_state = -1;
                break;
            }
            if (bytes_received > 0)
            {
                /* Données reçues : on traite le coup de l'adversaire */
                printf("[Reseau] J'ai recu la colonne %d de l'adversaire !\n", msg.column);

                /*
                 * On dépose le coup dans la mémoire partagée.
                 * puissance4.c lira last_played_column pour savoir
                 * quelle colonne appliquer sur le plateau.
                 */
                mem->last_played_column = msg.column;

                /*
                 * On lève le drapeau remote_move_received à 1 pour réveiller
                 * puissance4.c qui attend ce signal dans sa boucle d'attente.
                 */
                mem->remote_move_received = 1;

                /*
                 * Attente que puissance4.c ait appliqué le coup sur le plateau.
                 * Il remettra remote_move_received à 0 une fois fait.
                 * Cette synchronisation évite qu'on écrase last_played_column
                 * avec le prochain coup avant que l'actuel soit traité.
                 */
                while (mem->remote_move_received == 1)
                {
                    usleep(100000); /* Pause de 100 ms entre chaque vérification */
                }
            }
        }
    } /* fin du while(game_state == 0) */

    /* ============================================================
     * PARTIE 4 : NETTOYAGE FINAL
     * ============================================================
     * On ferme proprement toutes les ressources ouvertes.
     * Ne pas le faire laisserait des descripteurs de fichiers ouverts
     * et la mémoire partagée attachée (fuites de ressources).
     * ============================================================
     */

    close(client_socket);  /* Ferme la connexion TCP avec J2 */
    close(server_fd);      /* Ferme le socket d'écoute */
    shmdt(mem);            /* Détache la mémoire partagée (ne la supprime pas) */

    return 0; /* Programme terminé avec succès */
}
