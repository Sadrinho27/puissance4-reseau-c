# Puissance 4 en Réseau Local (C)

Un jeu de Puissance 4 développé en langage C permettant à deux joueurs de s'affronter sur des machines distinctes via le réseau.

Ce projet universitaire met en œuvre des concepts avancés de programmation système sous Linux :
- **Mémoire Partagée (IPC System V)** — communication entre le module jeu et le module réseau sur chaque machine.
- **Sockets TCP/IP** — échange des coups joués entre les deux machines via le réseau.
- **Signaux Unix (SIGINT)** — nettoyage automatique de la mémoire lors d'un arrêt avec Ctrl+C.
- **Synchronisation par drapeaux** — coordination entre processus parallèles sans mutex.

## Prérequis

- Système Linux (ou WSL sous Windows)
- Compilateur `gcc` installé
- Les deux machines sur le même réseau local

## Compilation

À la racine du projet, dans un terminal :

```bash
make
```

Cela génère trois exécutables : `puissance4`, `serveur_reseau`, et `client_reseau`.

Pour supprimer les exécutables :

```bash
make clean
```

## Comment jouer ?

Chaque machine fait tourner **deux processus en parallèle** : le plateau de jeu et le module réseau. Il faut donc ouvrir deux terminaux sur chaque machine.

**Important :** sur chaque machine, lancer le plateau (`./puissance4`) **avant** le module réseau.

---

### Machine du Joueur 1 (Serveur)

**Terminal 1 — le plateau de jeu :**
```bash
./puissance4 1
```

**Terminal 2 — le module réseau serveur :**
```bash
./serveur_reseau
```

Le serveur attendra la connexion du Joueur 2. Communiquez votre adresse IP locale au Joueur 2 (commande `ip a`, chercher une adresse de type `192.168.x.x`).

---

### Machine du Joueur 2 (Client)

**Terminal 1 — le plateau de jeu :**
```bash
./puissance4 2
```

**Terminal 2 — le module réseau client :**
```bash
./client_reseau <ADRESSE_IP_DU_JOUEUR_1>
```

Exemple :
```bash
./client_reseau 192.168.1.10
```

---

La synchronisation s'effectue automatiquement. La partie se termine quand un joueur aligne 4 jetons, en cas de match nul (plateau plein), ou si l'un des joueurs se déconnecte.

## Dépannage

**Problème : `Erreur : Lancez d'abord le jeu avec './puissance4 1'`**

Le module réseau ne trouve pas le segment de mémoire partagée. Solutions :
1. Vérifier que `./puissance4 1` est bien lancé dans un autre terminal **avant** `./serveur_reseau`.
2. Si le problème persiste, un segment IPC orphelin du lancement précédent bloque peut-être la création. Le nettoyer avec :

```bash
# Lister les segments IPC présents
ipcs -m

# Supprimer tous les segments orphelins (à utiliser avec précaution)
ipcrm -a
```

**Problème : `Erreur bind: Address already in use`**

Le port 8080 est encore occupé par un processus précédent. Attendre quelques secondes puis relancer `./serveur_reseau`.
