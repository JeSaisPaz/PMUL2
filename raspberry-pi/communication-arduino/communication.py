# Ce programme est un test pour realiser une communication entre un arduino MEGA et un raspberry pi 4

import serial
import time

# Arguments: Port serie (GPIO sur le raspberry) et bande
# Note: '/dev/ttyACM0' si par USB
s = serial.Serial('/dev/serial0', 9600)
# Temps de delai pour que la connexion serie soit bien etablie
time.sleep(2) 

def send():
    message_payload = input("Quel message envoyer ? > ")
    # Pourquoi b'' ?: On doit convertir la chaine de char en binaire ASCII pour etre transmissible par serial
    s.write(f"{message_payload}\n".encode('utf-8'))
    print(f"Message '{message_payload}' envoyé.")

    print("Message sent.")

def receive():
    print("Waiting for Arduino message...")
    payload = s.readline()
    payload.decode('utf-8').strip()
    if payload:
        print(f"Received from Arduino: {payload}")
    


while True:
    # .lower pour accepter HELP et help
    commande = str(input("> ")).lower()
    if commande == "help":
        print("""
send: Envoie un message de teste a l'arduino mega
receive: Attends un message de test de l'arduino mega
        """)
    elif commande == "send":
        send()
    elif commande == "receive":
        receive()
    elif commande == "exit":
        s.close()
        break
