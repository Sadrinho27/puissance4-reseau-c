/*
 * ============================================================
 *  PUISSANCE 4 — MODULE RÉSEAU CLIENT (client_reseau.c)
 * ============================================================
 * Ce programme est le PONT de communication côté Joueur 2.
 * Il est le miroir de serveur_reseau.c : il remplit les mêmes
 * deux rôles mais du côté client de la connexion TCP.
 *
 * Différence principale avec serveur_reseau.c :
 *   - Le serveur crée le socket et ATTEND la connexion (passif)
 *   - Le client crée le socket et VA SE CONNECTER au serveur (actif)
 *
 * POSITION DANS L'ARCHITECTURE :
 *
 *   [MACHINE J2]
 *   puissance4.c  <====IPC====>  client_reseau.c  <====TCP====> serveur_reseau.c
 *   (jeu local)   mém. partagée  (ce fichier)       réseau       (machine J1)
 *
 * Lancement : ./client_reseau <IP_DU_JOUEUR_1>
 *   (après avoir lancé ./puissance4 2 dans un autre terminal)
 *   Exemple : ./client_reseau 192.168.1.10
 * ============================================================
 */

#include <stdio.h>     /* printf(), perror() */
#include <stdlib.h>    /* exit() */
#include <string.h>    /* Manipulation de chaînes (inclus par bonne pratique) */
#include <unistd.h>    /* close(), usleep() */
#include <arpa/inet.h> /* API réseau : socket(), connect(), send(), recv(), inet_addr() */
#include <sys/ipc.h>   /* IPC System V : ftok() */
#include <sys/shm.h>   /* Mémoire partagée : shmget(), shmat(), shmdt() */
#include "common.h"    /* Structures partagées : SharedMemory, TcpMessage */

