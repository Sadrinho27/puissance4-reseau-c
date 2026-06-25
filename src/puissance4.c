/*
 * ============================================================
 *  PUISSANCE 4 — MODULE JEU (puissance4.c)
 * ============================================================
 * Ce programme gère l'affichage et la logique du plateau de jeu
 * sur la machine locale. Il ne communique PAS directement avec
 * l'adversaire sur le réseau : il délègue cette tâche au module
 * réseau (serveur_reseau ou client_reseau) via la mémoire partagée.
 *
 * ARCHITECTURE GLOBALE DU PROJET :
 *
 *   [MACHINE J1]                          [MACHINE J2]
 *   puissance4.c  <--IPC-->  serveur  <=TCP=>  client  <--IPC-->  puissance4.c
 *   (jeu local)              reseau            reseau              (jeu local)
 *
 * Lancement : ./puissance4 <1 ou 2>
 *   Joueur 1 : se lance en PREMIER sur la machine serveur
 *   Joueur 2 : se lance en PREMIER sur la machine cliente
 * ============================================================
 */

/* ============================================================
 * INCLUSIONS DES BIBLIOTHÈQUES
 * ============================================================
 * Les bibliothèques sont des fichiers précompilés fournis avec
 * le compilateur. On les inclut pour accéder aux fonctions
 * et types qu'elles définissent.
 * ============================================================
 */
#include <stdio.h>   /* Entrées/sorties standard : printf(), scanf() */
#include <stdlib.h>  /* Fonctions générales : exit(), atoi() */
#include <sys/ipc.h> /* IPC (Inter-Process Communication) : ftok() */
#include <sys/shm.h> /* Mémoire partagée System V : shmget(), shmat(), shmctl(), shmdt() */
#include <unistd.h>  /* Appels système POSIX : usleep() */
#include <signal.h>  /* Gestion des signaux Unix : signal(), SIGINT */
#include <termios.h> /* Contrôle du terminal : tcflush() */
#include "common.h"  /* Nos structures partagées : SharedMemory, TcpMessage, ROWS, COLUMNS */

/* ============================================================
 * VARIABLE GLOBALE
 * ============================================================
 * Une variable globale est déclarée en dehors de toute fonction.
 * Sa portée est l'ensemble du fichier et sa durée de vie est
 * celle du programme entier.
 *
 * On a besoin de global_shm_id au niveau global car la fonction
 * cleanup_on_exit() doit y accéder, et une fonction ne peut pas
 * lire les variables locales d'une autre fonction (comme main()).
 * ============================================================
 */
int global_shm_id;

/* ============================================================
 * FONCTION : cleanup_on_exit(int sig)
 * ============================================================
 * Gestionnaire de signal : le système l'appelle automatiquement
 * quand l'utilisateur presse Ctrl+C (signal SIGINT).
 *
 * CONCEPT — Signaux Unix :
 *   Un signal est une notification asynchrone envoyée à un processus.
 *   SIGINT (Signal d'INTerruption, numéro 2) est le signal envoyé
 *   par le terminal quand on presse Ctrl+C.
 *   Par défaut, il tue le programme. On le "capture" ici pour
 *   d'abord nettoyer la mémoire avant de quitter.
 *
 * POURQUOI NETTOYER LA MÉMOIRE PARTAGÉE :
 *   Contrairement aux variables locales (empilées sur la stack),
 *   les segments IPC ne sont PAS libérés automatiquement à la fin
 *   du programme. Sans nettoyage explicite, ils persistent en RAM
 *   jusqu'au prochain redémarrage. La commande "ipcs -m" permet
 *   de lister les segments orphelins encore présents.
 *
 * PARAMÈTRE :
 *   sig (int) : numéro du signal reçu. La signature de cette fonction
 *               est imposée par signal() : elle DOIT prendre un int.
 *               On le marque (void) pour éviter l'avertissement
 *               "unused parameter" du compilateur.
 * ============================================================
 */
