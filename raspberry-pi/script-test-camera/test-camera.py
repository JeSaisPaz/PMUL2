# Time pour les timestamps
import time
# Os pour utiliser la commande ls
import os

import cv2

import numpy as np

# pyzbar pour la lecture des codes QR
from pyzbar.pyzbar import decode
# PIL pour utiliser le type d'objet 'Image'
from PIL import Image
# picamera2 pour prendre des photos depuis la camera du raspberry pi
from picamera2 import Picamera2, Preview

# dossiers
imgs_path = "./imgs"
logs_path = "./logs"

# initialisation de la camera
cam = Picamera2()
camera_config = cam.create_preview_configuration()
cam.configure(camera_config)
cam.start_preview(Preview.DRM)
cam.start()
time.sleep(2)

# fonction pour prendre une photo
def takePicture():
        filename = str(imgs_path + str(time.time())) + "_QR.jpg"
        cam.capture_file(filename)
        logfile = open(logs_path + str(time.time()) + "-Logs.txt", "w")
        logfile.write("[+]" + filename + "\n")
        logfile.close()
        return filename


# fonctions pour decoder une image QR depuis son path en binaire/texte
def imgToText(img_path):
        try:
            payload = decode(Image.open(img_path))
            for obj in payload:
                    message = obj.data.decode('utf-8')
            return message
        except FileNotFoundError:
            print("Erreur : Le fichier spécifié est introuvable.")
        except Exception as e:
            print(f"On a vraiment merdé la... : {e}")

def imgToBin(img_path):
        message = imgToText(img_path)
        binary_message = ' '.join(format(ord(c), '08b') for c in message)
        return binary_message       

# routine pour determiner le nom d'un fichier
def filenameRoutine():
        print("Entrez le nom du fichier que vous souhaitez lire")
        os.system("ls " + imgs_path)
        file_name = input("> ")
        return file_name

# fonction pour determiner la couleur d'un bloque
def imgToColor(img_path):
    # on ouvre l'image dans CV2
    img = cv2.imread(img_path) 
    if img is None:
        return "Fichier image introuvable"

    # on converti l'image BGR (OpenCV) en format HSV
    # cela fait sens car uniquement le canal H (teinte) peut etre analyser
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)

    # on décode l'image avec pyzbar
    qr_results = decode(Image.open(img_path))
    
    if not qr_results:
        return "Aucun QR code trouvé pour ancrer la détection"

    obj = qr_results[0] # On récupère le premier QR code détecté

    # on calcul des coordonnées de la zone autours du code QR
    horizontalQrCoordinates = min(obj.rect.left + obj.rect.width + 3, img.shape[1] - 15)
    verticalQrCoordinates = min(obj.rect.top + (obj.rect.height // 2), img.shape[0] - 15)

    # portion de pixels en dehors du code QR (10x10)
    pixelsOutsideQr = hsv[verticalQrCoordinates:verticalQrCoordinates+10, horizontalQrCoordinates:horizontalQrCoordinates+10]
    
    # Moyenne arithmetique du canal H (Uniquement la teinte)
    averageHue = np.mean(pixelsOutsideQr[:, :, 0])

    # decision de la couleur en fonction de la teinte
    if 25 <= averageHue < 35:
        return "Jaune"
    elif 85 <= averageHue < 105:
        return "Cyan"
    elif 140 <= averageHue < 160:
        return "Magenta"
    else:
        return f"Couleur inconnue (H:{int(averageHue)})"

# boucle principale du programme avec le menu et les calls de fonctions
while(True):
        print("""
PMUL2 - Menu
1) Prendre une photo
2) QR -> Texte
3) QR -> Binaire
4) Quitter
        """)
        answer = int(input("> "))
        if answer == 1:
                print("[+]" + takePicture())
        elif answer == 2:
                file_name = filenameRoutine()
                print("Result: " + imgToText(imgs_path + file_name))
        elif answer == 3:
                file_name = filenameRoutine()
                print("Result: " + imgToBin(imgs_path + file_name))
        elif answer == 4:
                exit()
        else:
            print("Erreur, commande introuvable")