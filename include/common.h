#ifndef COMMON_H
#define COMMON_H

/*
 * ============================================================
 *  FICHIER D'EN-TÊTES PARTAGÉ : common.h
 * ============================================================
 * Ce fichier est inclus par les TROIS modules du projet :
 *   - puissance4.c     (le jeu local)
 *   - serveur_reseau.c (le relais réseau côté J1)
 *   - client_reseau.c  (le relais réseau côté J2)
 *
 * Il définit les structures de données communes, ce qui garantit
 * que tous les modules "parlent le même langage" : ils accèdent
 * aux mêmes champs avec les mêmes noms et les mêmes tailles.
 *
 * Les gardes #ifndef / #define / #endif empêchent une double
 * inclusion si le fichier était inclus plusieurs fois dans le
 * même fichier source (protection contre les redéfinitions).
 * ============================================================
 */

/* --- CONSTANTES DU PLATEAU --- */

/* Nombre de lignes du plateau de Puissance 4 (standard : 6) */
#define ROWS    6

/* Nombre de colonnes du plateau de Puissance 4 (standard : 7) */
#define COLUMNS 7

/*
 * ============================================================
 *  STRUCTURE : SharedMemory
 * ============================================================
 * Cette structure représente l'intégralité de la mémoire partagée
 * échangée entre le module jeu et le module réseau via IPC.
 *
 * CONCEPT CLÉ — Mémoire Partagée (IPC System V) :
 *   Sur chaque machine, deux processus (le jeu et le réseau) tournent
 *   simultanément. Pour communiquer, ils partagent une même zone de
 *   RAM identifiée par une clé unique. Toute écriture par l'un est
 *   instantanément lisible par l'autre, sans copie ni message.
 *
 * ATTENTION : il n'y a PAS de mémoire partagée entre les deux machines.
 *   La synchronisation des coups entre J1 et J2 passe EXCLUSIVEMENT
 *   par le réseau TCP (voir TcpMessage ci-dessous).
 * ============================================================
 */
typedef struct {

    /*
     * board[ROWS][COLUMNS] : le plateau de jeu sous forme de tableau 2D.
     *   - 0 = case vide
     *   - 1 = jeton du Joueur 1 (X)
     *   - 2 = jeton du Joueur 2 (O)
     *
     * En mémoire, un tableau 2D est stocké "à plat" ligne par ligne.
     * board[i][j] correspond à la ligne i, colonne j.
     */
    int board[ROWS][COLUMNS];

    /*
     * player_turn : numéro du joueur dont c'est le tour (1 ou 2).
     * Mis à jour après chaque coup valide.
     */
    int player_turn;

    /*
     * game_state : état global de la partie.
     *   0  = partie en cours
     *   1  = le Joueur 1 a gagné
     *   2  = le Joueur 2 a gagné
     *  -1  = match nul (plateau plein sans vainqueur) ou déconnexion
     */
    int game_state;

    /*
     * last_played_column : numéro de la colonne du dernier coup joué.
     * Sert de "boîte aux lettres" entre le jeu et le module réseau :
     *   - Le jeu y écrit la colonne qu'il vient de jouer
     *   - Le réseau lit cette valeur pour la transmettre via TCP
     *   - Et inversement : le réseau y écrit le coup reçu de l'adversaire
     *     pour que le jeu puisse l'appliquer sur le plateau.
     */
    int last_played_column;

    /*
     * local_move_to_send : drapeau de synchronisation (0 ou 1).
     *   Mécanisme :
     *   1. Le jeu joue un coup et lève ce drapeau à 1.
     *   2. Le réseau détecte le changement, envoie le coup via TCP.
     *   3. Le réseau remet le drapeau à 0 pour signaler "coup envoyé".
     *   4. Le jeu reprend son exécution.
     *
     * C'est une forme simplifiée de sémaphore binaire.
     */
    int local_move_to_send;

    /*
     * remote_move_received : drapeau de synchronisation (0 ou 1).
     *   Mécanisme inverse de local_move_to_send :
     *   1. Le réseau reçoit un coup de l'adversaire via TCP.
     *   2. Il écrit la colonne dans last_played_column et lève ce drapeau à 1.
     *   3. Le jeu détecte le changement et applique le coup sur le plateau.
     *   4. Le jeu remet le drapeau à 0 pour signaler "coup appliqué".
     */
    int remote_move_received;

} SharedMemory;

/*
 * ============================================================
 *  STRUCTURE : TcpMessage
 * ============================================================
 * Paquet réseau envoyé entre les deux machines via TCP.
 *
 * CONCEPT CLÉ — Sérialisation :
 *   Pour envoyer une structure C sur le réseau, on envoie ses octets
 *   bruts avec send(socket, &msg, sizeof(msg), 0). Le récepteur
 *   reconstitue la structure avec recv(). Cette technique fonctionne
 *   ici car les deux machines ont la même architecture (même endianness,
 *   même taille d'int). Dans un projet multi-plateforme, il faudrait
 *   normaliser avec htonl()/ntohl().
 *
 * On n'envoie QUE le strict nécessaire sur le réseau (2 entiers = 8 octets)
 * pour minimiser la bande passante et simplifier le protocole.
 * ============================================================
 */
typedef struct {

    /* La colonne jouée par l'adversaire (valeur entre 0 et COLUMNS-1) */
    int column;

    /* État de la partie au moment de l'envoi (0, 1, 2 ou -1) */
    int win_status;

} TcpMessage;

#endif /* COMMON_H */