int main(int argc, char *argv[])
{
    /* ============================================================
     * PARTIE 1 : VALIDATION DES ARGUMENTS
     * ============================================================
     * On vérifie en PREMIER que l'utilisateur a fourni l'adresse IP.
     * On le fait avant toute autre opération pour éviter de créer
     * des ressources (socket, IPC) qu'on devrait ensuite libérer
     * immédiatement en cas d'erreur d'arguments.
     * ============================================================
     */

    /*
     * argc doit valoir 2 : "./client_reseau" ET l'adresse IP.
     * Exemple valide : ./client_reseau 192.168.1.10
     */
    if (argc != 2)
    {
        printf("Utilisation : ./client_reseau <IP_DU_SERVEUR>\n");
        return 1;
    }

    /* ============================================================
     * PARTIE 2 : ATTACHEMENT À LA MÉMOIRE PARTAGÉE (IPC)
     * ============================================================
     * Même logique que dans serveur_reseau.c mais pour le Joueur 2.
     * On accède au segment créé par ./puissance4 2.
     * ============================================================
     */

    /*
     * ftok() : génère la clé IPC avec les MÊMES arguments que dans
     * puissance4.c et serveur_reseau.c. C'est indispensable pour
     * que tous les processus pointent vers le même segment de RAM.
     */
    key_t key = ftok("/tmp", 65);

    /*
     * shmget() sans IPC_CREAT : on accède à un segment existant.
     * Si ./puissance4 2 n'a pas été lancé avant ce programme,
     * le segment n'existe pas et shmget() retourne -1.
     */
    int shm_id = shmget(key, sizeof(SharedMemory), 0666);
    if (shm_id == -1)
    {
        printf("Erreur : Lancez d'abord le jeu avec './puissance4 2'\n");
        return 1;
    }

    /*
     * shmat() : attache le segment à notre espace d'adressage.
     * mem pointe vers la même RAM que le mem de puissance4.c (côté J2).
     */
    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);

    /* ============================================================
     * PARTIE 3 : CONNEXION AU SERVEUR TCP (JOUEUR 1)
     * ============================================================
     * CONCEPT — Sockets TCP (côté client) :
     *   Le client n'a besoin que de deux étapes (vs quatre pour le serveur) :
     *   1. socket()  → créer le socket
     *   2. connect() → se connecter au serveur (bloquant jusqu'à l'acceptation)
     *
     * Pas de bind() : le système choisit automatiquement un port source libre.
     * Pas de listen() ni accept() : c'est le serveur qui attend, pas le client.
     * ============================================================
     */

    /*
     * socket() : crée un socket TCP identique à celui du serveur.
     * AF_INET + SOCK_STREAM = IPv4 + TCP, comme côté serveur.
     */
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);

    /*
     * struct sockaddr_in : structure décrivant l'adresse du SERVEUR (J1)
     * auquel on veut se connecter. On remplit l'IP et le port de destination.
     */
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(8080); /* Même port que le serveur : 8080 */

    /*
     * inet_addr() : convertit une chaîne IP en notation décimale pointée
     * ("192.168.1.10") en entier 32 bits au format réseau (big-endian).
     *
     * Exemples de chaînes valides : "192.168.1.10", "127.0.0.1", "10.0.0.5"
     *
     * Si le format est invalide, inet_addr() retourne INADDR_NONE
     * (= 0xFFFFFFFF = -1 en non-signé). On vérifie ce cas pour
     * afficher un message d'erreur clair avant de tenter une connexion.
     */
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    if (server_addr.sin_addr.s_addr == INADDR_NONE)
    {
        printf("Erreur : adresse IP invalide '%s'\n", argv[1]);
        close(client_socket);
        shmdt(mem);
        return 1;
    }

    printf("--- APPLICATION RESEAU (J2) ---\n");
    printf("Tentative de connexion au Joueur 1 (%s:8080)...\n", argv[1]);

    /*
     * connect() : établit la connexion TCP avec le serveur.
     *
     * COMPORTEMENT BLOQUANT : connect() attend que le serveur appelle
     * accept(). Si le serveur n'est pas lancé ou si l'IP est incorrecte,
     * connect() échoue après un délai (timeout réseau) et retourne -1.
     *
     * Le cast (struct sockaddr *) est nécessaire pour la même raison
     * que dans bind() : l'API générique accepte plusieurs types d'adresses.
     */
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erreur de connexion au serveur");
        close(client_socket);
        shmdt(mem);
        return 1;
    }

    printf("Connecte au Joueur 1 ! La synchronisation commence.\n\n");

    /* ============================================================
     * PARTIE 4 : BOUCLE DE SYNCHRONISATION
     * ============================================================
     * Logique IDENTIQUE à serveur_reseau.c mais pour le Joueur 2.
     *
     * Les rôles sont simplement inversés :
     *   - Quand c'est le tour de J2 → envoyer le coup local à J1
     *   - Quand c'est le tour de J1 → recevoir son coup depuis J1
     *
     * Le mécanisme IPC (drapeaux, last_played_column) fonctionne
     * de manière symétrique sur les deux machines.
     * ============================================================
     */

    TcpMessage msg; /* Structure du paquet réseau envoyé/reçu sur TCP */
    int my_id = 2;  /* Ce module réseau appartient au Joueur 2 */

    while (mem->game_state == 0)
    {
        /* === MON TOUR (J2) : envoyer mon coup local via TCP à J1 === */
        if (mem->player_turn == my_id)
        {
            /*
             * On surveille le drapeau local_move_to_send.
             * puissance4.c (J2) le lève à 1 quand il joue un coup.
             */
            if (mem->local_move_to_send == 1)
            {
                /* On lit la colonne jouée et on prépare le paquet TCP */
                msg.column     = mem->last_played_column;
                msg.win_status = mem->game_state;

                /*
                 * send() : envoie le paquet vers J1 via le socket TCP.
                 * Même fonction que côté serveur, même fonctionnement.
                 * TCP garantit la livraison complète et dans l'ordre.
                 */
                send(client_socket, &msg, sizeof(msg), 0);
                printf("[Reseau] J'ai envoye la colonne %d a l'adversaire.\n", msg.column);

                /* Acquittement : coup envoyé → on baisse le drapeau */
                mem->local_move_to_send = 0;
            }
            else
            {
                /* puissance4.c J2 n'a pas encore joué → on attend */
                usleep(100000); /* Pause de 100 ms */
            }
        }
        /* === TOUR ADVERSE (J1) : recevoir son coup via TCP et le déposer en IPC === */
        else
        {
            /*
             * recv() : attend de recevoir un message de J1 via TCP.
             *
             * COMPORTEMENT BLOQUANT : le programme est suspendu ici
             * jusqu'à l'arrivée de données. L'OS met le processus en
             * veille sans consommer de CPU (contrairement à un spin-wait).
             *
             * Valeurs de retour :
             *   > 0 : données reçues → on les traite
             *   = 0 : J1 a fermé sa connexion (fin normale ou crash)
             *   < 0 : erreur réseau (câble coupé, timeout, etc.)
             */
            int bytes_received = recv(client_socket, &msg, sizeof(msg), 0);

            if (bytes_received == 0)
            {
                /* J1 s'est déconnecté → fin de partie forcée */
                printf("[Reseau] L'adversaire s'est deconnecte.\n");
                mem->game_state = -1;
                break; /* Sortie de la boucle while */
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
                /* Coup de J1 reçu : on le dépose dans la mémoire partagée */
                printf("[Reseau] J'ai recu la colonne %d de l'adversaire !\n", msg.column);

                /*
                 * Dépôt du coup dans la "boîte aux lettres" IPC.
                 * puissance4.c (J2) lira cette valeur pour appliquer
                 * le coup adverse sur le plateau local.
                 */
                mem->last_played_column  = msg.column;

                /*
                 * On lève le drapeau pour réveiller puissance4.c (J2).
                 * Il surveille ce drapeau dans sa boucle d'attente
                 * et appliquera le coup dès qu'il le verra à 1.
                 */
                mem->remote_move_received = 1;

                /*
                 * Attente que puissance4.c ait fini d'appliquer le coup.
                 * Il remet remote_move_received à 0 une fois le coup traité.
                 * Sans cette attente, on risquerait d'écraser last_played_column
                 * avec un prochain message avant que le précédent soit utilisé.
                 */
                while (mem->remote_move_received == 1)
                {
                    usleep(100000); /* Pause de 100 ms entre chaque vérification */
                }
            }
        }
    } /* fin du while(game_state == 0) */

    /* ============================================================
     * PARTIE 5 : NETTOYAGE FINAL
     * ============================================================
     * On libère proprement toutes les ressources allouées.
     * Note : contrairement à J1, J2 ne supprime PAS le segment IPC
     * (shmctl IPC_RMID) car J1 en est propriétaire et s'en charge.
     * ============================================================
     */

    close(client_socket); /* Ferme la connexion TCP vers J1 */
    shmdt(mem);           /* Détache la mémoire partagée locale */

    return 0; /* Programme terminé avec succès */
}
