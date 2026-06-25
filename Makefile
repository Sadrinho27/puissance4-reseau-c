# Compilateur et options de compilation
CC = gcc
# -Wall et -Wextra affichent tous les avertissements, -Iinclude indique où trouver les .h
CFLAGS = -Wall -Wextra -Iinclude

# Noms des exécutables finaux
EXEC_JEU = puissance4
EXEC_SRV = serveur_reseau
EXEC_CLI = client_reseau

# Règle par défaut : compile tout
all: $(EXEC_JEU) $(EXEC_SRV) $(EXEC_CLI)

# Compilation du jeu
$(EXEC_JEU): src/puissance4.c include/common.h
	$(CC) $(CFLAGS) src/puissance4.c -o $(EXEC_JEU)

# Compilation du serveur TCP
$(EXEC_SRV): src/serveur_reseau.c include/common.h
	$(CC) $(CFLAGS) src/serveur_reseau.c -o $(EXEC_SRV)

# Compilation du client TCP
$(EXEC_CLI): src/client_reseau.c include/common.h
	$(CC) $(CFLAGS) src/client_reseau.c -o $(EXEC_CLI)

# Règle de nettoyage (supprime les exécutables)
clean:
	rm -f $(EXEC_JEU) $(EXEC_SRV) $(EXEC_CLI)