void cleanup_on_exit(int sig)
{
    (void)sig; /* On ignore le numéro du signal, on n'en a pas besoin */

    printf("\n[Signal] Fermeture. Nettoyage de la memoire...\n");

    /*
     * shmctl() : fonction de CONTRôLe de la mémoire partagée.
     *   - global_shm_id : identifiant du segment à contrôler
     *   - IPC_RMID      : commande "Remove ID" → marque le segment
     *                     pour suppression (il sera détruit dès que
     *                     tous les processus s'en seront détachés)
     *   - NULL          : pas de structure de paramètres supplémentaire
     */
    shmctl(global_shm_id, IPC_RMID, NULL);

    exit(0); /* Quitter proprement avec le code de retour 0 (succès) */
}

/* ============================================================
 * FONCTION : init_board(SharedMemory *mem)
 * ============================================================
 * Initialise tous les champs de la mémoire partagée à leurs
 * valeurs de départ pour une nouvelle partie.
 *
 * CONCEPT — Passage par pointeur :
 *   Le paramètre "SharedMemory *mem" est un POINTEUR : il contient
 *   l'ADRESSE mémoire de la structure, pas une copie de celle-ci.
 *   Grâce au pointeur, les modifications faites ici sont visibles
 *   par TOUS les processus qui partagent ce même segment mémoire.
 *   Si on passait la structure par valeur (sans *), on travaillerait
 *   sur une copie locale et rien ne serait modifié dans la mémoire partagée.
 *
 * PARAMÈTRE :
 *   mem (SharedMemory *) : pointeur vers le début du segment partagé
 * ============================================================
 */
void init_board(SharedMemory *mem)
{
    /*
     * Deux boucles imbriquées pour parcourir tout le tableau 2D.
     *   i : indice de ligne (0 = haut, ROWS-1 = bas)
     *   j : indice de colonne (0 = gauche, COLUMNS-1 = droite)
     */
    for (int i = 0; i < ROWS; i++)
    {
        for (int j = 0; j < COLUMNS; j++)
        {
            /*
             * Accès à un champ via un pointeur : l'opérateur -> est
             * un raccourci pour (*mem).board[i][j].
             * Il déréférence le pointeur (accède à la valeur pointée)
             * puis accède au champ board, puis à la case [i][j].
             */
            mem->board[i][j] = 0; /* 0 = case vide */
        }
    }

    mem->player_turn         = 1;  /* Le Joueur 1 commence toujours */
    mem->game_state          = 0;  /* 0 = partie en cours */
    mem->last_played_column  = -1; /* -1 = aucun coup joué pour l'instant */
    mem->local_move_to_send  = 0;  /* Drapeau : rien à envoyer au réseau */
    mem->remote_move_received = 0; /* Drapeau : rien reçu du réseau */
}

/* ============================================================
 * FONCTION : display_board(SharedMemory *mem)
 * ============================================================
 * Affiche le plateau de jeu dans le terminal en caractères ASCII.
 *
 * Représentation :
 *   .  = case vide
 *   X  = jeton du Joueur 1
 *   O  = jeton du Joueur 2
 *
 * Le plateau est affiché de haut (ligne 0) en bas (ligne 5),
 * ce qui correspond à la gravité : les jetons tombent vers le bas.
 * ============================================================
 */
void display_board(SharedMemory *mem)
{
    printf("\n");

    /* Parcours ligne par ligne, de i=0 (haut) à i=ROWS-1 (bas) */
    for (int i = 0; i < ROWS; i++)
    {
        printf("|"); /* Barre verticale de gauche */

        for (int j = 0; j < COLUMNS; j++)
        {
            /*
             * On teste la valeur de chaque case et on affiche
             * le symbole correspondant. Les espaces autour
             * ( . ) améliorent la lisibilité.
             */
            if (mem->board[i][j] == 0)
                printf(" . "); /* Vide */
            else if (mem->board[i][j] == 1)
                printf(" X "); /* Joueur 1 */
            else if (mem->board[i][j] == 2)
                printf(" O "); /* Joueur 2 */
        }

        printf("|\n"); /* Barre verticale de droite + saut de ligne */
    }

    /* Ligne de séparation et numéros de colonnes */
    printf("-------------------------\n");
    printf("  0  1  2  3  4  5  6  \n\n");
}

