# Time pour les timestamps
import time
# Os pour utiliser la commande ls
import os

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