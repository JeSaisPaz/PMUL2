import serial
import signal
import sys
import time
import cv2
import mysql.connector
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview

mydb = mysql.connector.connect(
  host="ip",
  user="username",
  password="password"
)


# Bits et autres pour notre protocol série
BEST_TEAM             = 0x01 # Nous, bien évidement
START_BYTE            = 0x02
END_BYTE              = 0x03
TARGET_ORDER_PREFIX   = 0xFF # On l'utilise pour specifier que c'est une commande que l'on demande
ORDER_DONE_PREFIX     = 0x00 # Pareil, pour specifier que la commande est terminée (traitée)
BAUD                  = 9600
PORT                  = "/dev/serial0"

class Color:
  YELLOW  = 0x01
  BLUE    = 0x02
  MAGENTA = 0x03

class Team:
  TEAM01 = 0x01
  TEAM02 = 0x02
  TEAM03 = 0x03
  TEAM04 = 0x04
  TEAM05 = 0x05

# Communication serie
s = serial.Serial(SERIAL_PORT, BAUD)
time.sleep(2)

# Pi -> Arduino
def sendBlockInfo(color, team):
  # Trame: [START_BYTE][coleur][team][END_BYTE]: total de 4 bytes
  s.write(bytes([START_BYTE, color, team, END_BYTE]))
  s.flush()

def sendTargetOrder(team, blue, yellow, magenta):
  checksum = team + blue + yellow + magenta
  # Trame: [START_BYTE][TARGET_ORDER_PREFIX][team][bleu][jaune][magenta][checksum][END_BYTE]
  s.write(bytes([START_BYTE, TARGET_ORDER_PREFIX, team, blue, yellow, magenta, END_BYTE]))