/* ============================================================
 * FONCTION : play_token(SharedMemory *mem, int column, int player)
 * ============================================================
 * Tente de placer un jeton dans la colonne demandée.
 *
 * SIMULATION DE LA GRAVITÉ :
 *   On parcourt la colonne de BAS en HAUT et on place le jeton
 *   dans la première case vide rencontrée. Cela simule la chute
 *   du jeton vers le bas, comme dans le vrai jeu.
 *
 * PARAMÈTRES :
 *   mem    (SharedMemory *) : pointeur vers la mémoire partagée
 *   column (int)            : numéro de colonne choisie (0 à COLUMNS-1)
 *   player (int)            : numéro du joueur qui pose le jeton (1 ou 2)
 *
 * VALEUR DE RETOUR :
 *   1 si le coup a été joué avec succès
 *   0 si le coup est invalide (colonne hors limites ou pleine)
 * ============================================================
 */
int play_token(SharedMemory *mem, int column, int player)
{
    /*
     * Validation de la colonne : doit être dans les limites du plateau.
     * Si elle est hors limites, on retourne 0 (coup invalide) immédiatement.
     */
    if (column < 0 || column >= COLUMNS)
        return 0;

    /*
     * Parcours de bas en haut dans la colonne choisie.
     * On commence à ROWS-1 (la ligne du bas) et on remonte jusqu'à 0.
     * La boucle s'arrête dès qu'on trouve une case vide (valeur 0).
     */
    for (int i = ROWS - 1; i >= 0; i--)
    {
        if (mem->board[i][column] == 0)
        {
            /* Case libre trouvée : on y place le jeton du joueur */
            mem->board[i][column] = player;

            /*
             * On mémorise la colonne jouée dans last_played_column.
             * Le module réseau lira cette valeur pour l'envoyer à l'adversaire.
             */
            mem->last_played_column = column;

            return 1; /* Coup réussi */
        }
    }

    return 0; /* Aucune case libre : la colonne est pleine → coup invalide */
}

/* ============================================================
 * FONCTION : check_victory(SharedMemory *mem, int player)
 * ============================================================
 * Vérifie si le joueur donné a aligné au moins 4 jetons consécutifs
 * dans l'une des 4 directions possibles.
 *
 * Les 4 directions testées :
 *   1. Horizontale            (gauche → droite)
 *   2. Verticale              (haut → bas)
 *   3. Diagonale descendante  (haut-gauche → bas-droite) ↘
 *   4. Diagonale montante     (bas-gauche → haut-droite) ↗
 *
 * PARAMÈTRES :
 *   mem    (SharedMemory *) : pointeur vers la mémoire partagée
 *   player (int)            : joueur à vérifier (1 ou 2)
 *
 * VALEUR DE RETOUR :
 *   1 si le joueur a gagné
 *   0 sinon
 * ============================================================
 */
