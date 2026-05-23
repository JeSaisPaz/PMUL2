import requests
import random
import time

# Configuration
BASE_URL = "http://localhost:3000/api" # Ajuste le port si nécessaire
NB_COMMANDES = 5 # Nombre de commandes à créer

def get_colors():
    """Récupère les couleurs disponibles en base"""
    try:
        response = requests.get(f"{BASE_URL}/colors")
        response.raise_for_status()
        return response.json()
    except Exception as e:
        print(f"Erreur lors de la récupération des couleurs: {e}")
        return []

def create_random_order(available_colors):
    """Crée une commande avec 1 à 3 lignes de couleurs aléatoires"""
    if not available_colors:
        print("Aucune couleur disponible pour créer une commande.")
        return

    # On choisit entre 1 et 3 types de couleurs différentes pour la commande
    selected_colors = random.sample(available_colors, k=random.randint(1, min(3, len(available_colors))))
    
    lines = []
    for color in selected_colors:
        lines.append({
            "id": color['id'],
            "quantity": random.randint(1, 10) # Quantité entre 1 et 10
        })

    payload = {"lines": lines}

    try:
        # Ton endpoint attend un POST sur /neworder
        response = requests.post(f"{BASE_URL}/neworder", json=payload)
        if response.status_code == 204:
            print(f"Commande créée avec succès ({len(lines)} lignes)")
        else:
            print(f"Erreur {response.status_code}: {response.text}")
    except Exception as e:
        print(f"Erreur lors de l'envoi de la commande: {e}")

if __name__ == "__main__":
    print("Démarrage du script de création de commandes...")
    
    colors = get_colors()
    
    for i in range(NB_COMMANDES):
        print(f"Envoi de la commande {i+1}/{NB_COMMANDES}...")
        create_random_order(colors)
        time.sleep(1) # Petite pause pour ne pas saturer le serveur

    print("🏁 Fin du script.")