int check_victory(SharedMemory *mem, int player)
{
    /* --- 1. VÉRIFICATION HORIZONTALE --- */
    /*
     * Pour chaque ligne i, on teste les groupes de 4 cases horizontales.
     * j va de 0 à COLUMNS-4 (= 3) : si j dépassait 3, j+3 serait hors tableau.
     */
    for (int i = 0; i < ROWS; i++)
        for (int j = 0; j < COLUMNS - 3; j++)
            if (mem->board[i][j]     == player &&
                mem->board[i][j + 1] == player &&
                mem->board[i][j + 2] == player &&
                mem->board[i][j + 3] == player)
                return 1;

    /* --- 2. VÉRIFICATION VERTICALE --- */
    /*
     * Pour chaque colonne j, on teste les groupes de 4 cases verticales.
     * i va de 0 à ROWS-4 (= 2) : si i dépassait 2, i+3 serait hors tableau.
     */
    for (int i = 0; i < ROWS - 3; i++)
        for (int j = 0; j < COLUMNS; j++)
            if (mem->board[i][j]     == player &&
                mem->board[i + 1][j] == player &&
                mem->board[i + 2][j] == player &&
                mem->board[i + 3][j] == player)
                return 1;

    /* --- 3. VÉRIFICATION DIAGONALE DESCENDANTE (↘) --- */
    /*
     * Direction : haut-gauche vers bas-droite.
     * i et j sont tous les deux bornés à -3 de leurs maximums
     * pour éviter de sortir du tableau.
     */
    for (int i = 0; i < ROWS - 3; i++)
        for (int j = 0; j < COLUMNS - 3; j++)
            if (mem->board[i][j]         == player &&
                mem->board[i + 1][j + 1] == player &&
                mem->board[i + 2][j + 2] == player &&
                mem->board[i + 3][j + 3] == player)
                return 1;

    /* --- 4. VÉRIFICATION DIAGONALE MONTANTE (↗) --- */
    /*
     * Direction : bas-gauche vers haut-droite.
     * i commence à 3 (et non 0) car on remonte (i-1, i-2, i-3)
     * et on ne peut pas remonter en dessous de la ligne 0.
     */
    for (int i = 3; i < ROWS; i++)
        for (int j = 0; j < COLUMNS - 3; j++)
            if (mem->board[i][j]         == player &&
                mem->board[i - 1][j + 1] == player &&
                mem->board[i - 2][j + 2] == player &&
                mem->board[i - 3][j + 3] == player)
                return 1;

    return 0; /* Aucun alignement de 4 trouvé pour ce joueur */
}

/* ============================================================
 * FONCTION : is_board_full(SharedMemory *mem)
 * ============================================================
 * Vérifie si le plateau est entièrement rempli (aucune case vide).
 * Utilisée pour détecter le match nul quand personne n'a gagné.
 *
 * ASTUCE D'OPTIMISATION :
 *   Les jetons s'accumulent de bas en haut. Si la PREMIÈRE LIGNE
 *   (i=0, tout en haut) est pleine, alors tout le plateau l'est.
 *   Inutile de parcourir toutes les lignes : on ne teste que la ligne 0.
 *
 * PARAMÈTRE :
 *   mem (SharedMemory *) : pointeur vers la mémoire partagée
 *
 * VALEUR DE RETOUR :
 *   1 si le plateau est plein
 *   0 s'il reste au moins une case libre
 * ============================================================
 */
int is_board_full(SharedMemory *mem)
{
    for (int j = 0; j < COLUMNS; j++)
        if (mem->board[0][j] == 0) /* Case libre trouvée dans la ligne du haut */
            return 0;              /* Le plateau n'est pas encore plein */

    return 1; /* Toutes les cases du haut sont occupées → plateau plein */
}

/* ============================================================
 * FONCTION PRINCIPALE : main(int argc, char *argv[])
 * ============================================================
 * Point d'entrée du programme. C'est la première fonction
 * appelée par le système d'exploitation au lancement.
 *
 * PARAMÈTRES (arguments de la ligne de commande) :
 *   argc (int)    : nombre total d'arguments, y compris le nom
 *                   du programme lui-même. Vaut toujours au moins 1.
 *                   Exemple : "./puissance4 1" → argc = 2
 *
 *   argv (char**) : tableau de pointeurs vers des chaînes de caractères.
 *                   argv[0] = chemin du programme ("./puissance4")
 *                   argv[1] = premier argument ("1" ou "2")
 *                   Note : les arguments sont des CHAÎNES, pas des entiers.
 *                   Il faut les convertir avec atoi().
 *
 * VALEUR DE RETOUR :
 *   0 si le programme se termine normalement
 *   1 si une erreur est survenue
 * ============================================================
 */
int main(int argc, char *argv[])
{
    /* ============================================================
     * ÉTAPE 1 : VALIDATION DES ARGUMENTS
     * ============================================================
     * On vérifie que l'utilisateur a bien fourni le numéro du joueur.
     * argc == 2 signifie exactement 2 arguments : "./puissance4" et "1" ou "2".
     * ============================================================
     */
    if (argc != 2)
    {
        printf("Utilisation : ./puissance4 <1 ou 2>\n");
        return 1; /* Code de retour 1 = erreur, convention Unix */
    }

    /*
     * atoi() : "ASCII to Integer" → convertit la chaîne argv[1]
     * (par exemple "2") en entier (2).
     * my_id vaudra 1 pour le Joueur 1, 2 pour le Joueur 2.
     */
    int my_id = atoi(argv[1]);

    /* ============================================================
     * ÉTAPE 2 : ENREGISTREMENT DU GESTIONNAIRE DE SIGNAL
     * ============================================================
     * signal() associe une fonction à un signal Unix.
     * Ici : quand SIGINT (Ctrl+C) est reçu, appeler cleanup_on_exit().
     * Sans cela, Ctrl+C tuerait le programme sans libérer l'IPC.
     * ============================================================
     */
    signal(SIGINT, cleanup_on_exit);

    /* ============================================================
     * ÉTAPE 3 : CRÉATION ET ATTACHEMENT DE LA MÉMOIRE PARTAGÉE
     * ============================================================
     * CONCEPT — IPC System V (Mémoire Partagée) :
     *   Deux processus peuvent partager une zone de RAM en utilisant
     *   une clé commune. C'est l'équivalent d'un fichier en mémoire :
     *   plusieurs processus peuvent "ouvrir" le même segment et y lire
     *   ou écrire simultanément, sans passer par le réseau.
     *
     * Étapes :
     *   1. ftok()   → générer une clé unique identifiant ce segment
     *   2. shmget() → créer (ou trouver) le segment en mémoire
     *   3. shmat()  → l'attacher à l'espace d'adressage du processus
     * ============================================================
     */

    /*
     * ftok() : génère une clé IPC de type key_t (entier 32 bits).
     *
     *   Arguments :
     *   - "/tmp" : chemin d'un fichier existant sur le système.
     *              ftok() utilise l'identifiant d'inode de ce fichier
     *              (un numéro unique attribué par le système de fichiers).
     *   - 65     : identifiant de projet (octet arbitraire, 'A' en ASCII).
     *
     *   RÈGLE IMPORTANTE : tous les processus qui veulent partager
     *   le même segment DOIVENT appeler ftok() avec les mêmes arguments.
     *   C'est comme un mot de passe commun.
     *
     *   On utilise "/tmp" car ce dossier existe sur tous les systèmes Unix
     *   et son inode ne change jamais, garantissant une clé stable.
     */
    key_t key = ftok("/tmp", 65);

    /*
     * shmget() : crée ou accède à un segment de mémoire partagée.
     *
     *   Arguments :
     *   - key                  : la clé générée par ftok()
     *   - sizeof(SharedMemory) : taille en octets du segment à réserver.
     *                            sizeof() calcule la taille à la compilation.
     *   - 0666 | IPC_CREAT    : flags combinés avec l'opérateur bitwise OR (|)
     *       0666     : permissions UNIX (lire+écrire pour tous, comme chmod)
     *       IPC_CREAT : créer le segment s'il n'existe pas encore
     *
     *   Retourne un entier shm_id (SHared Memory IDentifier), analogue
     *   au descripteur de fichier retourné par open().
     */
    int shm_id = shmget(key, sizeof(SharedMemory), 0666 | IPC_CREAT);

    /*
     * On sauvegarde shm_id dans la variable globale afin que
     * cleanup_on_exit() puisse y accéder en cas de Ctrl+C.
     */
    global_shm_id = shm_id;

    /*
     * shmat() : "SHared Memory ATtach" → attache le segment à l'espace
     * d'adressage virtuel du processus courant.
     *
     *   Arguments :
     *   - shm_id : l'identifiant du segment à attacher
     *   - NULL   : l'OS choisit lui-même l'adresse virtuelle d'attachement
     *   - 0      : mode d'accès par défaut (lecture + écriture)
     *
     *   Retourne un pointeur void* vers le début du segment.
     *   On le caste en (SharedMemory *) pour pouvoir utiliser
     *   la notation mem->champ et accéder aux champs de la structure.
     *
     *   EFFET : mem pointe vers une zone RAM physiquement partagée
     *   avec le module réseau. Toute écriture dans *mem est
     *   immédiatement visible par l'autre processus.
     */
    SharedMemory *mem = (SharedMemory *)shmat(shm_id, NULL, 0);

    /* ============================================================
     * ÉTAPE 4 : INITIALISATION DU PLATEAU
     * ============================================================
     * On remet toutes les cases à zéro et tous les drapeaux à leur
     * valeur initiale. Chaque machine initialise sa propre mémoire
     * partagée locale au démarrage.
     * ============================================================
     */
    init_board(mem);

    printf("--- DEBUT DU JEU : VOUS ETES LE JOUEUR %d ---\n", my_id);
    display_board(mem);

    /* ============================================================
     * ÉTAPE 5 : BOUCLE PRINCIPALE DE JEU
     * ============================================================
     * La boucle while tourne en continu tant que game_state vaut 0.
     * Elle se termine automatiquement dès qu'un joueur gagne
     * (game_state = 1 ou 2) ou en cas de match nul (game_state = -1).
     *
     * À chaque itération, deux cas sont possibles :
     *   CAS 1 : c'est mon tour → je lis le clavier et joue un coup
     *   CAS 2 : c'est le tour adverse → j'attends le coup du réseau
     * ============================================================
     */
    while (mem->game_state == 0)
    {
        /* *** CAS 1 : C'EST MON TOUR *** */
        if (mem->player_turn == my_id)
        {
            int chosen_column;
            printf("A vous de jouer ! Colonne (0-6) : ");

            /*
             * scanf() : lit un entier depuis l'entrée standard (le clavier).
             *   "%d"           : format → entier décimal
             *   &chosen_column : adresse mémoire de chosen_column.
             *                    L'opérateur & retourne l'adresse d'une variable.
             *                    scanf() a besoin de l'adresse pour ÉCRIRE
             *                    la valeur lue directement dans la variable.
             *
             * IMPORTANT : scanf() est BLOQUANT → le programme attend ici
             * que l'utilisateur tape une valeur et appuie sur Entrée.
             */
            /*
             * tcflush() vide le buffer d'entrée du terminal avant de lire.
             * Sans ça, les chiffres tapés pendant le tour adverse restent
             * dans le buffer et sont lus automatiquement par scanf()
             * au tour suivant, jouant des coups à l'insu du joueur.
             *   STDIN_FILENO : descripteur de l'entrée standard (= 0)
             *   TCIFLUSH     : vide les données reçues non encore lues
             */
            tcflush(STDIN_FILENO, TCIFLUSH);
            scanf("%d", &chosen_column);

            /*
             * Tentative de placement du jeton.
             * La fonction retourne 1 si le coup est valide, 0 sinon.
             * Le "if" interprète 1 comme vrai et 0 comme faux.
             */
            if (play_token(mem, chosen_column, my_id))
            {
                /* Coup valide : on affiche le plateau mis à jour */
                display_board(mem);

                /*
                 * MÉCANISME DE SYNCHRONISATION PAR DRAPEAU :
                 *
                 * On lève local_move_to_send à 1 pour signaler au module
                 * réseau (serveur_reseau ou client_reseau) qu'un coup
                 * vient d'être joué et doit être transmis à l'adversaire.
                 *
                 * Le module réseau tourne EN PARALLÈLE dans un processus
                 * séparé et surveille ce drapeau dans sa propre boucle.
                 * Il lira la colonne dans last_played_column, enverra le
                 * coup via TCP, puis remettra le drapeau à 0.
                 */
                mem->local_move_to_send = 1;

                /*
                 * ATTENTE ACTIVE (spin-wait) :
                 *
                 * On bloque ici jusqu'à ce que le module réseau ait
                 * confirmé l'envoi du coup (drapeau remis à 0).
                 * usleep(10000) fait dormir le processus 10 millisecondes
                 * entre chaque vérification pour ne pas monopoliser le CPU.
                 *
                 * Sans le usleep, cette boucle consommerait 100% du CPU.
                 */
                while (mem->local_move_to_send == 1)
                {
                    usleep(10000); /* Pause de 10 ms = 10 000 microsecondes */
                }

                /* --- Vérification de fin de partie --- */

                if (check_victory(mem, my_id))
                {
                    /*
                     * J'ai aligné 4 jetons → je gagne !
                     * On met game_state à mon numéro pour signaler la victoire.
                     * La boucle while se terminera à la prochaine vérification.
                     */
                    mem->game_state = my_id;
                    printf("\n*** VOUS AVEZ GAGNE ! ***\n");
                }
                else if (is_board_full(mem))
                {
                    /* Plateau plein, personne n'a gagné → match nul */
                    mem->game_state = -1;
                    printf("\n*** MATCH NUL ! ***\n");
                }
                else
                {
                    /*
                     * Partie continue : on passe le tour à l'adversaire.
                     * Opérateur ternaire : (condition) ? valeur_si_vrai : valeur_si_faux
                     * Si je suis J1 → tour de J2, sinon → tour de J1.
                     */
                    mem->player_turn = (my_id == 1) ? 2 : 1;
                }
            }
            else
            {
                /* Coup invalide : on redemande sans changer le tour */
                printf("Coup invalide, reessayez.\n");
            }
        }
        /* *** CAS 2 : C'EST LE TOUR DE L'ADVERSAIRE *** */
        else
        {
            /*
             * On vérifie si le module réseau a déposé un coup adverse
             * dans la mémoire partagée via le drapeau remote_move_received.
             */
            if (mem->remote_move_received == 1)
            {
                /*
                 * Le module réseau a reçu un coup via TCP et l'a écrit
                 * dans last_played_column. On l'affiche et on l'applique.
                 */
                printf("\nL'adversaire a joue la colonne %d.\n", mem->last_played_column);

                /*
                 * On applique le coup adverse sur notre plateau local.
                 * mem->player_turn contient ENCORE le numéro de l'adversaire
                 * (le tour n'a pas encore changé), donc c'est bien lui
                 * qu'on passe comme identifiant de joueur.
                 */
                play_token(mem, mem->last_played_column, mem->player_turn);
                display_board(mem);

                /*
                 * Acquittement : on remet le drapeau à 0 pour signaler
                 * au module réseau que le coup a bien été traité et
                 * qu'il peut continuer à surveiller le réseau.
                 */
                mem->remote_move_received = 0;

                /* --- Vérification de fin de partie après le coup adverse --- */

                if (check_victory(mem, mem->player_turn))
                {
                    /* L'adversaire a aligné 4 jetons → il gagne */
                    mem->game_state = mem->player_turn;
                    printf("\n*** L'ADVERSAIRE A GAGNE... ***\n");
                }
                else if (is_board_full(mem))
                {
                    /* Plateau plein après le coup adverse → match nul */
                    mem->game_state = -1;
                    printf("\n*** MATCH NUL ! ***\n");
                }
                else
                {
                    /* Partie continue : c'est de nouveau mon tour */
                    mem->player_turn = my_id;
                }
            }
            else
            {
                /*
                 * Le coup adverse n'est pas encore arrivé.
                 * On attend 100 ms avant de re-vérifier le drapeau
                 * pour ne pas surcharger le CPU avec une boucle vide.
                 * (100 000 microsecondes = 100 millisecondes = 0.1 seconde)
                 */
                usleep(100000);
            }
        }
    } /* fin du while(game_state == 0) */

    /* ============================================================
     * ÉTAPE 6 : NETTOYAGE ET FIN DU PROGRAMME
     * ============================================================
     */

    /*
     * shmdt() : "SHared Memory DeTach" → détache le segment de l'espace
     * d'adressage du processus. Après cet appel, le pointeur mem
     * devient invalide et ne doit plus être utilisé.
     * Le segment lui-même n'est pas encore détruit.
     */
    shmdt(mem);

    /*
     * Seul le Joueur 1 supprime définitivement le segment IPC.
     * Convention : J1 est le "propriétaire" de la mémoire partagée
     * puisqu'il l'a créée (avec IPC_CREAT). J2 se contente de s'y attacher.
     * Si J2 essayait aussi de supprimer le segment, cela provoquerait une erreur.
     */
    if (my_id == 1)
        shmctl(shm_id, IPC_RMID, NULL); /* Suppression définitive du segment */

    return 0; /* Programme terminé avec succès */
}
